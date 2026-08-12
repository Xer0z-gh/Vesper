#pragma once
/*  ConvReverb.h — convolution reverb (D-029) for the Spring and Cathedral
    spaces. IRs are generated procedurally (no sample payload to license or
    ship), then handed to juce::dsp::Convolution, whose loadImpulseResponse
    is thread-safe and loads on its own background thread.

    Contract: regenerate() ALLOCATES and is called from the message thread
    only (the processor triggers it when the reverb params change while a
    convolution space is active). process() is audio-thread-safe and uses
    whatever IR is currently loaded. Predelay, damping, width, low-cut and
    wet/dry are applied around the convolution here. */

#include "Core.h"
#include <juce_dsp/juce_dsp.h>

namespace vesper::dsp {

class ConvReverb
{
public:
    void prepare(const Context& ctx)
    {
        fs = ctx.sampleRate;
        juce::dsp::ProcessSpec spec { fs, (juce::uint32) ctx.maxBlockSize, 2 };
        conv.prepare(spec);
        for (auto& d : pre) d.prepare(fs, 0.25);
        for (int ch = 0; ch < 2; ++ch)
        {
            lo[(size_t) ch].setCoefficients(Biquad::Type::highPass, fs, 90.0, 0.71, 0.0);
            damp[(size_t) ch].setCutoff(9000.0, fs);
        }
        shimL.prepare(fs); shimR.prepare(fs);
        shimL.setRatio(2.0); shimR.setRatio(2.0);
        mixSm.reset(fs, 0.05); widthSm.reset(fs, 0.05); shimSm.reset(fs, 0.05);
        wet.setSize(2, ctx.maxBlockSize);
        reset();
    }

    /* Message-thread only: build the IR and hand it to the convolution. */
    void regenerate(int type, float decaySec, float sizePct)
    {
        const double len = juce::jlimit(0.15, 8.0,
            (double) decaySec * (type == spring ? 0.8 : 1.4)
                              * (0.6 + 0.008 * sizePct));
        const int n = (int) (len * fs);
        juce::AudioBuffer<float> ir(2, juce::jmax(64, n));
        buildIR(ir, type, len, sizePct);
        conv.loadImpulseResponse(std::move(ir), fs,
                                 juce::dsp::Convolution::Stereo::yes,
                                 juce::dsp::Convolution::Trim::no,
                                 juce::dsp::Convolution::Normalise::yes);
    }

    /* Message thread: load a user impulse response from disk (D-038).
       Returns false if the file can't be read — the caller keeps the old IR
       and reports it, rather than silently going quiet. */
    bool loadUserIR(const juce::File& f)
    {
        if (! f.existsAsFile()) return false;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(f));
        if (rd == nullptr || rd->lengthInSamples < 4) return false;

        // cap absurd IRs: 10 s is longer than any usable space at this scale
        const int len = (int) juce::jmin(rd->lengthInSamples,
                                         (juce::int64) (rd->sampleRate * 10.0));
        juce::AudioBuffer<float> ir((int) juce::jmin<juce::uint32>(2, rd->numChannels), len);
        if (! rd->read(&ir, 0, len, 0, true, ir.getNumChannels() > 1))
            return false;

        conv.loadImpulseResponse(std::move(ir), rd->sampleRate, // JUCE resamples
                                 juce::dsp::Convolution::Stereo::yes,
                                 juce::dsp::Convolution::Trim::yes,
                                 juce::dsp::Convolution::Normalise::yes);
        return true;
    }

    void setParams(float predelayMs, float dampPct, float locutHz,
                   float widthPct, float mixPct) noexcept
    {
        preSamples = juce::jlimit(0.0, fs * 0.24, predelayMs * 0.001 * fs);
        const double dampHz = juce::jmap((double) dampPct * 0.01, 16000.0, 1600.0);
        if (std::abs(dampHz - lastDamp) > 1.0)
        {
            lastDamp = dampHz;
            for (auto& d : damp) d.setCutoff(dampHz, fs);
        }
        if (std::abs(locutHz - lastLo) > 0.5f)
        {
            lastLo = locutHz;
            for (auto& f : lo) f.setCoefficients(Biquad::Type::highPass, fs, locutHz, 0.71, 0.0);
        }
        widthSm.setTargetValue(widthPct * 0.01f);
        mixSm.setTargetValue(mixPct * 0.01f);
    }

    /* Shimmer for the convolution spaces (D-038). Feedforward by design:
       the octave-up voice is summed into the convolution INPUT, so the IR
       reverberates it without any loop around the output — the runaway that
       bit the FDN tank (D-023) is structurally impossible here. */
    void setShimmer(float shimmerPct) noexcept
    {
        shimSm.setTargetValue(juce::jlimit(0.0f, 1.0f, shimmerPct * 0.01f));
    }

    void process(juce::AudioBuffer<float>& buf)
    {
        const int nn = buf.getNumSamples();
        auto* l = buf.getWritePointer(0);
        auto* r = buf.getWritePointer(1);

        // predelay into the wet scratch
        wet.setSize(2, nn, false, false, true);
        auto* wl = wet.getWritePointer(0);
        auto* wr = wet.getWritePointer(1);
        for (int i = 0; i < nn; ++i)
        {
            pre[0].write(l[i]); pre[1].write(r[i]);
            float a = pre[0].readHermite(juce::jmax(1.0, preSamples));
            float b = pre[1].readHermite(juce::jmax(1.0, preSamples));

            // octave-up voice folded into the convolution input; energy is
            // traded against the fundamental so loud shimmer never adds gain
            const float s = shimSm.getNextValue();
            if (s > 0.001f)
            {
                const float su = shimL.process(a), sv = shimR.process(b);
                const float dryG = 1.0f - 0.5f * s;
                a = a * dryG + su * s * 0.7f;
                b = b * dryG + sv * s * 0.7f;
            }
            wl[i] = a;
            wr[i] = b;
        }

        juce::dsp::AudioBlock<float> block(wet);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        conv.process(ctx);

        for (int i = 0; i < nn; ++i)
        {
            float a = (float) damp[0].process(lo[0].process(wl[i]));
            float b = (float) damp[1].process(lo[1].process(wr[i]));
            const float w   = widthSm.getNextValue();
            const float mid = (a + b) * 0.5f, side = (a - b) * 0.5f * w;
            a = mid + side; b = mid - side;
            const float mix = mixSm.getNextValue();
            l[i] = equalPowerMix(l[i], a, mix);
            r[i] = equalPowerMix(r[i], b, mix);
        }
    }

    void reset()
    {
        conv.reset();
        for (auto& d : pre) d.clear();
        for (auto& f : lo) f.reset();
        for (auto& d : damp) d.reset();
        shimL.reset(); shimR.reset();
    }

    enum Space { spring = 0, cathedral, reverse, userIR };

private:
    /* Procedural IRs: exponential-decay decorrelated noise, plus a few early
       reflections; Spring adds a dispersive metallic chirp, Cathedral a
       slow high-frequency darkening. */
    void buildIR(juce::AudioBuffer<float>& ir, int type, double lenSec, float sizePct)
    {
        const int n = ir.getNumSamples();
        auto& rng = juce::Random::getSystemRandom();
        const double t60 = lenSec;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = ir.getWritePointer(ch);
            // per-channel decorrelated noise tail
            double lpState = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double tt = (double) i / fs;
                const double env = std::pow(10.0, -3.0 * tt / t60);
                double s = (rng.nextDouble() * 2.0 - 1.0) * env;
                if (type == cathedral || type == reverse) // darken progressively
                {
                    const double a = juce::jmap(juce::jlimit(0.0, 1.0, tt / t60), 0.35, 0.95);
                    lpState = s + a * (lpState - s);
                    s = lpState;
                }
                d[i] = (float) s;
            }
            if (type == reverse) // swell: reverse the decay so it grows into the hit
            {
                std::reverse(d, d + n);
                continue; // no early reflections / pip on a reverse tail
            }
            // early reflections
            static const double erMs[] = { 7.0, 13.0, 19.0, 29.0, 41.0, 57.0 };
            const double scale = 0.7 + 0.009 * sizePct;
            for (int e = 0; e < 6; ++e)
            {
                const int idx = (int) (erMs[e] * scale * 0.001 * fs) + (ch * 3);
                if (idx < n) d[idx] += (float) (0.6 / (e + 1)) * (ch == 0 ? 1.0f : -1.0f);
            }
            if (type == spring) // dispersive metallic chirp near the front
            {
                const int clen = juce::jmin(n, (int) (0.09 * fs));
                for (int i = 0; i < clen; ++i)
                {
                    const double tt = (double) i / fs;
                    const double f  = 1400.0 + 5200.0 * tt / 0.09; // rising chirp
                    const double env = std::exp(-tt * 22.0);
                    d[i] += (float) (0.5 * env * std::sin(juce::MathConstants<double>::twoPi * f * tt));
                }
            }
            d[0] += ch == 0 ? 0.9f : 0.0f; // direct pip anchors the convolution
        }
    }

    double fs = 48000.0, preSamples = 0.0, lastDamp = -1.0;
    float  lastLo = -1.0f;
    juce::dsp::Convolution conv;
    std::array<DelayLine, 2> pre;
    std::array<Biquad, 2> lo;
    std::array<OnePole, 2> damp;
    juce::AudioBuffer<float> wet { 2, 4096 };
    OctaveShifter shimL, shimR;
    SmoothLin mixSm, widthSm, shimSm;
};

} // namespace vesper::dsp
