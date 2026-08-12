#pragma once
/*  Saturation.h — 15 nonlinear color models with per-module oversampling.
    Each algorithm has its own transfer curve / harmonic fingerprint rather
    than one waveshaper with different names. Oversampled 1x–16x via
    polyphase IIR halfband (juce::dsp::Oversampling), DC-blocked at the end. */

#include "Core.h"

namespace vesper::dsp {

class Saturator
{
public:
    enum Algo { tube = 0, tape, transformer, console, softAnalog, warm, exciter,
                triode, pentode, germanium, silicon, foldback, wavefold,
                bitcrush, downsample, softClip, hardClip, numAlgos };

    void prepare(const Context& ctx)
    {
        fs = ctx.sampleRate;
        for (int f = 0; f < 4; ++f) // 2x, 4x, 8x, 16x
        {
            os[(size_t) f] = std::make_unique<juce::dsp::Oversampling<float>>(
                2, (size_t) f + 1,
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
            os[(size_t) f]->initProcessing((size_t) ctx.maxBlockSize);
        }
        for (auto& d : dcBlock) d.setCutoff(8.0, fs);
        driveSm.reset(fs, 0.02);  mixSm.reset(fs, 0.02);  trimSm.reset(fs, 0.02);
        for (auto& t : tilt) { t[0] = {}; t[1] = {}; }
        updateTone(true);
        reset();
    }

    void setAlgo(int a) noexcept        { algo = juce::jlimit(0, numAlgos - 1, a); }
    void setDrive(float db) noexcept    { driveSm.setTargetValue(dbToGain(db)); }
    void setBias(float b) noexcept      { bias = b * 0.3f; }
    void setTone(float t) noexcept      { tone = t; updateTone(false); }
    void setMix(float m) noexcept       { mixSm.setTargetValue(m); }
    void setTrim(float db) noexcept     { trimSm.setTargetValue(dbToGain(db)); }
    void setOversample(int factorLog2) noexcept { osIndex = juce::jlimit(0, 4, factorLog2); }

    int latencySamples() const noexcept
    {
        return osIndex == 0 ? 0 : (int) os[(size_t) osIndex - 1]->getLatencyInSamples();
    }

    void process(juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        dryScratch.makeCopyOf(buf, true);

        // Pre-tone tilt (post-tilt inverse restores overall balance, so "tone"
        // shifts *what gets saturated*, not the static EQ).
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer(ch);
            for (int i = 0; i < n; ++i)
                d[i] = (float) tilt[(size_t) ch][0].process(d[i]);
        }

        juce::dsp::AudioBlock<float> block(buf);
        if (osIndex > 0)
        {
            auto& over = *os[(size_t) osIndex - 1];
            auto up = over.processSamplesUp(block);
            shapeBlock(up);
            over.processSamplesDown(block);
        }
        else
        {
            shapeBlock(block);
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d   = buf.getWritePointer(ch);
            auto* dry = dryScratch.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
            {
                float w = (float) tilt[(size_t) ch][1].process(d[i]);   // inverse tilt
                w = (float) dcBlockProcess(ch, w);
                const float mix  = ch == 0 ? mixSm.getNextValue() : mixSm.getCurrentValue();
                const float trim = ch == 0 ? trimSm.getNextValue() : trimSm.getCurrentValue();
                d[i] = (dry[i] + (w * trim - dry[i]) * mix);
            }
        }
    }

    void reset()
    {
        for (auto& o : os) if (o) o->reset();
        for (auto& d : dcBlock) d.reset();
        for (auto& t : tilt) { t[0].reset(); t[1].reset(); }
        holdL = holdR = 0.0f; holdCount = 0;
        dcStateL = dcStateR = 0.0;
    }

    /* Static transfer curve — the card plot calls this with the live drive
       already folded into x, plus the bias, so the picture and the audio
       run the same shaper (D-066). */
    static float shapeStatic(int algo, float x, float bias = 0.0f) noexcept
    {
        return shapeSample(algo, x, bias);
    }

private:
    void updateTone(bool force)
    {
        juce::ignoreUnused(force);
        const double g = tone * 4.5;
        for (int ch = 0; ch < 2; ++ch)
        {
            tilt[(size_t) ch][0].setCoefficients(Biquad::Type::highShelf, fs, 650.0, 0.4,  g);
            tilt[(size_t) ch][1].setCoefficients(Biquad::Type::highShelf, fs, 650.0, 0.4, -g);
        }
    }

    double dcBlockProcess(int ch, float x) noexcept
    {
        auto& state = ch == 0 ? dcStateL : dcStateR;
        const double lp = dcBlock[(size_t) ch].process(x);
        state = x - lp;
        return state;
    }

    void shapeBlock(juce::dsp::AudioBlock<float>& block)
    {
        const int  n      = (int) block.getNumSamples();
        const auto factor = osIndex == 0 ? 1 : (1 << osIndex);
        auto* l = block.getChannelPointer(0);
        auto* r = block.getNumChannels() > 1 ? block.getChannelPointer(1) : l;

        for (int i = 0; i < n; ++i)
        {
            // Advance drive smoothing once per *base-rate* sample.
            const float drive = (i % factor == 0) ? driveSm.getNextValue()
                                                  : driveSm.getCurrentValue();
            const float norm = 1.0f / juce::jmax(0.35f, std::sqrt(drive)); // loudness-compensated
            if (algo == bitcrush || algo == downsample)
            {
                processDigital(l[i], r[i], drive, i);
            }
            else
            {
                l[i] = shapeSample(algo, l[i] * drive + bias, bias) * norm;
                r[i] = shapeSample(algo, r[i] * drive + bias, bias) * norm;
            }
        }
    }

    void processDigital(float& xl, float& xr, float drive, int i)
    {
        const float amt = juce::jlimit(0.0f, 1.0f, gainToDb(drive) / 36.0f);
        if (algo == bitcrush)
        {
            const float bits  = juce::jmap(amt, 16.0f, 3.0f);
            const float steps = std::pow(2.0f, bits);
            xl = std::round(xl * steps) / steps;
            xr = std::round(xr * steps) / steps;
        }
        else // downsample: sample & hold
        {
            const int hold = 1 + (int) (amt * 31.0f) * (osIndex == 0 ? 1 : (1 << osIndex));
            if (holdCount <= 0) { holdL = xl; holdR = xr; holdCount = hold; }
            --holdCount;
            juce::ignoreUnused(i);
            xl = holdL; xr = holdR;
        }
    }

    static float shapeSample(int algo, float x, float biasIn) noexcept
    {
        switch (algo)
        {
            case tube:        // asymmetric triode-ish: warm even harmonics
                return fastTanh(x) + 0.12f * x * std::abs(x) - 0.12f * biasIn * std::abs(biasIn);
            case tape:        // gentle arctan knee, compresses peaks smoothly
                return (2.0f / juce::MathConstants<float>::pi) * std::atan(1.4f * x);
            case transformer: // odd-heavy with a hard-ish core saturation
                return fastTanh(x) * 0.8f + fastTanh(3.0f * x) * 0.2f;
            case console:     // very subtle cubic — "glue", almost clean
                return x <= -1.5f ? -1.0f : x >= 1.5f ? 1.0f : x - (x * x * x) / 6.75f;
            case softAnalog:  // sine shaper below fold point
                return std::sin(juce::jlimit(-juce::MathConstants<float>::halfPi,
                                             juce::MathConstants<float>::halfPi, x));
            case warm:        // dominant 2nd harmonic
                return fastTanh(x + 0.28f * x * x) - fastTanh(0.28f * biasIn * biasIn);
            case exciter:     // fast, bright odd harmonics (pair with tone > 0)
                return fastTanh(2.2f * x) * 0.72f + 0.28f * x;
            case triode:      // stronger asymmetry than 'tube'
                return x >= 0.0f ? fastTanh(x) : fastTanh(0.68f * x) * 1.15f;
            case pentode:     // harder knee, later break-up
                return (1.0f - std::exp(-std::abs(1.6f * x))) * (x < 0.0f ? -0.92f : 1.0f);
            case germanium:   // lazy diode: soft crossover region, asym clip
                return fastTanh(1.3f * x + 0.5f * x * x * (x > 0.0f ? 0.3f : -0.15f));
            case silicon:     // tight, aggressive diode clipping
                return fastTanh(2.6f * x) * 0.92f;
            case foldback:    // triangle foldback past ±1
            {
                float v = x;
                for (int k = 0; k < 4 && std::abs(v) > 1.0f; ++k)
                    v = v > 1.0f ? 2.0f - v : v < -1.0f ? -2.0f - v : v;
                return juce::jlimit(-1.0f, 1.0f, v);
            }
            case wavefold:    // smooth sinusoidal folding
                return std::sin(x * 1.8f) * 0.9f + 0.1f * fastTanh(x);
            case softClip:    // cubic soft clipper, transparent until pushed
                return x <= -1.0f ? -0.6667f : x >= 1.0f ? 0.6667f : x - (x * x * x) / 3.0f;
            case hardClip:    // brutal digital ceiling
                return juce::jlimit(-1.0f, 1.0f, x);
            default:
                return fastTanh(x);
        }
    }

    double fs = 48000.0;
    int    algo = 0, osIndex = 1;
    float  bias = 0.0f, tone = 0.0f;
    SmoothLin driveSm, mixSm, trimSm;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 4> os;
    std::array<std::array<Biquad, 2>, 2> tilt; // [channel][pre/post]
    std::array<OnePole, 2> dcBlock;
    double dcStateL = 0.0, dcStateR = 0.0;
    float  holdL = 0.0f, holdR = 0.0f;
    int    holdCount = 0;
    juce::AudioBuffer<float> dryScratch { 2, 4096 };
};

// ---------------------------------------------------------------------------
/* Soft/hard clipper used inside the limiter output stage. */
inline float softClipTo(float x, float ceiling) noexcept
{
    return ceiling * fastTanh(x / juce::jmax(1.0e-6f, ceiling));
}

} // namespace vesper::dsp
