/*  IntegrationMain.cpp — full-processor integration tests (D-043).

    TestMain.cpp exercises DSP classes in isolation. This suite instantiates
    the real VesperProcessor and drives it the way a host does: prepare,
    process, automate, save/restore, reorder, at many sample rates and block
    sizes. It exists because isolated component tests can all pass while the
    assembled plugin is broken — a design function proven correct says
    nothing about whether the parameter reaches it.

    Everything here is headless and deterministic; no editor is created. */

#include "VesperProcessor.h"
#include <cstdio>
#include <vector>

static int failures = 0;
static int checks   = 0;

static void check(bool ok, const char* name)
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
    ++checks;
    if (! ok) ++failures;
}

//==============================================================================
namespace helper
{
    struct Stats { float peak = 0.0f; double rms = 0.0; bool finite = true; };

    inline Stats analyse(const juce::AudioBuffer<float>& b)
    {
        Stats s;
        double sum = 0.0;
        int n = 0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = b.getSample(ch, i);
                if (! std::isfinite(v)) s.finite = false;
                s.peak = juce::jmax(s.peak, std::abs(v));
                sum += (double) v * v;
                ++n;
            }
        s.rms = n > 0 ? std::sqrt(sum / n) : 0.0;
        return s;
    }

    /* Musical-ish excitation: a chord plus transients, so every module has
       something real to chew on (a pure sine hides plenty of bugs). */
    inline void fillProgram(juce::AudioBuffer<float>& b, double fs, double& phase, int blockIdx)
    {
        const int n = b.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            const double t = phase + i;
            double v = 0.34 * std::sin(juce::MathConstants<double>::twoPi * 110.0 * t / fs)
                     + 0.22 * std::sin(juce::MathConstants<double>::twoPi * 277.0 * t / fs)
                     + 0.16 * std::sin(juce::MathConstants<double>::twoPi * 1320.0 * t / fs)
                     + 0.08 * std::sin(juce::MathConstants<double>::twoPi * 6300.0 * t / fs);
            if (((blockIdx * n + i) % 12000) < 40) v += 0.55; // periodic transient
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                b.setSample(ch, i, (float) (v * 0.6));
        }
        phase += n;
    }

    inline void silence(juce::AudioBuffer<float>& b) { b.clear(); }

    /* Render `blocks` blocks of program material, returning the worst stats
       seen after an optional warm-up. */
    inline Stats render(VesperProcessor& p, int blockSize, int blocks,
                        int warmupBlocks = 0, bool feedSilenceAfterWarmup = false)
    {
        juce::AudioBuffer<float> buf(2, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;
        Stats worst;
        for (int b = 0; b < blocks; ++b)
        {
            if (feedSilenceAfterWarmup && b >= warmupBlocks) silence(buf);
            else fillProgram(buf, p.getSampleRate(), phase, b);

            p.processBlock(buf, midi);

            if (b < warmupBlocks && ! feedSilenceAfterWarmup) continue;
            const auto s = analyse(buf);
            worst.peak = juce::jmax(worst.peak, s.peak);
            worst.rms  = juce::jmax(worst.rms, s.rms);
            if (! s.finite) worst.finite = false;
        }
        return worst;
    }

    inline void setParam(VesperProcessor& p, const juce::String& id, float value01)
    {
        if (auto* prm = p.apvts.getParameter(id))
            prm->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, value01));
    }
    inline void setParamRaw(VesperProcessor& p, const juce::String& id, float raw)
    {
        if (auto* prm = p.apvts.getParameter(id))
            prm->setValueNotifyingHost(prm->convertTo0to1(raw));
    }
    inline float getParam01(VesperProcessor& p, const juce::String& id)
    {
        if (auto* prm = p.apvts.getParameter(id)) return prm->getValue();
        return -1.0f;
    }

    /* Pump the message loop so AsyncUpdaters (conv IR, match, LP EQ) run —
       the processor defers all allocation there by design. */
    inline void pumpMessages(int ms = 250)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
        while (juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }

    /* The processor refreshes its reported latency inside processBlock (the
       JUCE-correct place: setLatencySamples on the audio thread). So a host
       — and this harness — only sees a new value after the next block. */
    inline int latencyAfterOneBlock(VesperProcessor& p, int blockSize = 512)
    {
        juce::AudioBuffer<float> buf(2, blockSize);
        juce::MidiBuffer midi;
        buf.clear();
        p.processBlock(buf, midi);
        return p.getLatencySamples();
    }

    /* Semantic comparison: AudioParameterBool::getValue() returns the raw
       normalised float it was handed (0.85), while saved state carries the
       resolved boolean (1.0). Both mean "true", so compare what the
       parameter reports, not the float it happens to be holding. */
    inline juce::String semanticValue(const juce::AudioProcessorParameter* p)
    {
        return p->getText(p->getValue(), 64);
    }
} // namespace helper

using namespace helper;

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit; // message loop for AsyncUpdaters
    std::printf("=== Vesper integration suite ===\n\n");

    // ---------------------------------------------------------------------
    // 1. Sample-rate matrix — every rate a host might hand us.
    // ---------------------------------------------------------------------
    {
        bool allOk = true;
        juce::String bad;
        for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        {
            VesperProcessor p;
            p.setPlayConfigDetails(2, 2, fs, 512);
            p.prepareToPlay(fs, 512);
            const auto s = render(p, 512, 60, 10);
            const bool ok = s.finite && s.peak < 8.0f && s.peak > 1.0e-6f;
            if (! ok) { allOk = false; bad += juce::String(fs) + " "; }
            p.releaseResources();
        }
        check(allOk, ("sample rates: 44k1-192k render finite and bounded"
                      + (allOk ? juce::String() : " [bad: " + bad + "]")).toRawUTF8());
    }

    // ---------------------------------------------------------------------
    // 2. Block-size matrix — including 1, primes and non-power-of-two, which
    //    is where ring-buffer and chunked-loop bugs hide.
    // ---------------------------------------------------------------------
    {
        bool allOk = true;
        juce::String bad;
        for (int bs : { 1, 7, 13, 32, 64, 127, 128, 256, 511, 512, 1024, 2048 })
        {
            VesperProcessor p;
            p.setPlayConfigDetails(2, 2, 48000.0, bs);
            p.prepareToPlay(48000.0, bs);
            const int blocks = juce::jmax(20, 24000 / juce::jmax(1, bs));
            const auto s = render(p, bs, blocks, blocks / 4);
            if (! (s.finite && s.peak < 8.0f)) { allOk = false; bad += juce::String(bs) + " "; }
            p.releaseResources();
        }
        check(allOk, ("block sizes: 1..2048 (incl. primes) render finite and bounded"
                      + (allOk ? juce::String() : " [bad: " + bad + "]")).toRawUTF8());
    }

    // ---------------------------------------------------------------------
    // 3. Every factory preset: load, render, assert sane. This is the
    //    "render each preset, assert no clip/runaway" roadmap item.
    // ---------------------------------------------------------------------
    {
        VesperProcessor p;
        p.setPlayConfigDetails(2, 2, 48000.0, 512);
        p.prepareToPlay(48000.0, 512);

        const auto all = p.presets.getAll();
        int tested = 0, nonFinite = 0, runaway = 0, silent = 0;
        juce::StringArray offenders;

        for (const auto& info : all)
        {
            p.presets.loadPreset(info);
            pumpMessages(60);             // let conv/LP/match IRs load
            p.reset();
            const auto s = render(p, 512, 90, 20); // ~1 s, warm-up discarded
            ++tested;
            if (! s.finite)      { ++nonFinite; offenders.add(info.name + " (non-finite)"); }
            else if (s.peak > 4.0f) { ++runaway; offenders.add(info.name + " (peak "
                                                              + juce::String(s.peak, 2) + ")"); }
            else if (s.rms < 1.0e-5)  { ++silent;  offenders.add(info.name + " (silent)"); }
        }
        for (int i = 0; i < juce::jmin(8, offenders.size()); ++i)
            std::printf("      %s\n", offenders[i].toRawUTF8());

        check(tested >= 20, "presets: all factory presets loaded for rendering");
        check(nonFinite == 0, "presets: none produce NaN/Inf");
        check(runaway == 0,   "presets: none run away (peak < 4.0 over 1 s)");
        check(silent == 0,    "presets: none render silence");
    }

    // ---------------------------------------------------------------------
    // 4. Linear-phase EQ in the REAL chain (D-036) — the isolated test only
    //    proved the kernel design; this proves the parameter reaches it,
    //    the host is told the latency, and audio still flows.
    // ---------------------------------------------------------------------
    {
        VesperProcessor p;
        p.setPlayConfigDetails(2, 2, 48000.0, 512);
        p.prepareToPlay(48000.0, 512);

        setParamRaw(p, "eq_on", 1.0f);
        setParamRaw(p, "eq_b2_on", 1.0f);
        setParamRaw(p, "eq_b2_freq", 1000.0f);
        setParamRaw(p, "eq_b2_gain", 9.0f);
        pumpMessages(120);

        const int latMin = latencyAfterOneBlock(p);
        setParamRaw(p, "eq_lp", 1.0f);          // arm linear phase
        pumpMessages(400);                       // FIR designs + convolution loads
        const int latLp = latencyAfterOneBlock(p);

        const auto s = render(p, 512, 80, 20);
        setParamRaw(p, "eq_lp", 0.0f);
        pumpMessages(150);
        const int latOff = latencyAfterOneBlock(p);
        const auto s2 = render(p, 512, 40, 10);

        check(latLp >= latMin + 1000 && latLp <= latMin + 1100,
              "linear-phase EQ: arming adds ~1024 samples of reported latency");
        check(latOff == latMin, "linear-phase EQ: disarming restores latency");
        check(s.finite && s.peak < 4.0f && s.rms > 1.0e-4,
              "linear-phase EQ: processes real audio, finite and bounded");
        check(s2.finite && s2.rms > 1.0e-4,
              "linear-phase EQ: audio still flows after disarming");
    }

    // ---------------------------------------------------------------------
    // 5. Match EQ end-to-end (D-032/D-039): learn -> match -> apply.
    // ---------------------------------------------------------------------
    {
        VesperProcessor p;
        p.setPlayConfigDetails(2, 2, 48000.0, 512);
        p.prepareToPlay(48000.0, 512);

        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer midi;
        double phase = 0.0;

        p.matchEQ.setActiveReference(0);
        p.matchEQ.startLearn(true);                    // reference
        for (int b = 0; b < 30; ++b)
        {
            fillProgram(buf, 48000.0, phase, b);
            p.processBlock(buf, midi);
            p.matchEQ.pumpLearn();
        }
        p.matchEQ.stopLearn();
        const bool refOk = p.matchEQ.referenceReady(0);

        p.matchEQ.startLearn(false);                   // source
        for (int b = 0; b < 30; ++b)
        {
            fillProgram(buf, 48000.0, phase, b);
            for (int i = 0; i < 512; ++i)              // duller source
                for (int ch = 0; ch < 2; ++ch)
                    buf.setSample(ch, i, buf.getSample(ch, i) * 0.5f);
            p.processBlock(buf, midi);
            p.matchEQ.pumpLearn();
        }
        p.matchEQ.stopLearn();
        const bool srcOk = p.matchEQ.sourceReady();

        p.matchEQ.computeMatch();
        const bool hasMatch = p.matchEQ.hasMatch();
        const int latBefore = latencyAfterOneBlock(p);
        setParamRaw(p, "mch_on", 1.0f);
        pumpMessages(400);
        const int latAfter = latencyAfterOneBlock(p);

        const auto s = render(p, 512, 80, 20);
        check(refOk && srcOk && hasMatch, "match EQ: learns reference + source, computes a match");
        check(latAfter >= latBefore + 1000, "match EQ: engaging reports added latency");
        check(s.finite && s.peak < 4.0f && s.rms > 1.0e-4,
              "match EQ: applied correction processes finite, bounded audio");
    }

    // ---------------------------------------------------------------------
    // 6. User IR (D-038) in the real chain, and the missing-file fallback.
    // ---------------------------------------------------------------------
    {
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("vesper_integration_ir.wav");
        {
            juce::AudioBuffer<float> ir(2, 24000);
            auto& rng = juce::Random::getSystemRandom();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < ir.getNumSamples(); ++i)
                    ir.setSample(ch, i, (float) ((rng.nextDouble() * 2.0 - 1.0)
                                                 * std::exp(-4.0 * i / 24000.0)));
            juce::WavAudioFormat wav;
            tmp.deleteFile();
            if (auto* os = tmp.createOutputStream().release())
                if (auto* w = wav.createWriterFor(os, 48000.0, 2, 16, {}, 0))
                {
                    w->writeFromAudioSampleBuffer(ir, 0, ir.getNumSamples());
                    delete w;
                }
        }

        VesperProcessor p;
        p.setPlayConfigDetails(2, 2, 48000.0, 512);
        p.prepareToPlay(48000.0, 512);
        setParamRaw(p, "rev_on", 1.0f);
        setParamRaw(p, "rev_mix", 60.0f);

        const bool loaded = p.loadUserIR(tmp);
        pumpMessages(300);
        const auto s = render(p, 512, 80, 20);

        // now break it: point at a deleted file and re-trigger the space
        tmp.deleteFile();
        VesperProcessor p2;
        p2.setPlayConfigDetails(2, 2, 48000.0, 512);
        p2.prepareToPlay(48000.0, 512);
        setParamRaw(p2, "rev_on", 1.0f);
        setParamRaw(p2, "rev_mix", 60.0f);
        const bool refused = ! p2.loadUserIR(tmp);
        setParamRaw(p2, "rev_type", 7.0f);   // User IR with nothing loaded
        pumpMessages(300);
        const auto s2 = render(p2, 512, 60, 15);

        check(loaded && s.finite && s.peak < 4.0f && s.rms > 1.0e-4,
              "user IR: loads into the live chain and reverberates");
        check(refused && s2.finite && s2.rms > 1.0e-4,
              "user IR: missing file falls back audibly, never silence or NaN");
    }

    // ---------------------------------------------------------------------
    // 7. State round-trip: randomise every parameter, save, wipe, restore,
    //    compare. A single mis-registered parameter shows up here.
    // ---------------------------------------------------------------------
    {
        VesperProcessor a;
        a.setPlayConfigDetails(2, 2, 48000.0, 512);
        a.prepareToPlay(48000.0, 512);

        juce::Random rng(20260722);
        std::vector<std::pair<juce::String, juce::String>> written;
        for (auto* prm : a.getParameters())
            if (auto* wid = dynamic_cast<juce::AudioProcessorParameterWithID*>(prm))
            {
                wid->setValueNotifyingHost(rng.nextFloat());
                written.emplace_back(wid->paramID, semanticValue(wid));
            }

        juce::MemoryBlock blob;
        a.getStateInformation(blob);

        VesperProcessor b;
        b.setPlayConfigDetails(2, 2, 48000.0, 512);
        b.prepareToPlay(48000.0, 512);
        b.setStateInformation(blob.getData(), (int) blob.getSize());

        int mismatches = 0;
        juce::StringArray bad;
        for (const auto& [id, v] : written)
            if (auto* prm = b.apvts.getParameter(id))
                if (semanticValue(prm) != v)
                {
                    ++mismatches;
                    bad.add(id + " '" + v + "' -> '" + semanticValue(prm) + "'");
                }
        for (int i = 0; i < juce::jmin(6, bad.size()); ++i)
            std::printf("      %s\n", bad[i].toRawUTF8());

        check(! written.empty() && mismatches == 0,
              "state: every parameter survives save/restore exactly");

        // and the restored processor must still make sound
        pumpMessages(200);
        const auto s = render(b, 512, 60, 15);
        check(s.finite, "state: restored processor renders finite audio");
    }

    // ---------------------------------------------------------------------
    // 8. Chain-reorder torture with parameter storms (roadmap QA item).
    // ---------------------------------------------------------------------
    {
        VesperProcessor p;
        p.setPlayConfigDetails(2, 2, 48000.0, 256);
        p.prepareToPlay(48000.0, 256);

        juce::Random rng(777);
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        double phase = 0.0;
        bool finite = true;
        float peak = 0.0f, peakFirstHalf = 0.0f, peakSecondHalf = 0.0f;

        auto params = p.getParameters();
        for (int b = 0; b < 400; ++b)
        {
            // storm: 12 random parameter writes per block
            for (int k = 0; k < 12; ++k)
                if (auto* prm = params[rng.nextInt(params.size())])
                    prm->setValueNotifyingHost(rng.nextFloat());

            // reorder the chain every few blocks
            if (b % 5 == 0)
            {
                auto order = p.getChainOrder();
                std::swap(order[(size_t) rng.nextInt(vesper::numModules)],
                          order[(size_t) rng.nextInt(vesper::numModules)]);
                p.setChainOrder(order);
            }

            fillProgram(buf, 48000.0, phase, b);
            p.processBlock(buf, midi);
            const auto s = analyse(buf);
            if (! s.finite) finite = false;
            peak = juce::jmax(peak, s.peak);
            (b < 200 ? peakFirstHalf : peakSecondHalf)
                = juce::jmax(b < 200 ? peakFirstHalf : peakSecondHalf, s.peak);
        }
        check(finite, "torture: 400 blocks of parameter storms + reordering stay finite");

        /* Random automation legitimately dials in enormous gain (in_gain +24,
           sat drive +36, comp makeup +24, limiter gain +24, out_gain +24,
           with the limiter itself randomly bypassed), so a musical ceiling is
           the wrong invariant here — the real question is whether energy
           *diverges*. Feedback runaway grows without bound; gain staging does
           not. Compare the two halves of the run. */
        std::printf("      [torture peak %.1f | first half %.1f | second half %.1f]\n",
                    peak, peakFirstHalf, peakSecondHalf);
        check(peakSecondHalf < juce::jmax(1.0f, peakFirstHalf) * 50.0f,
              "torture: energy does not diverge over time (no runaway)");
    }

    // ---------------------------------------------------------------------
    // 8b. The limiter's product promise: "Ceiling caps out". Slam the chain
    //     with far more level than it can hold and verify the ceiling holds.
    // ---------------------------------------------------------------------
    {
        // Ceiling is a -2..0 dB parameter (it is a safety margin, not a
        // gain control), so ask only for values it can actually express and
        // verify against the value the parameter reports back.
        for (float requestedDb : { -0.3f, -1.0f, -2.0f })
        {
            VesperProcessor p;
            p.setPlayConfigDetails(2, 2, 48000.0, 512);
            p.prepareToPlay(48000.0, 512);

            for (int m = 0; m < vesper::numModules; ++m)   // limiter alone
                setParamRaw(p, vesper::moduleOnID(m), 0.0f);
            setParamRaw(p, "lim_on", 1.0f);
            setParamRaw(p, "lim_gain", 24.0f);             // hammer it
            setParamRaw(p, "lim_ceiling", requestedDb);
            setParamRaw(p, "lim_tp", 1.0f);
            setParamRaw(p, "in_gain", 18.0f);
            setParamRaw(p, "mix", 100.0f);
            pumpMessages(120);

            // read back what the parameter actually holds, so the assertion
            // tracks the real range rather than the value we hoped for
            const float ceilingDb = p.apvts.getRawParameterValue("lim_ceiling")->load();
            const auto s = render(p, 512, 120, 30);
            const float ceilingLin = juce::Decibels::decibelsToGain(ceilingDb);
            // small margin for the soft-clip knee + inter-sample behaviour
            const bool held = s.finite && s.peak <= ceilingLin * 1.10f;
            if (! held)
                std::printf("      [ceiling %.2f dB -> peak %.4f (limit %.4f)]\n",
                            ceilingDb, s.peak, ceilingLin);
            check(held, (juce::String("limiter: ceiling ") + juce::String(ceilingDb, 1)
                         + " dB holds against +42 dB of drive").toRawUTF8());
        }
    }

    // ---------------------------------------------------------------------
    // 9. Long-run stability — the D-023 runaway class, worst-case patch.
    // ---------------------------------------------------------------------
    {
        VesperProcessor p;
        p.setPlayConfigDetails(2, 2, 48000.0, 512);
        p.prepareToPlay(48000.0, 512);

        // everything feedback-capable pushed hard, simultaneously
        setParamRaw(p, "rev_on", 1.0f);
        setParamRaw(p, "rev_type", 3.0f);      // Bloom (FDN)
        setParamRaw(p, "rev_decay", 12.0f);
        setParamRaw(p, "rev_shimmer", 100.0f);
        setParamRaw(p, "rev_mix", 100.0f);
        setParamRaw(p, "dly_on", 1.0f);
        setParamRaw(p, "dly_feedback", 110.0f);
        setParamRaw(p, "dly_mix", 100.0f);
        setParamRaw(p, "flt_on", 1.0f);
        setParamRaw(p, "flt_res", 100.0f);
        pumpMessages(200);

        // 20 s of program, then 10 s of silence: the tail must decay, not grow
        const auto driven = render(p, 512, (int) (20.0 * 48000 / 512), 40);
        const auto tail   = render(p, 512, (int) (10.0 * 48000 / 512), 0, true);

        check(driven.finite && driven.peak < 16.0f,
              "stability: 20 s worst-case patch stays finite and bounded");
        check(tail.finite && tail.peak <= driven.peak * 1.05f,
              "stability: tail decays into silence, no self-oscillation growth");
    }

    // ---------------------------------------------------------------------
    // 10. Bypass + silence hygiene.
    // ---------------------------------------------------------------------
    {
        VesperProcessor p;
        p.setPlayConfigDetails(2, 2, 48000.0, 512);
        p.prepareToPlay(48000.0, 512);
        setParamRaw(p, "bypass", 1.0f);

        juce::AudioBuffer<float> buf(2, 512), ref(2, 512);
        juce::MidiBuffer midi;
        double phase = 0.0;
        fillProgram(buf, 48000.0, phase, 0);
        ref.makeCopyOf(buf);
        p.processBlock(buf, midi);

        float maxDiff = 0.0f;
        for (int i = 0; i < 512; ++i)
            maxDiff = juce::jmax(maxDiff, std::abs(buf.getSample(0, i) - ref.getSample(0, i)));
        check(maxDiff < 1.0e-6f, "bypass: passes the signal through untouched");

        // silence in -> silence out (denormal / self-noise hygiene)
        VesperProcessor q;
        q.setPlayConfigDetails(2, 2, 48000.0, 512);
        q.prepareToPlay(48000.0, 512);
        const auto sil = render(q, 512, 40, 0, true);
        check(sil.finite && sil.peak < 1.0e-6f,
              "silence: silent input produces silent, finite output");
    }

    // ---------------------------------------------------------------------
    // 11. Bus layouts: mono and stereo, with and without sidechain.
    // ---------------------------------------------------------------------
    {
        /* The processor declares three buses (main in, main out, sidechain in),
           so a layout must describe all of them — the sidechain disabled, or
           enabled for the keyed-dynamics path. */
        auto tryLayout = [] (const juce::AudioChannelSet& main,
                             const juce::AudioChannelSet& sidechain) -> bool
        {
            VesperProcessor p;
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add(main);
            l.inputBuses.add(sidechain);
            l.outputBuses.add(main);
            if (! p.checkBusesLayoutSupported(l)) return false;
            if (! p.setBusesLayout(l)) return false;

            p.prepareToPlay(48000.0, 512);
            const int chans = juce::jmax(main.size() + sidechain.size(), main.size());
            juce::AudioBuffer<float> buf(chans, 512);
            juce::MidiBuffer midi;
            double phase = 0.0;
            bool finite = true;
            for (int b = 0; b < 40; ++b)
            {
                fillProgram(buf, 48000.0, phase, b);
                p.processBlock(buf, midi);
                if (! analyse(buf).finite) finite = false;
            }
            p.releaseResources();
            return finite;
        };

        const auto mono   = juce::AudioChannelSet::mono();
        const auto stereo = juce::AudioChannelSet::stereo();
        const auto none   = juce::AudioChannelSet::disabled();

        check(tryLayout(mono, none),     "buses: mono in/out renders finite");
        check(tryLayout(stereo, none),   "buses: stereo in/out renders finite");
        check(tryLayout(stereo, stereo), "buses: stereo + stereo sidechain renders finite");
        check(tryLayout(stereo, mono),   "buses: stereo + mono sidechain renders finite");
    }

    // ---------------------------------------------------------------------
    // 12. Latency honesty: what we report must match what we delay.
    //     Impulse in, find it at the output; compare to getLatencySamples().
    // ---------------------------------------------------------------------
    {
        auto measureLatency = [] (bool armLinearPhase) -> std::pair<int,int>
        {
            VesperProcessor p;
            p.setPlayConfigDetails(2, 2, 48000.0, 512);
            p.prepareToPlay(48000.0, 512);
            // flat chain: everything off, unity mix, so only latency remains
            for (int m = 0; m < vesper::numModules; ++m)
                setParamRaw(p, vesper::moduleOnID(m), 0.0f);
            setParamRaw(p, "mix", 100.0f);
            if (armLinearPhase)
            {
                setParamRaw(p, "eq_on", 1.0f);
                setParamRaw(p, "eq_lp", 1.0f);
            }
            pumpMessages(400);
            // one silent block first: latency is published from processBlock,
            // and it also flushes any startup transient out of the delays
            const int reported = latencyAfterOneBlock(p);
            p.reset();

            juce::AudioBuffer<float> buf(2, 512);
            juce::MidiBuffer midi;
            std::vector<float> out;
            for (int b = 0; b < 20; ++b)
            {
                buf.clear();
                if (b == 0) { buf.setSample(0, 0, 1.0f); buf.setSample(1, 0, 1.0f); }
                p.processBlock(buf, midi);
                for (int i = 0; i < 512; ++i) out.push_back(buf.getSample(0, i));
            }
            int peakIdx = 0;
            float peakVal = 0.0f;
            for (int i = 0; i < (int) out.size(); ++i)
                if (std::abs(out[(size_t) i]) > peakVal)
                { peakVal = std::abs(out[(size_t) i]); peakIdx = i; }
            return { reported, peakIdx };
        };

        const auto flat = measureLatency(false);
        const auto lp   = measureLatency(true);
        check(std::abs(flat.first - flat.second) <= 2,
              "latency: flat chain reports exactly the delay it applies");
        check(std::abs(lp.first - lp.second) <= 2,
              "latency: linear-phase EQ reports exactly the delay it applies");
    }

    // ---------------------------------------------------------------------
    // 13. Extremes: all parameters at minimum, then all at maximum.
    // ---------------------------------------------------------------------
    {
        for (int pass = 0; pass < 2; ++pass)
        {
            VesperProcessor p;
            p.setPlayConfigDetails(2, 2, 48000.0, 512);
            p.prepareToPlay(48000.0, 512);
            for (auto* prm : p.getParameters())
                prm->setValueNotifyingHost(pass == 0 ? 0.0f : 1.0f);
            pumpMessages(300);
            const auto s = render(p, 512, 60, 15);
            check(s.finite, pass == 0 ? "extremes: every parameter at minimum stays finite"
                                      : "extremes: every parameter at maximum stays finite");
        }
    }

    // ---------------------------------------------------------------------
    // 14. Re-prepare churn: hosts change rate/blocksize mid-session.
    // ---------------------------------------------------------------------
    {
        VesperProcessor p;
        bool ok = true;
        for (int i = 0; i < 6; ++i)
        {
            const double fs = (i % 2 == 0) ? 44100.0 : 96000.0;
            const int bs = (i % 3 == 0) ? 64 : (i % 3 == 1) ? 512 : 1024;
            p.setPlayConfigDetails(2, 2, fs, bs);
            p.prepareToPlay(fs, bs);
            if (! render(p, bs, 30, 8).finite) ok = false;
            p.releaseResources();
        }
        check(ok, "re-prepare: repeated rate/block changes stay finite");
    }

    // ---------------------------------------------------------------------
    // 15. Editor layout (D-044): the fixed reference canvas must map fully
    //     inside whatever bounds the host hands us, at ANY aspect ratio.
    //     Verified through the transform actually applied to the canvas,
    //     rather than by inspecting pixels.
    // ---------------------------------------------------------------------
    {
        VesperProcessor p;
        p.setPlayConfigDetails(2, 2, 48000.0, 512);
        p.prepareToPlay(48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        bool ok = (ed != nullptr);
        juce::StringArray bad;

        if (ed != nullptr)
        {
            struct Case { int w, h; const char* label; };
            for (auto c : { Case{ 1080, 680, "reference" },
                            Case{  810, 510, "min" },
                            Case{ 2160, 1360, "max" },
                            Case{ 1200,  600, "wide (host squashed)" },
                            Case{  820,  760, "tall (host stretched)" },
                            Case{ 1018,  639, "standalone minus notice" } })
            {
                ed->setSize(c.w, c.h);
                auto* canvas = ed->getChildComponent(0);
                if (canvas == nullptr) { ok = false; bad.add("no canvas"); break; }

                // where does the reference canvas actually land on screen?
                const auto mapped = canvas->getBounds().toFloat()
                                        .transformedBy(canvas->getTransform());
                const juce::Rectangle<float> host(0.0f, 0.0f, (float) c.w, (float) c.h);

                const bool inside  = host.expanded(1.0f).contains(mapped);
                const bool fills   = mapped.getWidth()  >= host.getWidth()  - 1.5f
                                  || mapped.getHeight() >= host.getHeight() - 1.5f;
                const bool centred = std::abs((mapped.getX() + mapped.getRight())
                                              - host.getWidth()) < 2.0f
                                  && std::abs((mapped.getY() + mapped.getBottom())
                                              - host.getHeight()) < 2.0f;
                if (! (inside && fills && centred))
                {
                    ok = false;
                    bad.add(juce::String(c.label) + " " + juce::String(c.w) + "x"
                            + juce::String(c.h) + " -> canvas ("
                            + juce::String(mapped.getX(), 1) + "," + juce::String(mapped.getY(), 1)
                            + " " + juce::String(mapped.getWidth(), 1) + "x"
                            + juce::String(mapped.getHeight(), 1) + ")"
                            + (inside ? "" : " OVERFLOWS")
                            + (fills ? "" : " UNDERFILLS")
                            + (centred ? "" : " OFF-CENTRE"));
                }
            }
        }
        for (int i = 0; i < juce::jmin(6, bad.size()); ++i)
            std::printf("      %s\n", bad[i].toRawUTF8());
        check(ok, "editor: reference canvas fits, fills and centres at every aspect");
    }

    // ---------------------------------------------------------------------
    // 16. Numerical precision (D-045): the modules keep double state but hand
    //     float between each other. Does that interchange cost anything?
    //     A *linear* chain is scale-invariant in exact arithmetic, so running
    //     the same signal 100 dB apart and compensating must give the same
    //     answer. Any numerical noise floor shows up as deviation at the
    //     quiet level. This is the evidence behind the double-precision
    //     decision — not an opinion about float.
    // ---------------------------------------------------------------------
    {
        auto renderLinearChain = [] (float amplitude, std::vector<float>& out)
        {
            VesperProcessor p;
            p.setPlayConfigDetails(2, 2, 48000.0, 512);
            p.prepareToPlay(48000.0, 512);
            for (int m = 0; m < vesper::numModules; ++m)   // only linear stages
                setParamRaw(p, vesper::moduleOnID(m), 0.0f);
            setParamRaw(p, "eq_on", 1.0f);
            setParamRaw(p, "eq_b2_on", 1.0f);
            setParamRaw(p, "eq_b2_freq", 1000.0f);
            setParamRaw(p, "eq_b2_gain", 9.0f);
            setParamRaw(p, "eq_b2_q", 2.0f);
            setParamRaw(p, "eq_b4_on", 1.0f);
            setParamRaw(p, "eq_b4_freq", 4000.0f);
            setParamRaw(p, "eq_b4_gain", -7.0f);
            setParamRaw(p, "flt_on", 1.0f);
            setParamRaw(p, "flt_type", 0.0f);              // clean SVF
            setParamRaw(p, "flt_cutoff", 6000.0f);
            setParamRaw(p, "flt_res", 20.0f);
            setParamRaw(p, "flt_drive", 0.0f);             // no nonlinearity
            setParamRaw(p, "mix", 100.0f);
            pumpMessages(150);
            p.reset();

            juce::AudioBuffer<float> buf(2, 512);
            juce::MidiBuffer midi;
            double phase = 0.0;
            const double inc = juce::MathConstants<double>::twoPi * 997.0 / 48000.0;
            out.clear();
            for (int b = 0; b < 60; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float v = amplitude * (float) std::sin(phase);
                    phase += inc;
                    buf.setSample(0, i, v);
                    buf.setSample(1, i, v);
                }
                p.processBlock(buf, midi);
                if (b >= 20) // discard smoothing ramps
                    for (int i = 0; i < 512; ++i) out.push_back(buf.getSample(0, i));
            }
        };

        std::vector<float> loud, quiet;
        renderLinearChain(0.5f,     loud);    // ~ -6 dBFS
        renderLinearChain(0.5e-5f,  quiet);   // ~ -106 dBFS (100 dB quieter)

        // compensate and measure the error relative to the quiet signal
        double sigSq = 0.0, errSq = 0.0;
        const size_t n = juce::jmin(loud.size(), quiet.size());
        for (size_t i = 0; i < n; ++i)
        {
            const double expected = (double) loud[i] * 1.0e-5;
            const double err = (double) quiet[i] - expected;
            sigSq += expected * expected;
            errSq += err * err;
        }
        const double snrDb = 10.0 * std::log10(juce::jmax(1.0e-300, sigSq)
                                               / juce::jmax(1.0e-300, errSq));
        std::printf("      [linear-chain scale invariance over 100 dB: %.1f dB SNR]\n", snrDb);
        check(n > 0 && snrDb > 90.0,
              "precision: linear chain is scale-invariant over 100 dB (float "
              "interchange adds no audible floor)");
    }

    // ---------------------------------------------------------------------
    // 17. CPU budget (D-045): render offline and report the realtime factor
    //     for the whole chain and for each module in isolation. This is the
    //     regression tripwire — a module that suddenly costs 5x more shows
    //     up here rather than in a session.
    // ---------------------------------------------------------------------
    {
        constexpr double fs = 48000.0;
        constexpr int    bs = 512;
        constexpr double seconds = 4.0;
        const int blocks = (int) (seconds * fs / bs);

        auto timeConfig = [&] (const std::function<void(VesperProcessor&)>& configure)
        {
            VesperProcessor p;
            p.setPlayConfigDetails(2, 2, fs, bs);
            p.prepareToPlay(fs, bs);
            for (int m = 0; m < vesper::numModules; ++m)
                setParamRaw(p, vesper::moduleOnID(m), 0.0f);
            configure(p);
            pumpMessages(200);
            p.reset();

            juce::AudioBuffer<float> buf(2, bs);
            juce::MidiBuffer midi;
            double phase = 0.0;
            for (int b = 0; b < 10; ++b) { fillProgram(buf, fs, phase, b); p.processBlock(buf, midi); }

            const auto t0 = juce::Time::getHighResolutionTicks();
            for (int b = 0; b < blocks; ++b)
            {
                fillProgram(buf, fs, phase, b);
                p.processBlock(buf, midi);
            }
            const double elapsed = juce::Time::highResolutionTicksToSeconds(
                juce::Time::getHighResolutionTicks() - t0);
            return seconds / juce::jmax(1.0e-9, elapsed); // xRealtime
        };

        const double allOff = timeConfig([] (VesperProcessor&) {});
        const double whole  = timeConfig([] (VesperProcessor& p)
        {
            for (int m = 0; m < vesper::numModules; ++m)
                setParamRaw(p, vesper::moduleOnID(m), 1.0f);
        });

        std::printf("      [chain empty %.0fx realtime | all 13 modules %.0fx realtime]\n",
                    allOff, whole);
        for (int m = 0; m < vesper::numModules; ++m)
        {
            const double x = timeConfig([m] (VesperProcessor& p)
                                        { setParamRaw(p, vesper::moduleOnID(m), 1.0f); });
            std::printf("      [%-10s %6.0fx realtime]\n", vesper::moduleName(m), x);
        }

        /* Absolute realtime factors depend on what else the machine is doing
           — measured 151x/26x idle but 88x/14x with a game and a VM running,
           which would fail an absolute budget for no reason. A test that
           cries wolf is one you learn to ignore, so assert the *ratio*
           instead: how much the 13 modules cost relative to an empty chain
           measured in the same run. That cancels machine load almost exactly
           (5.8x idle vs 6.3x under 93% load). The absolute numbers are still
           reported, and a catastrophic floor still applies. */
        const double costRatio = allOff / juce::jmax(1.0e-9, whole);
        std::printf("      [chain costs %.1fx the empty baseline]\n", costRatio);
       #if JUCE_DEBUG
        std::printf("      [debug build: CPU budget reported, not asserted]\n");
       #else
        check(costRatio < 10.0,
              "cpu: 13 modules cost <10x an empty chain (load-invariant)");
        check(whole > 5.0, "cpu: full chain still comfortably faster than realtime");
       #endif
    }

    // ---------------------------------------------------------------------
    // 18. Startup budget (D-045): plugin instantiation + first prepare, and
    //     editor construction, both of which a host does on every load.
    // ---------------------------------------------------------------------
    {
        const auto t0 = juce::Time::getHighResolutionTicks();
        VesperProcessor p;
        p.setPlayConfigDetails(2, 2, 48000.0, 512);
        p.prepareToPlay(48000.0, 512);
        const double ctorMs = juce::Time::highResolutionTicksToSeconds(
            juce::Time::getHighResolutionTicks() - t0) * 1000.0;

        const auto t1 = juce::Time::getHighResolutionTicks();
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        const double editorMs = juce::Time::highResolutionTicksToSeconds(
            juce::Time::getHighResolutionTicks() - t1) * 1000.0;

        std::printf("      [instantiate+prepare %.1f ms | editor open %.1f ms]\n",
                    ctorMs, editorMs);
        check(ctorMs < 500.0, "startup: instantiate + prepare under 500 ms");
        check(ed != nullptr && editorMs < 500.0, "startup: editor opens under 500 ms");
    }

    // ---------------------------------------------------------------------
    // 19. Accessibility (D-046): every focusable control must expose a name,
    //     and every value-bearing control must announce a value that carries
    //     its unit — what a screen-reader user hears has to match what a
    //     sighted user reads off the arc.
    // ---------------------------------------------------------------------
    {
        VesperProcessor p;
        p.setPlayConfigDetails(2, 2, 48000.0, 512);
        p.prepareToPlay(48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());

        int sliders = 0, unnamed = 0, valueless = 0, focusable = 0;
        juce::StringArray bad;

        std::function<void(juce::Component*)> walk = [&] (juce::Component* c)
        {
            if (c == nullptr) return;
            if (auto* s = dynamic_cast<juce::Slider*>(c))
            {
                ++sliders;
                if (s->getWantsKeyboardFocus()) ++focusable;
                if (s->getTitle().isEmpty()) { ++unnamed; bad.add("slider with no title"); }
                const auto txt = s->getTextFromValue(s->getValue());
                if (txt.trim().isEmpty())
                {
                    ++valueless;
                    bad.add(s->getTitle() + " announces no value");
                }
            }
            for (int i = 0; i < c->getNumChildComponents(); ++i)
                walk(c->getChildComponent(i));
        };
        if (ed != nullptr) walk(ed.get());

        std::printf("      [%d sliders | %d focusable | %d unnamed | %d valueless]\n",
                    sliders, focusable, unnamed, valueless);
        for (int i = 0; i < juce::jmin(5, bad.size()); ++i)
            std::printf("      %s\n", bad[i].toRawUTF8());

        check(sliders > 20, "a11y: editor exposes its controls for traversal");
        check(unnamed == 0, "a11y: every slider has an accessible name");
        check(valueless == 0, "a11y: every slider announces a value");
        check(focusable == sliders, "a11y: every slider is keyboard-focusable");

        // and the announced text must carry the unit the arc draws
        VesperProcessor q;
        q.setPlayConfigDetails(2, 2, 48000.0, 512);
        q.prepareToPlay(48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed2(q.createEditor());
        bool unitOk = false;
        std::function<void(juce::Component*)> findDrive = [&] (juce::Component* c)
        {
            if (c == nullptr || unitOk) return;
            if (auto* s = dynamic_cast<juce::Slider*>(c))
                if (s->getTitle() == "DRIVE")
                    unitOk = s->getTextFromValue(s->getValue()).containsIgnoreCase("dB");
            for (int i = 0; i < c->getNumChildComponents() && ! unitOk; ++i)
                findDrive(c->getChildComponent(i));
        };
        if (ed2 != nullptr) findDrive(ed2.get());
        check(unitOk, "a11y: announced value carries its unit (DRIVE reads in dB)");
    }

    // ---------------------------------------------------------------------
    // 20. Preset LOUDNESS discipline (D-047). "No runaway" is not the same
    //     as "controlled": peak < 4.0 is +12 dBFS, which is painful. Every
    //     preset must stay at or under unity and must not shove loudness up
    //     relative to what went in.
    // ---------------------------------------------------------------------
    {
        VesperProcessor p;
        p.setPlayConfigDetails(2, 2, 48000.0, 512);
        p.prepareToPlay(48000.0, 512);

        // reference: the same program material with the plugin bypassed
        double inRms = 0.0, inPeak = 0.0;
        {
            juce::AudioBuffer<float> buf(2, 512);
            double phase = 0.0;
            double sum = 0.0; int n = 0;
            for (int b = 0; b < 90; ++b)
            {
                fillProgram(buf, 48000.0, phase, b);
                if (b < 20) continue;
                for (int i = 0; i < 512; ++i)
                {
                    const double v = buf.getSample(0, i);
                    sum += v * v; ++n;
                    inPeak = juce::jmax(inPeak, std::abs(v));
                }
            }
            inRms = std::sqrt(sum / juce::jmax(1, n));
        }
        const double inRmsDb = juce::Decibels::gainToDecibels(inRms);

        int hotPeak = 0, hotGain = 0;
        juce::StringArray report;
        for (const auto& info : p.presets.getAll())
        {
            p.presets.loadPreset(info);
            pumpMessages(60);
            p.reset();

            juce::AudioBuffer<float> buf(2, 512);
            juce::MidiBuffer midi;
            double phase = 0.0, sum = 0.0;
            int n = 0; float peak = 0.0f;
            for (int b = 0; b < 90; ++b)
            {
                fillProgram(buf, 48000.0, phase, b);
                p.processBlock(buf, midi);
                if (b < 20) continue;               // discard smoothing ramps
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                    {
                        const double v = buf.getSample(ch, i);
                        sum += v * v; ++n;
                        peak = juce::jmax(peak, (float) std::abs(v));
                    }
            }
            const double rms   = std::sqrt(sum / juce::jmax(1, n));
            const double peakDb = juce::Decibels::gainToDecibels(peak);
            const double gainDb = juce::Decibels::gainToDecibels(rms) - inRmsDb;

            const bool peakBad = peakDb > 0.5;      // above unity = clipping
            const bool gainBad = gainDb > 6.0;      // >6 dB louder than source
            if (peakBad) ++hotPeak;
            if (gainBad) ++hotGain;
            if (peakBad || gainBad)
                report.add(juce::String(info.name).paddedRight(' ', 22)
                           + " peak " + juce::String(peakDb, 1) + " dBFS"
                           + "  gain " + juce::String(gainDb, 1) + " dB");
        }
        std::printf("      [input reference: %.1f dBFS RMS, %.1f dBFS peak]\n",
                    inRmsDb, juce::Decibels::gainToDecibels(inPeak));
        for (int i = 0; i < report.size(); ++i)
            std::printf("      %s\n", report[i].toRawUTF8());

        check(hotPeak == 0, "loudness: no preset exceeds 0 dBFS peak");
        check(hotGain == 0, "loudness: no preset is more than +6 dB louder than its source");

        /* D-047 — and the other half of "controlled": a preset must audibly
           DO something. It was not obvious in use that the
           effects were working, so measure each preset's difference from the
           dry signal and fail any that barely moves it. */
        int inert = 0;
        juce::StringArray quiet;
        for (const auto& info : p.presets.getAll())
        {
            p.presets.loadPreset(info);
            pumpMessages(60);
            p.reset();

            juce::AudioBuffer<float> buf(2, 512), dry(2, 512);
            juce::MidiBuffer midi;
            double phase = 0.0, diffSq = 0.0, refSq = 0.0;
            for (int b = 0; b < 70; ++b)
            {
                fillProgram(buf, 48000.0, phase, b);
                dry.makeCopyOf(buf);
                p.processBlock(buf, midi);
                if (b < 20) continue;
                for (int i = 0; i < 512; ++i)
                {
                    const double d = (double) buf.getSample(0, i) - dry.getSample(0, i);
                    diffSq += d * d;
                    refSq  += (double) dry.getSample(0, i) * dry.getSample(0, i);
                }
            }
            // difference relative to the source, in dB (0 dB = as loud as the
            // source itself; -40 dB = essentially inaudible)
            const double diffDb = 10.0 * std::log10(juce::jmax(1.0e-300, diffSq)
                                                    / juce::jmax(1.0e-300, refSq));
            if (diffDb < -24.0)
            {
                ++inert;
                quiet.add(juce::String(info.name).paddedRight(' ', 24)
                          + juce::String(diffDb, 1) + " dB from dry");
            }
        }
        for (int i = 0; i < quiet.size(); ++i)
            std::printf("      %s\n", quiet[i].toRawUTF8());
        check(inert == 0, "presets: every preset audibly changes the signal");
    }

    //----------------------------------------------------------------------
    std::printf("\n%s (%d checks, %d failure%s)\n",
                failures == 0 ? "ALL INTEGRATION TESTS PASSED" : "INTEGRATION TESTS FAILED",
                checks, failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
