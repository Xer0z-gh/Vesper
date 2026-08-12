#include "VesperProcessor.h"
#include "VesperEditor.h"

using namespace vesper;

//==============================================================================
VesperProcessor::VesperProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, &undoManager, "VESPER", createParameterLayout()),
      presets(apvts, *this)
{
    // Cache every raw parameter atomic once.
    for (auto* param : getParameters())
        if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            raw[withID->paramID] = apvts.getRawParameterValue(withID->paramID);

    apvts.addParameterListener(ids::oversamp, this);
    for (int m = 1; m <= 4; ++m)
        apvts.addParameterListener("macro" + juce::String(m), this);
    for (auto* id : { "rev_type", "rev_decay", "rev_size" })
        apvts.addParameterListener(id, this);
    for (auto* id : { "mch_on", "mch_amount", "mch_smooth", "mch_maxdb" })
        apvts.addParameterListener(id, this);
    apvts.addParameterListener("eq_lp", this); // D-036: linear-phase redesign
    for (int b = 0; b < dsp::ParametricEQ::numBands; ++b)
        for (auto* s : { "on", "type", "freq", "gain", "q" }) // dyn: static curve only
            apvts.addParameterListener(eqBandID(b, s), this);
    convRegen.fn  = [this] { regenerateConvIR(); };
    matchRegen.fn = [this] { regenerateMatch(); };
    lpRegen.fn    = [this] { regenerateLpEQ(); };
    otherState = apvts.copyState();

    presets.onStateReloaded = [this]
    {
        chainOrder.store(
            packOrder(orderFromString(apvts.state.getProperty("chainOrder").toString())),
            std::memory_order_relaxed);
        latencyDirty = true;
    };
}

VesperProcessor::~VesperProcessor()
{
    convRegen.cancelPendingUpdate();
    apvts.removeParameterListener(ids::oversamp, this);
    for (int m = 1; m <= 4; ++m)
        apvts.removeParameterListener("macro" + juce::String(m), this);
    for (auto* id : { "rev_type", "rev_decay", "rev_size" })
        apvts.removeParameterListener(id, this);
}

void VesperProcessor::regenerateConvIR()
{
    const int type = (int) rv("rev_type");
    if (type == 7) // User IR (D-038): fall back to Cathedral if it won't load
    {
        if (userIRPath.isNotEmpty() && convReverb.loadUserIR(juce::File(userIRPath)))
            return;
        userIRLoadFailed = userIRPath.isNotEmpty();
        convReverb.regenerate(dsp::ConvReverb::cathedral, rv("rev_decay"), rv("rev_size"));
    }
    else if (type >= 4) // Spring / Cathedral / Reverse
    {
        userIRLoadFailed = false;
        convReverb.regenerate(type - 4, rv("rev_decay"), rv("rev_size"));
    }
}

bool VesperProcessor::loadUserIR(const juce::File& f)
{
    const bool ok = convReverb.loadUserIR(f);
    if (ok)
    {
        userIRPath = f.getFullPathName();
        userIRLoadFailed = false;
        apvts.state.setProperty("userIRPath", userIRPath, nullptr);
    }
    else userIRLoadFailed = true;
    return ok;
}

void VesperProcessor::regenerateMatch()
{
    // message thread: shape the learned curve into the linear-phase FIR
    matchEQ.setActiveReference((int) rv("mch_ref")); // D-039
    matchEQ.setParams(rb("mch_on"), rv("mch_amount"), rv("mch_smooth"), rv("mch_maxdb"));
    latencyDirty = true; // active/latency may have flipped
}

void VesperProcessor::regenerateLpEQ()
{
    // message thread: rebuild the linear-phase FIR from the current bands.
    // Runs on every EQ edit (armed or not) so the engine is always warm —
    // toggling eq_lp never meets an unloaded convolution.
    std::array<dsp::ParametricEQ::Band, dsp::ParametricEQ::numBands> b {};
    for (int i = 0; i < dsp::ParametricEQ::numBands; ++i)
    {
        b[(size_t) i].on   = raw.at(eqBandID(i, "on"))->load() > 0.5f;
        b[(size_t) i].type = (int) raw.at(eqBandID(i, "type"))->load();
        b[(size_t) i].freq = raw.at(eqBandID(i, "freq"))->load();
        b[(size_t) i].gain = raw.at(eqBandID(i, "gain"))->load();
        b[(size_t) i].q    = raw.at(eqBandID(i, "q"))->load();
    }
    lpEQ.design(b.data());
}

//==============================================================================
bool VesperProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    // Sidechain: absent, mono or stereo
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.getChannelSet(true, 1);
        return sc.isDisabled() || sc == juce::AudioChannelSet::mono()
            || sc == juce::AudioChannelSet::stereo();
    }
    return true;
}

void VesperProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    dsp::Context ctx { sampleRate, samplesPerBlock, 2 };

    eq.prepare(ctx);
    filter.prepare(ctx);
    comp.prepare(ctx);
    mbComp.prepare(ctx);
    gate.prepare(ctx);
    sat.prepare(ctx);
    transient.prepare(ctx);
    modFX.prepare(ctx);
    pitch.prepare(ctx);
    delay.prepare(ctx);
    reverb.prepare(ctx);
    convReverb.prepare(ctx);
    stereo.prepare(ctx);
    limiter.prepare(ctx);
    matchEQ.prepare(ctx);
    lpEQ.prepare(ctx);
    regenerateConvIR(); // build the initial IR if a convolution space is active
    regenerateMatch();  // rebuild the match FIR if a curve was restored
    regenerateLpEQ();   // linear-phase FIR always resident (D-036)

    meterIn.prepare(sampleRate);
    meterOut.prepare(sampleRate);

    wet.setSize(2, samplesPerBlock);
    dry.setSize(2, samplesPerBlock);
    for (auto& d : dryDelay) d.prepare(sampleRate, 0.1);
    for (auto& d : satComp)  d.prepare(sampleRate, 0.05);
    for (auto& d : limComp)  d.prepare(sampleRate, 0.05);
    for (auto& d : eqLpComp) d.prepare(sampleRate, 0.05); // >= 1024 smp at any fs

    inGainSm.reset(sampleRate, 0.03);
    outGainSm.reset(sampleRate, 0.03);
    mixSm.reset(sampleRate, 0.03);

    latencyDirty = true;
    refreshLatency();
}

void VesperProcessor::refreshLatency()
{
    const int osIdx = (int) rv(ids::oversamp);
    sat.setOversample(osIdx);
    lpEQ.setArmed(rb("eq_lp")); // armed state feeds the latency it reports
    currentLatency = sat.latencySamples() + limiter.latencySamples()
                   + matchEQ.latencySamples() + lpEQ.latencySamples();
    setLatencySamples((int) std::round(currentLatency));
    latencyDirty = false;
}

void VesperProcessor::parameterChanged(const juce::String& id, float)
{
    if (id == ids::oversamp)
        latencyDirty = true;
    else if (id.startsWith("macro"))
        macros.trigger(); // realtime-safe; targets apply on the message thread
    else if (id == "rev_type" || id == "rev_decay" || id == "rev_size")
        convRegen.triggerAsyncUpdate(); // rebuild the IR off the audio thread
    else if (id.startsWith("mch_"))
        matchRegen.triggerAsyncUpdate(); // redesign the match FIR off the audio thread
    else if (id == "eq_lp")
    {
        latencyDirty = true;
        lpRegen.triggerAsyncUpdate();
    }
    else if (id.startsWith("eq_b"))
        lpRegen.triggerAsyncUpdate(); // keep the linear-phase FIR warm (D-036)
}

//==============================================================================
void VesperProcessor::updateModuleParams()
{
    // EQ
    for (int b = 0; b < dsp::ParametricEQ::numBands; ++b)
    {
        dsp::ParametricEQ::Band band;
        band.on   = raw.at(eqBandID(b, "on"))->load() > 0.5f;
        band.type = (int) raw.at(eqBandID(b, "type"))->load();
        band.freq = raw.at(eqBandID(b, "freq"))->load();
        band.gain = raw.at(eqBandID(b, "gain"))->load();
        band.q    = raw.at(eqBandID(b, "q"))->load();
        band.dyn  = raw.at(eqBandID(b, "dyn"))->load();
        eq.setBand(b, band);
    }
    filter.set((int) rv("flt_type"), rv("flt_cutoff"), rv("flt_res"), rv("flt_drive"));
    comp.set(rv("cmp_thresh"), rv("cmp_ratio"), rv("cmp_attack"), rv("cmp_release"),
             rv("cmp_knee"), rv("cmp_makeup"), rv("cmp_upward"), rv("cmp_mix"),
             (int) rv("cmp_detect"), (int) rv("cmp_character"), rb("cmp_sc"));
    sat.setAlgo((int) rv("sat_algo"));
    sat.setDrive(rv("sat_drive"));
    sat.setBias(rv("sat_bias"));
    sat.setTone(rv("sat_tone"));
    sat.setMix(rv("sat_mix") * 0.01f);
    sat.setTrim(rv("sat_out"));
    transient.set(rv("trn_attack"), rv("trn_sustain"), (int) rv("trn_speed"));
    // M6: synced Motion rides the host tempo through the delay's division table
    float modRate = rv("mod_rate");
    if (rb("mod_sync") && hostBpm > 1.0)
        modRate = juce::jlimit(0.02f, 20.0f,
                               (float) (hostBpm / 60.0 / delayDivBeats((int) rv("mod_div"))));
    modFX.set((int) rv("mod_mode"), modRate, rv("mod_depth"),
              rv("mod_feedback"), rv("mod_spread"), rv("mod_mix"));
    delay.set(rb("dly_sync"), rv("dly_time"), (int) rv("dly_div"), hostBpm,
              rv("dly_feedback"), rb("dly_pingpong"), (int) rv("dly_color"),
              rv("dly_wow"), rv("dly_locut"), rv("dly_hicut"),
              rv("dly_diffuse"), rv("dly_width"), rv("dly_mix"));
    reverb.set((int) rv("rev_type"), rv("rev_size"), rv("rev_decay"), rv("rev_predelay"),
               rv("rev_damp"), rv("rev_locut"), rv("rev_shimmer"), rv("rev_width"),
               rv("rev_mix"));
    convReverb.setParams(rv("rev_predelay"), rv("rev_damp"), rv("rev_locut"),
                         rv("rev_width"), rv("rev_mix"));
    convReverb.setShimmer(rv("rev_shimmer")); // D-038: conv spaces shimmer too
    stereo.set(rv("st_width"), rv("st_mid"), rv("st_side"), rv("st_haas"),
               rv("st_pan"), rv("st_monofreq"));
    limiter.set(rv("lim_gain"), rv("lim_ceiling"), rv("lim_release"),
                rb("lim_softclip"), rb("lim_tp"));
    gate.set(rv("gte_thresh"), rv("gte_range"), rv("gte_attack"), rv("gte_release"),
             rv("gte_hold"), (int) rv("gte_mode"), rb("gte_sc"));
    mbComp.set(rv("mbc_xlow"), rv("mbc_xhigh"),
               rv("mbc_lo_thresh"), rv("mbc_lo_ratio"), rv("mbc_lo_gain"),
               rv("mbc_mid_thresh"), rv("mbc_mid_ratio"), rv("mbc_mid_gain"),
               rv("mbc_hi_thresh"), rv("mbc_hi_ratio"), rv("mbc_hi_gain"),
               rv("mbc_attack"), rv("mbc_release"));
    pitch.set((int) rv("pit_mode"), rv("pit_amount"), rv("pit_mix"));
}

void VesperProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    if (latencyDirty.load(std::memory_order_relaxed))
        refreshLatency();

    if (rb(ids::bypass))
        return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm = *bpm;

    updateModuleParams();

    // ---- stage the MAIN bus into internal stereo buffers (bus-aware:
    //      with a sidechain present, raw channel 1 may not be main R) ----
    auto mainBus = getBusBuffer(buffer, true, 0);
    const int mainCh = mainBus.getNumChannels();
    wet.setSize(2, n, false, false, true);
    dry.setSize(2, n, false, false, true);
    wet.copyFrom(0, 0, mainBus, 0, 0, n);
    wet.copyFrom(1, 0, mainBus, mainCh > 1 ? 1 : 0, 0, n);

    // ---- sidechain tap for Comp + Gate detectors (D-021) ----
    const float* scL = nullptr;
    const float* scR = nullptr;
    if (getBusCount(true) > 1)
    {
        auto sc = getBusBuffer(buffer, true, 1);
        if (sc.getNumChannels() > 0)
        {
            scL = sc.getReadPointer(0);
            scR = sc.getNumChannels() > 1 ? sc.getReadPointer(1) : scL;
        }
    }
    comp.setSidechain(scL, scR);
    gate.setSidechain(scL, scR);

    // input gain
    inGainSm.setTargetValue(dsp::dbToGain(rv(ids::inGain)));
    for (int i = 0; i < n; ++i)
    {
        const float g = inGainSm.getNextValue();
        wet.getWritePointer(0)[i] *= g;
        wet.getWritePointer(1)[i] *= g;
    }
    meterIn.push(wet);
    dry.makeCopyOf(wet, true);

    // ---- Match EQ (D-032): learn tap on the raw input, then apply the
    //      linear-phase correction before the reorderable chain. The dry
    //      copy above stays pre-match so the global Mix blends toward the
    //      true input; currentLatency includes the FIR group delay. ----
    if (! rb("mch_tap")) matchEQ.pushInput(wet); // D-039: tap = Input
    matchEQ.process(wet);

    // ---- the chain ----
    const auto order = getChainOrder();
    for (int slot : order)
    {
        const bool on = raw.at(moduleOnID(slot))->load() > 0.5f;
        switch (slot)
        {
            case mEQ: // D-036: linear-phase mode swaps the biquads for the FIR
            {
                const double lat = lpEQ.latencySamples();
                if (on && lpEQ.isArmed() && lpEQ.ready())
                {
                    lpEQ.process(wet); // FIR carries the reported delay itself
                    for (int b = 0; b < dsp::ParametricEQ::numBands; ++b)
                        eq.dynMeterDb[(size_t) b].store(0.0f, std::memory_order_relaxed);
                }
                else
                {
                    if (on) eq.process(wet);
                    if (lat > 0.01) // armed but bypassed/loading: matched delay
                        for (int ch = 0; ch < 2; ++ch)
                        {
                            auto* d = wet.getWritePointer(ch);
                            for (int i = 0; i < n; ++i)
                            {
                                eqLpComp[(size_t) ch].write(d[i]);
                                d[i] = eqLpComp[(size_t) ch].readLinear(lat);
                            }
                        }
                }
                break;
            }
            case mFilter:    if (on) filter.process(wet);    break;
            case mComp:      if (on) comp.process(wet);      break;
            case mMBComp:    if (on) mbComp.process(wet);    break;
            case mGate:      if (on) gate.process(wet);      break;
            case mTransient: if (on) transient.process(wet); break;
            case mModFX:     if (on) modFX.process(wet);     break;
            case mPitch:     if (on) pitch.process(wet);     break;
            case mDelay:     if (on) delay.process(wet);     break;
            case mReverb:    if (on) { if ((int) rv("rev_type") >= 4) convReverb.process(wet);
                                       else reverb.process(wet); } break;
            case mStereo:    if (on) stereo.process(wet);    break;

            case mSat: // latency-bearing: bypassed -> matched delay
            {
                const double lat = sat.latencySamples();
                if (on) sat.process(wet);
                else if (lat > 0.01)
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        auto* d = wet.getWritePointer(ch);
                        for (int i = 0; i < n; ++i)
                        {
                            satComp[(size_t) ch].write(d[i]);
                            d[i] = satComp[(size_t) ch].readLinear(lat);
                        }
                    }
                break;
            }
            case mLimiter:
            {
                const double lat = limiter.latencySamples();
                if (on) limiter.process(wet);
                else
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        auto* d = wet.getWritePointer(ch);
                        for (int i = 0; i < n; ++i)
                        {
                            limComp[(size_t) ch].write(d[i]);
                            d[i] = limComp[(size_t) ch].readLinear(lat);
                        }
                    }
                break;
            }
            default: break;
        }
    }

    // ---- global parallel mix (latency-compensated dry, null at 100%) ----
    mixSm.setTargetValue(rv(ids::mix) * 0.01f);
    outGainSm.setTargetValue(dsp::dbToGain(rv(ids::outGain)));
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* w = wet.getWritePointer(ch);
        const auto* d = dry.getReadPointer(ch);
        for (int i = 0; i < n; ++i)
        {
            dryDelay[(size_t) ch].write(d[i]);
            const float dd  = dryDelay[(size_t) ch].readLinear(juce::jmax(1.0, currentLatency));
            const float mix = ch == 0 ? mixSm.getNextValue() : mixSm.getCurrentValue();
            const float og  = ch == 0 ? outGainSm.getNextValue() : outGainSm.getCurrentValue();
            w[i] = (dd + (w[i] - dd) * mix) * og;
        }
    }

    meterOut.push(wet);
    fftTap.push(wet);
    if (rb("mch_tap")) matchEQ.pushInput(wet); // D-039: tap = Output (post-chain)

    // ---- back to the host's MAIN output bus ----
    auto outBus = getBusBuffer(buffer, false, 0);
    if (outBus.getNumChannels() > 1)
    {
        outBus.copyFrom(0, 0, wet, 0, 0, n);
        outBus.copyFrom(1, 0, wet, 1, 0, n);
    }
    else // mono bus: equal-gain fold-down
    {
        outBus.copyFrom(0, 0, wet, 0, 0, n);
        outBus.addFrom(0, 0, wet, 1, 0, n);
        outBus.applyGain(0, 0, n, 0.5f);
    }
}

//==============================================================================
void VesperProcessor::toggleAB()
{
    auto current = apvts.copyState();
    apvts.replaceState(otherState);
    if (auto orderStr = apvts.state.getProperty("chainOrder").toString(); orderStr.isNotEmpty())
        chainOrder.store(packOrder(orderFromString(orderStr)), std::memory_order_relaxed);
    otherState = std::move(current);
    stateIsB = ! stateIsB;
}

void VesperProcessor::copyActiveToOther()
{
    otherState = apvts.copyState();
}

//==============================================================================
juce::String VesperProcessor::orderToString(const std::array<int, numModules>& o)
{
    juce::StringArray s;
    for (auto v : o) s.add(juce::String(v));
    return s.joinIntoString(",");
}

std::array<int, numModules> VesperProcessor::orderFromString(const juce::String& str)
{
    auto def = unpackOrder(defaultOrder());
    juce::StringArray tok;
    tok.addTokens(str, ",", "");
    if (tok.size() != numModules) return def;

    std::array<int, numModules> o {};
    std::array<bool, numModules> seen {};
    for (int i = 0; i < numModules; ++i)
    {
        const int v = tok[i].getIntValue();
        if (v < 0 || v >= numModules || seen[(size_t) v]) return def; // corrupt -> default
        o[(size_t) i] = v;
        seen[(size_t) v] = true;
    }
    return o;
}

void VesperProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    apvts.state.setProperty("chainOrder",
                            orderToString(getChainOrder()), nullptr);
    apvts.state.setProperty("presetName", presets.getCurrentName(), nullptr);
    apvts.state.setProperty("matchCurve", matchEQ.saveCurve(), nullptr);
    apvts.state.setProperty("userIRPath", userIRPath, nullptr);
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void VesperProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (! xml->hasTagName(apvts.state.getType())) return;
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
        const auto orderStr = apvts.state.getProperty("chainOrder").toString();
        chainOrder.store(packOrder(orderFromString(orderStr)), std::memory_order_relaxed);
        presets.setCurrentName(apvts.state.getProperty("presetName").toString());
        matchEQ.loadCurve(apvts.state.getProperty("matchCurve").toString());
        regenerateMatch(); // apply mch_* params to the restored curve
        userIRPath = apvts.state.getProperty("userIRPath").toString();
        convRegen.triggerAsyncUpdate(); // reload the user IR off the audio thread
        lpRegen.triggerAsyncUpdate(); // rebuild the LP FIR from restored bands
        latencyDirty = true;
    }
}

juce::AudioProcessorEditor* VesperProcessor::createEditor()
{
    return new VesperEditor(*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VesperProcessor();
}
