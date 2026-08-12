/*  VesperTests — DSP correctness and safety suite.
    Plain console runner (no framework dependency): each check prints
    PASS/FAIL; a non-zero exit code fails CI. */

#include <juce_dsp/juce_dsp.h>
#include "dsp/Core.h"
#include "dsp/Saturation.h"
#include "dsp/Dynamics.h"
#include "dsp/DynamicsPlus.h"
#include "dsp/EQFilter.h"
#include "dsp/LinearPhaseEQ.h"
#include "dsp/MatchEQ.h"
#include "dsp/TimeFX.h"
#include "dsp/ConvReverb.h"
#include "dsp/ModFX.h"
#include "dsp/PitchFX.h"
#include "core/Params.h"
#include "core/Creative.h"
#include "core/PresetManager.h"

#include <BinaryData.h>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <vector>

static int failures = 0;

static void check(bool condition, const char* name)
{
    std::printf("%s  %s\n", condition ? "PASS" : "FAIL", name);
    if (! condition) ++failures;
}

static bool isFiniteBuffer(const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite(b.getSample(ch, i))) return false;
    return true;
}

static float peakOf(const juce::AudioBuffer<float>& b)
{
    float p = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        p = juce::jmax(p, b.getMagnitude(ch, 0, b.getNumSamples()));
    return p;
}

static void fillSine(juce::AudioBuffer<float>& b, double freq, double fs, float amp)
{
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = amp * (float) std::sin(juce::MathConstants<double>::twoPi * freq * i / fs);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            b.setSample(ch, i, v);
    }
}

//==============================================================================
int main()
{
    constexpr double fs = 48000.0;
    const vesper::dsp::Context ctx { fs, 512, 2 };

    // ---- fast math: bounded, monotonic, continuous, tanh-close ----
    {
        bool ok = true;
        float prev = -1.0f;
        for (float x = -6.0f; x <= 6.0f; x += 0.001f)
        {
            const float y = vesper::dsp::fastTanh(x);
            const bool bounded = std::abs(y) <= 1.0f;
            const bool mono    = y >= prev - 1.0e-6f;
            const bool close   = std::abs(y - std::tanh(x)) < 0.025f;
            const bool cont    = std::abs(y - vesper::dsp::fastTanh(x - 0.001f)) < 0.01f;
            ok = ok && bounded && mono && close && cont;
            prev = y;
        }
        check(ok, "fastTanh bounded, monotonic, continuous, within 0.025 of tanh");
    }

    // ---- biquad response ----
    {
        vesper::dsp::Biquad bq;
        bq.setCoefficients(vesper::dsp::Biquad::Type::bell, fs, 1000.0, 1.0, 6.0);
        const double magAtCentre = bq.magnitudeAt(1000.0, fs);
        const double magFarAway  = bq.magnitudeAt(60.0, fs);
        check(std::abs(juce::Decibels::gainToDecibels(magAtCentre) - 6.0) < 0.1,
              "biquad bell +6 dB at centre");
        check(std::abs(juce::Decibels::gainToDecibels(magFarAway)) < 0.5,
              "biquad bell ~0 dB far from centre");
    }

    // ---- every saturation algorithm: bounded, finite, silence-safe ----
    {
        bool ok = true;
        for (int algo = 0; algo < vesper::dsp::Saturator::numAlgos; ++algo)
        {
            vesper::dsp::Saturator sat;
            sat.prepare(ctx);
            sat.setAlgo(algo);
            sat.setDrive(30.0f);
            sat.setMix(1.0f);
            sat.setOversample(1); // 2x

            juce::AudioBuffer<float> buf(2, 512);
            fillSine(buf, 220.0, fs, 0.9f);
            for (int block = 0; block < 20; ++block) sat.process(buf);
            ok = ok && isFiniteBuffer(buf) && peakOf(buf) < 4.0f;
        }
        check(ok, "all 17 saturation algorithms finite and bounded at max drive");

        vesper::dsp::Saturator sat;
        sat.prepare(ctx);
        juce::AudioBuffer<float> silent(2, 512);
        silent.clear();
        for (int block = 0; block < 50; ++block) sat.process(silent);
        check(peakOf(silent) < 1.0e-3f, "saturator: silence in, (near) silence out");
    }

    // ---- compressor reduces loud material ----
    {
        vesper::dsp::Compressor comp;
        comp.prepare(ctx);
        comp.set(-30.0f, 8.0f, 1.0f, 80.0f, 3.0f, 0.0f, 0.0f, 100.0f, 0, 0);

        juce::AudioBuffer<float> loud(2, 512);
        for (int block = 0; block < 40; ++block)
        {
            fillSine(loud, 220.0, fs, 0.8f);
            comp.process(loud);
        }
        check(comp.grDb.load() < -6.0f, "compressor applies >6 dB GR to hot signal");
        check(peakOf(loud) < 0.8f, "compressor output quieter than input");
        check(isFiniteBuffer(loud), "compressor output finite");
    }

    // ---- limiter honors ceiling ----
    {
        vesper::dsp::Limiter lim;
        lim.prepare(ctx);
        lim.set(12.0f, -1.0f, 80.0f, true, true);
        const float ceiling = vesper::dsp::dbToGain(-1.0f);

        juce::AudioBuffer<float> buf(2, 512);
        float maxPeak = 0.0f;
        for (int block = 0; block < 40; ++block)
        {
            fillSine(buf, 97.0, fs, 0.99f);
            lim.process(buf);
            maxPeak = juce::jmax(maxPeak, peakOf(buf));
        }
        check(maxPeak <= ceiling + 0.01f, "limiter output never exceeds ceiling");
        check(lim.latencySamples() == (int) std::ceil(fs * 0.0015), "limiter reports lookahead latency");
    }

    // ---- true-peak limiting catches inter-sample overs ----
    {
        // fs/4 sine at 45 deg: sample peaks 0.707*A, true (analog) peak A.
        auto runISP = [&] (bool tp)
        {
            vesper::dsp::Limiter lim;
            lim.prepare(ctx);
            lim.set(0.0f, -1.0f, 100.0f, false, tp); // soft clip off: isolate detection
            juce::AudioBuffer<float> buf(2, 512);
            juce::AudioBuffer<float> out(2, 512);
            for (int block = 0; block < 40; ++block)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const auto n = (double) (block * 512 + i);
                    const float v = 0.98f * (float) std::sin(juce::MathConstants<double>::halfPi * n
                                                             + juce::MathConstants<double>::pi / 4.0);
                    buf.setSample(0, i, v);
                    buf.setSample(1, i, v);
                }
                lim.process(buf);
                out.makeCopyOf(buf, true);
            }
            // reference-grade reconstruction: 32-tap windowed sinc, 7 phases
            constexpr int TL = 32, NP = 7;
            static double taps[NP][TL];
            for (int ph = 0; ph < NP; ++ph)
            {
                double sum = 0.0;
                for (int t = 0; t < TL; ++t)
                {
                    const double x = t - (TL / 2 - 1) - (ph + 1) / 8.0;
                    const double sinc = std::abs(x) < 1e-9 ? 1.0
                        : std::sin(juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                    const double w = 0.42 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * t / (TL - 1.0))
                                   + 0.08 * std::cos(2.0 * juce::MathConstants<double>::twoPi * t / (TL - 1.0));
                    taps[ph][t] = sinc * w;
                    sum += taps[ph][t];
                }
                for (auto& tap : taps[ph]) tap /= sum;
            }
            double tpMax = 0.0;
            const auto* d = out.getReadPointer(0);
            for (int n = TL; n < 512; ++n)
            {
                tpMax = juce::jmax(tpMax, std::abs((double) d[n]));
                for (int ph = 0; ph < NP; ++ph)
                {
                    double v = 0.0;
                    for (int t = 0; t < TL; ++t) v += taps[ph][t] * d[n - (TL - 1) + t];
                    tpMax = juce::jmax(tpMax, std::abs(v));
                }
            }
            return tpMax;
        };
        const double ceilingLin = (double) vesper::dsp::dbToGain(-1.0f);
        const double withTP    = runISP(true);
        const double withoutTP = runISP(false);
        check(withTP <= ceilingLin * 1.06, "true-peak limiter holds inter-sample peaks at ceiling");
        check(withoutTP > ceilingLin * 1.05, "sample-peak mode demonstrably misses inter-sample overs");
    }

    // ---- delay line timing ----
    {
        vesper::dsp::DelayLine dl;
        dl.prepare(fs, 1.0);
        for (int i = 0; i < 1000; ++i)
            dl.write(i == 0 ? 1.0f : 0.0f);
        // After 1000 writes, the impulse (written first) sits 1000 samples back
        check(std::abs(dl.readLinear(1000.0) - 1.0f) < 1.0e-4f,
              "delay line returns impulse at exact delay");
        check(std::abs(dl.readLinear(500.0)) < 1.0e-4f, "delay line silent elsewhere");
    }

    // ---- reverb decays and stays finite ----
    {
        vesper::dsp::FDNReverb verb;
        verb.prepare(ctx);
        verb.set(1, 60.0f, 2.0f, 0.0f, 40.0f, 100.0f, 0.0f, 100.0f, 100.0f);

        juce::AudioBuffer<float> buf(2, 512);
        buf.clear();
        buf.setSample(0, 0, 1.0f);
        buf.setSample(1, 0, 1.0f);
        verb.process(buf);
        float early = peakOf(buf);

        float late = 0.0f;
        for (int block = 0; block < (int) (fs * 6.0 / 512.0); ++block)
        {
            buf.clear();
            verb.process(buf);
            late = peakOf(buf);
            if (! isFiniteBuffer(buf)) { late = 1.0e9f; break; }
        }
        check(std::isfinite(late) && late < 1.0e-2f, "reverb tail decays after 6 s");
        check(early < 4.0f, "reverb impulse response bounded");
    }

    // ---- convolution reverb: Spring & Cathedral stay finite and bounded ----
    {
        juce::ScopedJuceInitialiser_GUI juceInit; // convolution loads on a bg thread
        bool ok = true;
        for (int space = 0; space <= 2; ++space) // spring, cathedral, reverse
        {
            vesper::dsp::ConvReverb cv;
            cv.prepare(ctx);
            cv.regenerate(space, space == 0 ? 1.2f : 6.0f, 70.0f);
            cv.setParams(20.0f, 40.0f, 90.0f, 100.0f, 100.0f);
            juce::Thread::sleep(120); // let the IR load

            juce::AudioBuffer<float> buf(2, 512);
            buf.clear(); buf.setSample(0, 0, 1.0f); buf.setSample(1, 0, 1.0f);
            float peak = 0.0f;
            for (int block = 0; block < (int) (fs * 8.0 / 512.0); ++block)
            {
                cv.process(buf);
                peak = juce::jmax(peak, peakOf(buf));
                if (! isFiniteBuffer(buf)) { ok = false; break; }
                buf.clear(); // impulse only in the first block
            }
            ok = ok && peak < 4.0f;
        }
        check(ok, "convolution reverb (Spring + Cathedral + Reverse) finite and bounded");
    }

    // ---- ladder filter stability at max resonance ----
    {
        vesper::dsp::LadderFilter lad;
        lad.prepare(fs);
        lad.set(2000.0, 1.05, 2.0);
        bool ok = true;
        for (int i = 0; i < 48000; ++i)
        {
            const double y = lad.process(std::sin(0.05 * i) * 0.9);
            if (! std::isfinite(y) || std::abs(y) > 10.0) { ok = false; break; }
        }
        check(ok, "ladder filter stable at max resonance + drive");
    }

    // ---- shimmer stability: max shimmer + long decay must never run away ----
    {
        vesper::dsp::FDNReverb verb;
        verb.prepare(ctx);
        verb.set(1, 70.0f, 20.0f, 0.0f, 30.0f, 60.0f, 100.0f, 100.0f, 100.0f);
        juce::AudioBuffer<float> buf(2, 512);
        buf.clear();
        buf.setSample(0, 0, 1.0f);
        buf.setSample(1, 0, 1.0f);
        verb.process(buf);
        float early = 0.0f, late = 0.0f;
        const int total = (int) (fs * 10.0 / 512.0);
        bool finite = true;
        for (int block = 0; block < total; ++block)
        {
            buf.clear();
            verb.process(buf);
            finite = finite && isFiniteBuffer(buf);
            const float pk = peakOf(buf);
            if (block >= 90 && block < 280) early = juce::jmax(early, pk);   // ~1-3 s
            if (block >= total - 190)       late  = juce::jmax(late, pk);    // last 2 s
        }
        check(finite && late < 4.0f, "shimmer tank stays bounded at max settings");
        check(late <= early * 1.15f + 1.0e-4f, "shimmer tank does not grow (no ringing runaway)");
    }

    // ---- EVERY KNOB WORKS: min-vs-max renders must differ (harness) ----
    {
        constexpr int RB = 24, RN = 512;
        enum Prog { Psine, Plow, Phigh, Pburst, Pasym };
        auto fillProg = [&] (juce::AudioBuffer<float>& b, int block, Prog p)
        {
            for (int i = 0; i < RN; ++i)
            {
                const double t = (double) (block * RN + i);
                auto sine = [&] (double f) { return std::sin(juce::MathConstants<double>::twoPi * f * t / fs); };
                float l = 0.0f, r = 0.0f;
                switch (p)
                {
                    case Psine:  l = r = 0.4f  * (float) sine(220.0);  break;
                    case Plow:   l = r = 0.5f  * (float) sine(60.0);   break;
                    case Phigh:  l = r = 0.3f  * (float) sine(6000.0); break;
                    case Pburst: l = r = ((block % 8) < 4 ? 0.6f : 0.015f) * (float) sine(220.0); break;
                    case Pasym:  l = 0.45f * (float) sine(220.0); r = 0.2f * (float) sine(330.0); break;
                }
                b.setSample(0, i, l);
                b.setSample(1, i, r);
            }
        };
        auto differs = [&] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
        {
            double d = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < a.getNumSamples(); ++i)
                    d = juce::jmax(d, std::abs((double) a.getSample(ch, i) - b.getSample(ch, i)));
            return (float) d;
        };
        auto knob = [&] (const char* nm, float d)
        {
            if (d <= 2.0e-3f) std::printf("  [dead knob] %s (max delta %.6f)\n", nm, d);
            check(d > 2.0e-3f, nm);
        };

        // Generic render: fresh module, cfg applied per block (like the processor)
        #define RENDER(Type, prog, cfg)                                              \
            [&] {                                                                    \
                vesper::dsp::Type m;                                                 \
                m.prepare(ctx);                                                      \
                juce::AudioBuffer<float> out(2, RB * RN), blk(2, RN);                \
                for (int b = 0; b < RB; ++b)                                         \
                {                                                                    \
                    fillProg(blk, b, prog);                                          \
                    cfg;                                                             \
                    m.process(blk);                                                  \
                    for (int ch = 0; ch < 2; ++ch)                                   \
                        out.copyFrom(ch, b * RN, blk, ch, 0, RN);                    \
                }                                                                    \
                return out;                                                          \
            }()

        // Filter
        knob("flt type",   differs(RENDER(CreativeFilter, Psine, m.set(0, 1200, 15, 0)),
                                   RENDER(CreativeFilter, Psine, m.set(2, 1200, 15, 0))));
        knob("flt cutoff", differs(RENDER(CreativeFilter, Psine, m.set(2, 150, 15, 0)),
                                   RENDER(CreativeFilter, Psine, m.set(2, 9000, 15, 0))));
        knob("flt res",    differs(RENDER(CreativeFilter, Psine, m.set(2, 400, 0, 0)),
                                   RENDER(CreativeFilter, Psine, m.set(2, 400, 85, 0))));
        knob("flt drive",  differs(RENDER(CreativeFilter, Psine, m.set(2, 2000, 20, 0)),
                                   RENDER(CreativeFilter, Psine, m.set(2, 2000, 20, 24))));
        // Compressor (burst program exposes time constants)
        knob("cmp thresh", differs(RENDER(Compressor, Pburst, m.set(0,4,5,80,3,0,0,100,0,0)),
                                   RENDER(Compressor, Pburst, m.set(-40,4,5,80,3,0,0,100,0,0))));
        knob("cmp ratio",  differs(RENDER(Compressor, Pburst, m.set(-30,1.2f,5,80,3,0,0,100,0,0)),
                                   RENDER(Compressor, Pburst, m.set(-30,15,5,80,3,0,0,100,0,0))));
        knob("cmp attack", differs(RENDER(Compressor, Pburst, m.set(-30,8,0.1f,80,3,0,0,100,0,0)),
                                   RENDER(Compressor, Pburst, m.set(-30,8,200,80,3,0,0,100,0,0))));
        knob("cmp release",differs(RENDER(Compressor, Pburst, m.set(-30,8,2,8,3,0,0,100,0,0)),
                                   RENDER(Compressor, Pburst, m.set(-30,8,2,1800,3,0,0,100,0,0))));
        knob("cmp knee",   differs(RENDER(Compressor, Pburst, m.set(-6,8,5,80,0.1f,0,0,100,0,0)),
                                   RENDER(Compressor, Pburst, m.set(-6,8,5,80,24,0,0,100,0,0))));
        knob("cmp makeup", differs(RENDER(Compressor, Psine, m.set(-30,4,5,80,3,0,0,100,0,0)),
                                   RENDER(Compressor, Psine, m.set(-30,4,5,80,3,12,0,100,0,0))));
        knob("cmp upward", differs(RENDER(Compressor, Pburst, m.set(-10,4,5,80,3,0,0,100,0,0)),
                                   RENDER(Compressor, Pburst, m.set(-10,4,5,80,3,0,12,100,0,0))));
        knob("cmp mix",    differs(RENDER(Compressor, Pburst, m.set(-30,8,5,80,3,6,0,0,0,0)),
                                   RENDER(Compressor, Pburst, m.set(-30,8,5,80,3,6,0,100,0,0))));
        // Multiband
        knob("mbc xlow",   differs(RENDER(MultibandComp, Plow, m.set(60,2500,-35,8,0, 0,1,0, 0,1,0, 5,120)),
                                   RENDER(MultibandComp, Plow, m.set(500,2500,-35,8,0, 0,1,0, 0,1,0, 5,120))));
        knob("mbc lo thr", differs(RENDER(MultibandComp, Plow, m.set(150,2500,0,8,0, 0,1,0, 0,1,0, 5,120)),
                                   RENDER(MultibandComp, Plow, m.set(150,2500,-40,8,0, 0,1,0, 0,1,0, 5,120))));
        knob("mbc hi thr", differs(RENDER(MultibandComp, Phigh, m.set(150,2500,0,1,0, 0,1,0, 0,8,0, 5,120)),
                                   RENDER(MultibandComp, Phigh, m.set(150,2500,0,1,0, 0,1,0, -40,8,0, 5,120))));
        knob("mbc lo gain",differs(RENDER(MultibandComp, Plow, m.set(150,2500,0,1,0, 0,1,0, 0,1,0, 5,120)),
                                   RENDER(MultibandComp, Plow, m.set(150,2500,0,1,9, 0,1,0, 0,1,0, 5,120))));
        knob("mbc hi gain",differs(RENDER(MultibandComp, Phigh, m.set(150,2500,0,1,0, 0,1,0, 0,1,0, 5,120)),
                                   RENDER(MultibandComp, Phigh, m.set(150,2500,0,1,0, 0,1,0, 0,1,9, 5,120))));
        knob("mbc attack", differs(RENDER(MultibandComp, Pburst, m.set(150,2500,-30,8,0, -30,8,0, -30,8,0, 0.5f,120)),
                                   RENDER(MultibandComp, Pburst, m.set(150,2500,-30,8,0, -30,8,0, -30,8,0, 90,120))));
        // Gate
        knob("gte thresh", differs(RENDER(Gate, Pburst, m.set(-70,-80,0.5f,60,5,0)),
                                   RENDER(Gate, Pburst, m.set(-20,-80,0.5f,60,5,0))));
        knob("gte range",  differs(RENDER(Gate, Pburst, m.set(-25,-6,0.5f,60,5,0)),
                                   RENDER(Gate, Pburst, m.set(-25,-70,0.5f,60,5,0))));
        knob("gte attack", differs(RENDER(Gate, Pburst, m.set(-25,-80,0.05f,60,5,0)),
                                   RENDER(Gate, Pburst, m.set(-25,-80,45,60,5,0))));
        knob("gte release",differs(RENDER(Gate, Pburst, m.set(-25,-80,0.5f,6,5,0)),
                                   RENDER(Gate, Pburst, m.set(-25,-80,0.5f,1500,5,0))));
        knob("gte mode",   differs(RENDER(Gate, Pburst, m.set(-25,-40,0.5f,60,5,0)),
                                   RENDER(Gate, Pburst, m.set(-25,-40,0.5f,60,5,1))));
        // Saturator (2x OS default off for determinism: use setOversample(0))
        #define SATCFG(algo,drv,bias,tone,mix,out) \
            m.setOversample(0); m.setAlgo(algo); m.setDrive(drv); m.setBias(bias); \
            m.setTone(tone); m.setMix(mix); m.setTrim(out)
        knob("sat algo",  differs(RENDER(Saturator, Psine, SATCFG(0,18,0,0,1,0)),
                                  RENDER(Saturator, Psine, SATCFG(13,18,0,0,1,0))));
        knob("sat drive", differs(RENDER(Saturator, Psine, SATCFG(0,0,0,0,1,0)),
                                  RENDER(Saturator, Psine, SATCFG(0,30,0,0,1,0))));
        knob("sat bias",  differs(RENDER(Saturator, Psine, SATCFG(0,15,-1,0,1,0)),
                                  RENDER(Saturator, Psine, SATCFG(0,15,1,0,1,0))));
        knob("sat tone",  differs(RENDER(Saturator, Psine, SATCFG(0,15,0,-1,1,0)),
                                  RENDER(Saturator, Psine, SATCFG(0,15,0,1,1,0))));
        knob("sat mix",   differs(RENDER(Saturator, Psine, SATCFG(10,24,0,0,0,0)),
                                  RENDER(Saturator, Psine, SATCFG(10,24,0,0,1,0))));
        knob("sat trim",  differs(RENDER(Saturator, Psine, SATCFG(0,10,0,0,1,-12)),
                                  RENDER(Saturator, Psine, SATCFG(0,10,0,0,1,12))));
        // Transient
        knob("trn attack", differs(RENDER(TransientDesigner, Pburst, m.set(-100,0,1)),
                                   RENDER(TransientDesigner, Pburst, m.set(100,0,1))));
        knob("trn sustain",differs(RENDER(TransientDesigner, Pburst, m.set(0,-100,1)),
                                   RENDER(TransientDesigner, Pburst, m.set(0,100,1))));
        knob("trn speed",  differs(RENDER(TransientDesigner, Pburst, m.set(80,-60,0)),
                                   RENDER(TransientDesigner, Pburst, m.set(80,-60,2))));
        // Motion
        knob("mod mode",  differs(RENDER(ModulationFX, Psine, m.set(0,1,60,0,1,100)),
                                  RENDER(ModulationFX, Psine, m.set(4,1,60,0,1,100))));
        knob("mod rate",  differs(RENDER(ModulationFX, Psine, m.set(0,0.1f,60,0,1,100)),
                                  RENDER(ModulationFX, Psine, m.set(0,8,60,0,1,100))));
        knob("mod depth", differs(RENDER(ModulationFX, Psine, m.set(0,1,5,0,1,100)),
                                  RENDER(ModulationFX, Psine, m.set(0,1,95,0,1,100))));
        knob("mod fb",    differs(RENDER(ModulationFX, Psine, m.set(3,1,60,0,1,100)),
                                  RENDER(ModulationFX, Psine, m.set(3,1,60,0.9f,1,100))));
        knob("mod spread",differs(RENDER(ModulationFX, Psine, m.set(0,1,60,0,0,100)),
                                  RENDER(ModulationFX, Psine, m.set(0,1,60,0,100,100))));
        knob("mod mix",   differs(RENDER(ModulationFX, Psine, m.set(0,1,60,0,1,0)),
                                  RENDER(ModulationFX, Psine, m.set(0,1,60,0,1,100))));
        // Pitch
        knob("pit mode",  differs(RENDER(PitchFX, Psine, m.set(1,25,100)),
                                  RENDER(PitchFX, Psine, m.set(2,25,100))));
        knob("pit amount",differs(RENDER(PitchFX, Psine, m.set(0,2,100)),
                                  RENDER(PitchFX, Psine, m.set(0,95,100))));
        knob("pit mix",   differs(RENDER(PitchFX, Psine, m.set(1,25,0)),
                                  RENDER(PitchFX, Psine, m.set(1,25,100))));
        // Delay (free time unless testing div; bpm 120)
        #define DLY(sync,t,div,fb,pp,col,wow,lo,hi,dif,w,mix) \
            m.set(sync, t, div, 120.0, fb, pp, col, wow, lo, hi, dif, w, mix)
        knob("dly time",  differs(RENDER(StereoDelay, Pburst, DLY(false,80,6,40,false,0,0,20,20000,0,100,50)),
                                  RENDER(StereoDelay, Pburst, DLY(false,700,6,40,false,0,0,20,20000,0,100,50))));
        knob("dly div",   differs(RENDER(StereoDelay, Pburst, DLY(true,250,3,40,false,0,0,20,20000,0,100,50)),
                                  RENDER(StereoDelay, Pburst, DLY(true,250,9,40,false,0,0,20,20000,0,100,50))));
        knob("dly fb",    differs(RENDER(StereoDelay, Pburst, DLY(false,80,6,5,false,0,0,20,20000,0,100,50)),
                                  RENDER(StereoDelay, Pburst, DLY(false,80,6,80,false,0,0,20,20000,0,100,50))));
        knob("dly ping",  differs(RENDER(StereoDelay, Pasym, DLY(false,200,6,50,false,0,0,20,20000,0,100,50)),
                                  RENDER(StereoDelay, Pasym, DLY(false,200,6,50,true,0,0,20,20000,0,100,50))));
        knob("dly color", differs(RENDER(StereoDelay, Pburst, DLY(false,200,6,60,false,0,0,20,20000,0,100,50)),
                                  RENDER(StereoDelay, Pburst, DLY(false,200,6,60,false,2,0,20,20000,0,100,50))));
        knob("dly wow",   differs(RENDER(StereoDelay, Psine, DLY(false,200,6,50,false,1,0,20,20000,0,100,50)),
                                  RENDER(StereoDelay, Psine, DLY(false,200,6,50,false,1,100,20,20000,0,100,50))));
        knob("dly locut", differs(RENDER(StereoDelay, Plow, DLY(false,150,6,60,false,0,0,20,20000,0,100,60)),
                                  RENDER(StereoDelay, Plow, DLY(false,150,6,60,false,0,0,900,20000,0,100,60))));
        knob("dly hicut", differs(RENDER(StereoDelay, Phigh, DLY(false,150,6,60,false,0,0,20,18000,0,100,60)),
                                  RENDER(StereoDelay, Phigh, DLY(false,150,6,60,false,0,0,20,1200,0,100,60))));
        knob("dly diffuse",differs(RENDER(StereoDelay, Pburst, DLY(false,200,6,50,false,0,0,20,20000,0,100,60)),
                                   RENDER(StereoDelay, Pburst, DLY(false,200,6,50,false,0,0,20,20000,100,100,60))));
        knob("dly width", differs(RENDER(StereoDelay, Pasym, DLY(false,200,6,50,false,0,0,20,20000,0,0,60)),
                                  RENDER(StereoDelay, Pasym, DLY(false,200,6,50,false,0,0,20,20000,0,100,60))));
        knob("dly mix",   differs(RENDER(StereoDelay, Pburst, DLY(false,200,6,50,false,0,0,20,20000,0,100,0)),
                                  RENDER(StereoDelay, Pburst, DLY(false,200,6,50,false,0,0,20,20000,0,100,80))));
        // Reverb
        #define REV(ty,sz,dec,pre,damp,lo,shim,w,mix) m.set(ty,sz,dec,pre,damp,lo,shim,w,mix)
        knob("rev type",  differs(RENDER(FDNReverb, Pburst, REV(0,60,2,0,40,60,0,100,80)),
                                  RENDER(FDNReverb, Pburst, REV(2,60,2,0,40,60,0,100,80))));
        knob("rev size",  differs(RENDER(FDNReverb, Pburst, REV(1,15,2,0,40,60,0,100,80)),
                                  RENDER(FDNReverb, Pburst, REV(1,95,2,0,40,60,0,100,80))));
        knob("rev decay", differs(RENDER(FDNReverb, Pburst, REV(1,60,0.3f,0,40,60,0,100,80)),
                                  RENDER(FDNReverb, Pburst, REV(1,60,10,0,40,60,0,100,80))));
        knob("rev predly",differs(RENDER(FDNReverb, Pburst, REV(1,60,2,0,40,60,0,100,80)),
                                  RENDER(FDNReverb, Pburst, REV(1,60,2,180,40,60,0,100,80))));
        knob("rev damp",  differs(RENDER(FDNReverb, Phigh, REV(1,60,3,0,0,60,0,100,80)),
                                  RENDER(FDNReverb, Phigh, REV(1,60,3,0,100,60,0,100,80))));
        knob("rev locut", differs(RENDER(FDNReverb, Plow, REV(1,60,3,0,40,20,0,100,80)),
                                  RENDER(FDNReverb, Plow, REV(1,60,3,0,40,800,0,100,80))));
        knob("rev shimmer",differs(RENDER(FDNReverb, Pburst, REV(1,60,3,0,40,60,0,100,80)),
                                   RENDER(FDNReverb, Pburst, REV(1,60,3,0,40,60,80,100,80))));
        knob("rev width", differs(RENDER(FDNReverb, Pasym, REV(1,60,3,0,40,60,0,0,80)),
                                  RENDER(FDNReverb, Pasym, REV(1,60,3,0,40,60,0,100,80))));
        knob("rev mix",   differs(RENDER(FDNReverb, Pburst, REV(1,60,3,0,40,60,0,100,5)),
                                  RENDER(FDNReverb, Pburst, REV(1,60,3,0,40,60,0,100,90))));
        // Stereo tools
        knob("st width",  differs(RENDER(StereoTools, Pasym, m.set(0,0,0,0,0,0)),
                                  RENDER(StereoTools, Pasym, m.set(200,0,0,0,0,0))));
        knob("st mid",    differs(RENDER(StereoTools, Pasym, m.set(100,-9,0,0,0,0)),
                                  RENDER(StereoTools, Pasym, m.set(100,9,0,0,0,0))));
        knob("st side",   differs(RENDER(StereoTools, Pasym, m.set(100,0,-9,0,0,0)),
                                  RENDER(StereoTools, Pasym, m.set(100,0,9,0,0,0))));
        knob("st haas",   differs(RENDER(StereoTools, Pasym, m.set(100,0,0,0,0,0)),
                                  RENDER(StereoTools, Pasym, m.set(100,0,0,30,0,0))));
        knob("st pan",    differs(RENDER(StereoTools, Psine, m.set(100,0,0,0,-1,0)),
                                  RENDER(StereoTools, Psine, m.set(100,0,0,0,1,0))));
        knob("st mono<",  differs(RENDER(StereoTools, Pasym, m.set(100,0,0,0,0,0)),
                                  RENDER(StereoTools, Pasym, m.set(100,0,0,0,0,400))));
        // Limiter
        knob("lim gain",  differs(RENDER(Limiter, Psine, m.set(0,-0.3f,80,false,false)),
                                  RENDER(Limiter, Psine, m.set(12,-0.3f,80,false,false))));
        knob("lim ceil",  differs(RENDER(Limiter, Psine, m.set(12,0,80,false,false)),
                                  RENDER(Limiter, Psine, m.set(12,-2,80,false,false))));
        knob("lim rel",   differs(RENDER(Limiter, Pburst, m.set(12,-1,12,false,false)),
                                  RENDER(Limiter, Pburst, m.set(12,-1,900,false,false))));
        // EQ band params
        auto eqRender = [&] (Prog p, vesper::dsp::ParametricEQ::Band band)
        {
            vesper::dsp::ParametricEQ m;
            m.prepare(ctx);
            juce::AudioBuffer<float> out(2, RB * RN), blk(2, RN);
            for (int b = 0; b < RB; ++b)
            {
                fillProg(blk, b, p);
                m.setBand(0, band);
                m.process(blk);
                for (int ch = 0; ch < 2; ++ch)
                    out.copyFrom(ch, b * RN, blk, ch, 0, RN);
            }
            return out;
        };
        using Band = vesper::dsp::ParametricEQ::Band;
        knob("eq gain", differs(eqRender(Psine, Band { true, 0, 220, -12, 1, 0 }),
                                eqRender(Psine, Band { true, 0, 220, 12, 1, 0 })));
        knob("eq freq", differs(eqRender(Psine, Band { true, 0, 220, 9, 2, 0 }),
                                eqRender(Psine, Band { true, 0, 2000, 9, 2, 0 })));
        knob("eq q",    differs(eqRender(Psine, Band { true, 0, 300, 9, 0.3f, 0 }),
                                eqRender(Psine, Band { true, 0, 300, 9, 12, 0 })));
        knob("eq type", differs(eqRender(Psine, Band { true, 3, 800, 0, 0.71f, 0 }),
                                eqRender(Psine, Band { true, 4, 800, 0, 0.71f, 0 })));
        #undef RENDER
        #undef SATCFG
        #undef DLY
        #undef REV
        // Deliberately not asserted (documented, not dead):
        //   dly_time while synced / dly_div while free (each governs the other
        //   mode) · cmp detect+character (subtle detector color) · gte hold
        //   (sub-block timing) · eq DYN (own test above) · lim clip/TP (own
        //   tests above) · morph/macros (engine tests above).
    }

    // ---- sidechain: external signal drives the detectors (D-021) ----
    {
        // Ducking: quiet main, loud sidechain -> compressor still bites
        auto runDuck = [&] (bool scOn)
        {
            vesper::dsp::Compressor comp;
            comp.prepare(ctx);
            comp.set(-30.0f, 8.0f, 1.0f, 80.0f, 3.0f, 0.0f, 0.0f, 100.0f, 0, 0, scOn);
            juce::AudioBuffer<float> mainBuf(2, 512);
            std::vector<float> sc(512);
            for (int block = 0; block < 40; ++block)
            {
                fillSine(mainBuf, 220.0, fs, 0.02f); // -34 dB main: under threshold
                for (int i = 0; i < 512; ++i)        // loud external key
                    sc[(size_t) i] = 0.8f * (float) std::sin(juce::MathConstants<double>::twoPi
                                                             * 110.0 * i / fs);
                comp.setSidechain(sc.data(), sc.data());
                comp.process(mainBuf);
            }
            return comp.grDb.load();
        };
        check(runDuck(true) < -6.0f, "sidechain compressor ducks quiet main from loud key");
        check(runDuck(false) > -1.0f, "without sidechain the quiet main stays untouched");

        // Keyed gate: loud main, silent sidechain -> gate stays closed
        vesper::dsp::Gate gate;
        gate.prepare(ctx);
        gate.set(-40.0f, -80.0f, 0.5f, 50.0f, 5.0f, 0, true);
        std::vector<float> silentSC(512, 0.0f);
        juce::AudioBuffer<float> loud(2, 512);
        for (int block = 0; block < 40; ++block)
        {
            fillSine(loud, 220.0, fs, 0.5f);
            gate.setSidechain(silentSC.data(), silentSC.data());
            gate.process(loud);
        }
        check(peakOf(loud) < 0.01f, "keyed gate closes on silent sidechain despite loud main");
    }

    // ---- macro engine: assign, travel, remove (D-020) ----
    {
        juce::ScopedJuceInitialiser_GUI juceInit; // MessageManager for APVTS

        struct DummyProc : juce::AudioProcessor
        {
            juce::AudioProcessorValueTreeState apvts;
            DummyProc() : apvts(*this, nullptr, "T", vesper::createParameterLayout()) {}
            const juce::String getName() const override { return "T"; }
            void prepareToPlay(double, int) override {}
            void releaseResources() override {}
            using juce::AudioProcessor::processBlock;
            void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
            juce::AudioProcessorEditor* createEditor() override { return nullptr; }
            bool hasEditor() const override { return false; }
            bool acceptsMidi() const override { return false; }
            bool producesMidi() const override { return false; }
            double getTailLengthSeconds() const override { return 0.0; }
            int getNumPrograms() override { return 1; }
            int getCurrentProgram() override { return 0; }
            void setCurrentProgram(int) override {}
            const juce::String getProgramName(int) override { return {}; }
            void changeProgramName(int, const juce::String&) override {}
            void getStateInformation(juce::MemoryBlock&) override {}
            void setStateInformation(const void*, int) override {}
        };

        DummyProc dp;
        vesper::MacroEngine eng(dp.apvts);
        auto* drive  = dp.apvts.getParameter("sat_drive");
        auto* macro1 = dp.apvts.getParameter("macro1");

        eng.assign("sat_drive", 1);
        check(eng.macroFor("sat_drive") == 1, "macro engine records the assignment");

        macro1->setValueNotifyingHost(1.0f);
        eng.applyNow();
        check(drive->getValue() > 0.99f, "macro at max drives target to its MAX end");

        macro1->setValueNotifyingHost(0.5f);
        eng.applyNow();
        check(std::abs(drive->getValue() - 0.5f) < 0.01f, "macro travel interpolates");

        // pin MIN to the current (half-way) value: macro 0 must now land there
        eng.setEndToCurrent("sat_drive", false);
        macro1->setValueNotifyingHost(0.0f);
        eng.applyNow();
        check(std::abs(drive->getValue() - 0.5f) < 0.01f, "MIN pin narrows the travel");

        eng.remove("sat_drive");
        check(eng.macroFor("sat_drive") == 0, "assignment removed");
        check(! vesper::MacroEngine::assignable("macro2"), "macros cannot follow macros");
    }

    // ---- chain order packing ----
    {
        std::array<int, vesper::numModules> order { 9, 3, 5, 0, 12, 1, 2, 10, 4, 6, 11, 7, 8 };
        check(vesper::unpackOrder(vesper::packOrder(order)) == order,
              "chain order pack/unpack roundtrip (13 modules)");
    }

    // ---- dynamic EQ: a flaring band gets tamed (adaptive reference) ----
    {
        auto runFlare = [&] (float dynAmount)
        {
            vesper::dsp::ParametricEQ eq;
            eq.prepare(ctx);
            vesper::dsp::ParametricEQ::Band b;
            b.on = true; b.type = 0; b.freq = 1000.0f; b.gain = 0.0f;
            b.q = 1.5f; b.dyn = dynAmount;
            eq.setBand(0, b);

            juce::AudioBuffer<float> buf(2, 512);
            // quiet bed lets the reference settle low
            for (int block = 0; block < 30; ++block)
            {
                fillSine(buf, 1000.0, fs, 0.02f);
                eq.process(buf);
            }
            // sudden flare: measure the early response
            float peak = 0.0f;
            for (int block = 0; block < 4; ++block)
            {
                fillSine(buf, 1000.0, fs, 0.6f);
                eq.process(buf);
                if (block >= 2) peak = juce::jmax(peak, peakOf(buf));
            }
            return peak;
        };
        const float tamed  = runFlare(-12.0f);
        const float still  = runFlare(0.0f);
        check(std::isfinite(tamed) && std::isfinite(still), "dynamic EQ output finite");
        check(tamed < still * 0.85f, "dynamic EQ dips a flaring band by >1.4 dB");
    }

    // ---- gate closes on silence, opens on signal ----
    {
        vesper::dsp::Gate gate;
        gate.prepare(ctx);
        gate.set(-40.0f, -80.0f, 0.5f, 50.0f, 5.0f, 0);

        juce::AudioBuffer<float> quiet(2, 512);
        fillSine(quiet, 220.0, fs, 0.001f); // -60 dB: below threshold
        for (int block = 0; block < 40; ++block) { fillSine(quiet, 220.0, fs, 0.001f); gate.process(quiet); }
        check(peakOf(quiet) < 0.001f * 0.1f, "gate attenuates sub-threshold signal");

        juce::AudioBuffer<float> loud(2, 512);
        for (int block = 0; block < 40; ++block) { fillSine(loud, 220.0, fs, 0.5f); gate.process(loud); }
        check(peakOf(loud) > 0.4f, "gate passes above-threshold signal");
        check(isFiniteBuffer(loud), "gate output finite");
    }

    // ---- multiband: flat when idle, compresses hot lows ----
    {
        vesper::dsp::MultibandComp mbc;
        mbc.prepare(ctx);
        // Thresholds at 0 dB, ratio 1 -> should be ~transparent (allpass-flat)
        mbc.set(150.0f, 2500.0f, 0, 1, 0, 0, 1, 0, 0, 1, 0, 15.0f, 150.0f);
        juce::AudioBuffer<float> buf(2, 512);
        float p = 0.0f;
        for (int block = 0; block < 30; ++block)
        {
            // phase-continuous sine across blocks (no boundary clicks)
            for (int i = 0; i < 512; ++i)
            {
                const auto t = (double) (block * 512 + i);
                const float v = 0.25f * (float) std::sin(juce::MathConstants<double>::twoPi * 440.0 * t / fs);
                buf.setSample(0, i, v);
                buf.setSample(1, i, v);
            }
            mbc.process(buf);
            p = peakOf(buf);
        }
        check(std::abs(p - 0.25f) < 0.04f, "multiband is ~transparent at ratio 1:1");

        mbc.set(150.0f, 2500.0f, -30, 8, 0, 0, 1, 0, 0, 1, 0, 2.0f, 100.0f);
        for (int block = 0; block < 40; ++block) { fillSine(buf, 60.0, fs, 0.8f); mbc.process(buf); }
        check(mbc.bandGrDb[0].load() < -6.0f, "multiband low band compresses hot 60 Hz");
        check(isFiniteBuffer(buf), "multiband output finite");
    }

    // ---- pitch: all modes finite and mix-obedient ----
    {
        bool ok = true;
        for (int mode = 0; mode < 5; ++mode)
        {
            vesper::dsp::PitchFX pit;
            pit.prepare(ctx);
            pit.set(mode, 50.0f, 100.0f);
            juce::AudioBuffer<float> buf(2, 512);
            for (int block = 0; block < 20; ++block)
            {
                fillSine(buf, 330.0, fs, 0.5f);
                pit.process(buf);
            }
            ok = ok && isFiniteBuffer(buf) && peakOf(buf) < 2.0f;
        }
        check(ok, "all 5 pitch modes finite and bounded");
    }

    // ---- stereo tools: mono-below folds side band ----
    {
        vesper::dsp::StereoTools st;
        st.prepare(ctx);
        st.set(100.0f, 0.0f, 0.0f, 0.0f, 0.0f, 200.0f);
        juce::AudioBuffer<float> buf(2, 512);
        // 50 Hz anti-phase content = pure side signal below the mono frequency
        float sideEnergy = 0.0f;
        for (int block = 0; block < 20; ++block)
        {
            fillSine(buf, 50.0, fs, 0.5f);
            for (int i = 0; i < 512; ++i)
                buf.setSample(1, i, -buf.getSample(1, i));
            st.process(buf);
            sideEnergy = 0.0f;
            for (int i = 0; i < 512; ++i)
                sideEnergy = juce::jmax(sideEnergy,
                                        std::abs(buf.getSample(0, i) - buf.getSample(1, i)));
        }
        check(sideEnergy < 0.25f, "mono-below attenuates low-frequency side content");
    }

    // ---- Motion rate-sync (M6): synced tremolo follows the division table ----
    {
        // Tremolo at a known LFO rate leaves a measurable modulation period.
        // Feed DC-ish (low sine), count amplitude dips over a fixed window for
        // two divisions at 120 BPM: 1/4 -> 2 Hz, 1/16 -> 8 Hz.
        auto dipsFor = [&] (double lfoHz)
        {
            vesper::dsp::ModulationFX m;
            m.prepare(ctx);
            m.set(vesper::dsp::ModulationFX::tremoloMode, (float) lfoHz,
                  100.0f, 0.0f, 0.0f, 100.0f);
            juce::AudioBuffer<float> buf(2, 512);
            int dips = 0; bool below = false;
            for (int block = 0; block < 200; ++block) // ~2.1 s
            {
                fillSine(buf, 30.0, fs, 0.5f);
                m.process(buf);
                float pk = 0.0f;
                for (int i = 0; i < 512; ++i)
                    pk = juce::jmax(pk, std::abs(buf.getSample(0, i)));
                const bool low = pk < 0.18f;
                if (low && ! below) ++dips;
                below = low;
            }
            return dips;
        };
        // the division table the processor uses for sync (120 BPM)
        const double hzQuarter   = 120.0 / 60.0 / vesper::delayDivBeats(3);  // 1/4  = 2 Hz
        const double hzSixteenth = 120.0 / 60.0 / vesper::delayDivBeats(9);  // 1/16 = 8 Hz
        const int d4  = dipsFor(hzQuarter);
        const int d16 = dipsFor(hzSixteenth);
        check(std::abs(hzQuarter - 2.0) < 1.0e-9 && std::abs(hzSixteenth - 8.0) < 1.0e-9,
              "motion sync: division table maps 1/4->2Hz, 1/16->8Hz at 120 BPM");
        check(d16 > d4 * 2 && d4 >= 2,
              "motion sync: faster division modulates proportionally faster");
    }

    // ---- Preset recents: LRU order, dedup, cap (D-042) --------------------
    {
        struct Dummy : juce::AudioProcessor
        {
            Dummy() : juce::AudioProcessor(BusesProperties()) {}
            const juce::String getName() const override { return "d"; }
            void prepareToPlay(double, int) override {}
            void releaseResources() override {}
            using juce::AudioProcessor::processBlock;
            void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
            juce::AudioProcessorEditor* createEditor() override { return nullptr; }
            bool hasEditor() const override { return false; }
            bool acceptsMidi() const override { return false; }
            bool producesMidi() const override { return false; }
            double getTailLengthSeconds() const override { return 0.0; }
            int getNumPrograms() override { return 1; }
            int getCurrentProgram() override { return 0; }
            void setCurrentProgram(int) override {}
            const juce::String getProgramName(int) override { return {}; }
            void changeProgramName(int, const juce::String&) override {}
            void getStateInformation(juce::MemoryBlock&) override {}
            void setStateInformation(const void*, int) override {}
        };
        Dummy d;
        juce::UndoManager undo; // loadPreset applies XML through the undo manager
        juce::AudioProcessorValueTreeState apvts(d, &undo, "VESPER",
                                                 vesper::createParameterLayout());
        // start from a clean recents file so the run is deterministic
        vesper::PresetManager::presetRoot().getChildFile("recents.json").deleteFile();
        vesper::PresetManager pm(apvts, d);

        const auto& all = pm.getAll();
        bool ran = false, order = false, deduped = false, capped = false;
        if (all.size() >= 10)
        {
            ran = true;
            for (int i = 0; i < 10; ++i) pm.loadPreset(all[(size_t) i]); // load 10 distinct
            capped = pm.recents().size() <= 8;                            // cap at 8
            order = pm.recents()[0] == all[9].name;                       // newest first
            pm.loadPreset(all[3]);                                        // re-load an older one
            deduped = pm.recents()[0] == all[3].name
                   && pm.recents().indexOf(all[3].name) == 0
                   && [&]
                      {
                          int c = 0;
                          for (auto& n : pm.recents())
                              if (n == all[3].name) ++c;
                          return c;
                      }() == 1;
        }
        check(ran && order && deduped && capped,
              "preset recents: newest-first, de-duplicated, capped at 8");
        vesper::PresetManager::presetRoot().getChildFile("recents.json").deleteFile();
    }

    // ---- Macro assignment counting (D-041) --------------------------------
    {
        struct Dummy : juce::AudioProcessor
        {
            Dummy() : juce::AudioProcessor(BusesProperties()) {}
            const juce::String getName() const override { return "d"; }
            void prepareToPlay(double, int) override {}
            void releaseResources() override {}
            using juce::AudioProcessor::processBlock;
            void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
            juce::AudioProcessorEditor* createEditor() override { return nullptr; }
            bool hasEditor() const override { return false; }
            bool acceptsMidi() const override { return false; }
            bool producesMidi() const override { return false; }
            double getTailLengthSeconds() const override { return 0.0; }
            int getNumPrograms() override { return 1; }
            int getCurrentProgram() override { return 0; }
            void setCurrentProgram(int) override {}
            const juce::String getProgramName(int) override { return {}; }
            void changeProgramName(int, const juce::String&) override {}
            void getStateInformation(juce::MemoryBlock&) override {}
            void setStateInformation(const void*, int) override {}
        };
        Dummy d;
        juce::AudioProcessorValueTreeState apvts(d, nullptr, "VESPER",
                                                 vesper::createParameterLayout());
        vesper::MacroEngine macros(apvts);
        int notified = 0;
        macros.onAssignmentsChanged = [&notified] { ++notified; };

        const bool empty = macros.targetCount(1) == 0;
        macros.assign("flt_cutoff", 1);
        macros.assign("rev_mix", 1);
        macros.assign("dly_mix", 2);
        const bool counted = macros.targetCount(1) == 2 && macros.targetCount(2) == 1
                          && macros.targetCount(3) == 0;
        macros.remove("rev_mix");
        const bool afterRemove = macros.targetCount(1) == 1;
        // a parameter follows at most one macro: reassigning moves it
        macros.assign("flt_cutoff", 3);
        const bool moved = macros.targetCount(1) == 0 && macros.targetCount(3) == 1;

        check(empty && counted && afterRemove && moved && notified > 0,
              "macros: assignment counts track assign/remove/reassign + notify");
    }

    // ---- Factory preset QA (D-040) ---------------------------------------
    /* Every shipped preset is audited against the live parameter set: an id
       that no longer exists, or a value outside its parameter's range, is
       silently ignored at load time — the preset just quietly does less than
       it claims. Nothing else in the suite would catch that. */
    {
        std::map<juce::String, juce::NormalisableRange<float>> ranges;
        for (const auto& p : vesper::createParameterList())
            ranges.emplace(p->paramID, p->getNormalisableRange());

        int presets = 0, badId = 0, badRange = 0, unstable = 0;
        juce::StringArray offenders;

        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const juce::String origName(BinaryData::originalFilenames[i]);
            if (! origName.endsWithIgnoreCase(".vpreset")) continue;

            int size = 0;
            const char* data = BinaryData::getNamedResource(
                BinaryData::namedResourceList[i], size);
            if (data == nullptr || size <= 0) continue;

            auto xml = juce::parseXML(juce::String::fromUTF8(data, size));
            if (xml == nullptr) { ++badId; offenders.add(origName + " (unparseable)"); continue; }
            ++presets;

            for (auto* e : xml->getChildWithTagNameIterator("PARAM"))
            {
                const auto id = e->getStringAttribute("id");
                const auto it = ranges.find(id);
                if (it == ranges.end())
                {
                    ++badId; offenders.add(origName + " -> unknown id '" + id + "'");
                    continue;
                }
                const float v = (float) e->getDoubleAttribute("value");
                const auto& r = it->second;
                if (v < r.start - 1.0e-4f || v > r.end + 1.0e-4f)
                {
                    ++badRange;
                    offenders.add(origName + " -> " + id + "=" + juce::String(v)
                                  + " outside [" + juce::String(r.start) + ", "
                                  + juce::String(r.end) + "]");
                }
                // D-023 stability caps on the runaway-capable parameters
                const bool over = (id == "rev_shimmer" && v > 85.0f)
                               || (id == "dly_feedback" && v > 110.0f)
                               || (id == "flt_res"      && v > 95.0f);
                if (over) { ++unstable; offenders.add(origName + " -> " + id + "=" + juce::String(v)); }
            }
        }
        for (int i = 0; i < juce::jmin(6, offenders.size()); ++i)
            std::printf("      %s\n", offenders[i].toRawUTF8());

        check(presets >= 20, "preset QA: factory presets found in the binary");
        check(badId == 0,    "preset QA: every preset id exists in the parameter set");
        check(badRange == 0, "preset QA: every preset value is inside its range");
        check(unstable == 0, "preset QA: stability caps respected (D-023)");
    }

    // ---- User IR loader + convolution shimmer (D-038) --------------------
    {
        // write a short decaying-noise IR to a temp wav, then load it
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("vesper_test_ir.wav");
        {
            juce::AudioBuffer<float> ir(2, (int) (fs * 0.5));
            auto& rng = juce::Random::getSystemRandom();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < ir.getNumSamples(); ++i)
                    ir.setSample(ch, i, (float) ((rng.nextDouble() * 2.0 - 1.0)
                                                 * std::exp(-3.0 * i / (fs * 0.2))));
            juce::WavAudioFormat wav;
            tmp.deleteFile();
            if (auto* os = tmp.createOutputStream().release())
                if (auto* w = wav.createWriterFor(os, fs, 2, 16, {}, 0))
                {
                    w->writeFromAudioSampleBuffer(ir, 0, ir.getNumSamples());
                    delete w;
                }
        }

        vesper::dsp::ConvReverb cv;
        cv.prepare(ctx);
        const bool loaded  = cv.loadUserIR(tmp);
        const bool refused = ! cv.loadUserIR(
            juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("vesper_no_such_ir.wav"));
        check(loaded && refused,
              "user IR: loads a real wav, refuses a missing file");

        // shimmer is feedforward — it must stay bounded even at full amount
        cv.setParams(0.0f, 45.0f, 90.0f, 100.0f, 100.0f);
        cv.setShimmer(100.0f);
        juce::AudioBuffer<float> buf(2, 512);
        float peak = 0.0f;
        bool finite = true;
        for (int b = 0; b < 200; ++b) // ~2.1 s of continuous excitation
        {
            fillSine(buf, 220.0, fs, 0.5f);
            cv.process(buf);
            finite = finite && isFiniteBuffer(buf);
            peak = juce::jmax(peak, peakOf(buf));
        }
        check(finite && peak < 4.0f,
              "conv shimmer: bounded and finite at 100% (no feedback runaway)");
        tmp.deleteFile();
    }

    // ---- Automation continuity harness (D-037) --------------------------
    /* Hosts deliver parameter changes at BLOCK granularity (JUCE's VST3
       wrapper keeps only the last point of each queue and drops
       offsetSamples), so every automated parameter steps once per block.
       Any parameter that reaches the output without smoothing therefore
       clicks. This harness automates a parameter across its range, one step
       per block, and asserts the sample-to-sample delta at block boundaries
       never spikes above the signal's own in-block delta. */
    {
        auto isContinuous = [&] (std::function<void(float)> setParam,
                                 std::function<void(juce::AudioBuffer<float>&)> proc,
                                 float from, float to)
        {
            constexpr int blocks = 40, bs = 64;
            std::vector<float> out;
            out.reserve((size_t) (blocks * bs));
            juce::AudioBuffer<float> buf(2, bs);
            double phase = 0.0;
            const double inc = juce::MathConstants<double>::twoPi * 100.0 / fs;
            std::vector<int> boundaries;

            for (int b = 0; b < blocks; ++b)
            {
                setParam(juce::jmap((float) b / (float) (blocks - 1), from, to));
                for (int i = 0; i < bs; ++i) // continuous phase across blocks
                {
                    const float v = 0.5f * (float) std::sin(phase);
                    phase += inc;
                    buf.setSample(0, i, v);
                    buf.setSample(1, i, v);
                }
                proc(buf);
                if (b > 2) boundaries.push_back((int) out.size()); // skip warm-up
                for (int i = 0; i < bs; ++i) out.push_back(buf.getSample(0, i));
            }

            /* Curvature (second difference) is the click detector: a smooth
               waveform's second difference is second-order small, while a
               step discontinuity in gain shows up directly. Comparing
               boundary curvature against the signal's own in-block curvature
               keeps it valid for nonlinear modules (saturation raises both). */
            float inBlockMax = 0.0f, boundaryMax = 0.0f;
            const std::set<int> bset(boundaries.begin(), boundaries.end());
            const size_t start = (size_t) (3 * bs) + 2; // warm-up excluded from BOTH
            for (size_t i = start; i < out.size(); ++i)
            {
                // a 3-tap second difference straddles the edge at both i and
                // i+1, so both belong to the boundary set
                const float curv = std::abs(out[i] - 2.0f * out[i - 1] + out[i - 2]);
                if (bset.count((int) i) || bset.count((int) i - 1))
                    boundaryMax = juce::jmax(boundaryMax, curv);
                else
                    inBlockMax = juce::jmax(inBlockMax, curv);
            }
            return boundaryMax <= juce::jmax(1.0e-5f, inBlockMax * 3.0f);
        };
        auto continuous = [&] (const char* name,
                               std::function<void(float)> setParam,
                               std::function<void(juce::AudioBuffer<float>&)> proc,
                               float from, float to)
        {
            check(isContinuous(std::move(setParam), std::move(proc), from, to), name);
        };

        // Negative control: an unsmoothed gain MUST trip the harness, or the
        // four checks below would pass vacuously.
        {
            float rawGain = 1.0f;
            check(! isContinuous([&] (float v) { rawGain = v; },
                                 [&] (juce::AudioBuffer<float>& b) { b.applyGain(rawGain); },
                                 1.0f, 4.0f),
                  "automation: harness detects an unsmoothed gain (control)");
        }

        {
            vesper::dsp::Compressor c; c.prepare(ctx);
            continuous("automation: comp makeup steps without clicking",
                       [&] (float v) { c.set(-20.0f, 4.0f, 10.0f, 100.0f, 6.0f, v, 0.0f,
                                             100.0f, 1, 0, false); },
                       [&] (juce::AudioBuffer<float>& b) { c.process(b); }, 0.0f, 18.0f);
        }
        {
            vesper::dsp::Saturator s; s.prepare(ctx);
            continuous("automation: saturator drive steps without clicking",
                       [&] (float v) { s.setDrive(v); },
                       [&] (juce::AudioBuffer<float>& b) { s.process(b); }, 0.0f, 24.0f);
        }
        {
            vesper::dsp::StereoTools st; st.prepare(ctx);
            continuous("automation: stereo width steps without clicking",
                       [&] (float v) { st.set(v, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f); },
                       [&] (juce::AudioBuffer<float>& b) { st.process(b); }, 0.0f, 200.0f);
        }
        {
            vesper::dsp::Limiter lm; lm.prepare(ctx);
            continuous("automation: limiter gain steps without clicking",
                       [&] (float v) { lm.set(v, -0.3f, 100.0f, false, true); },
                       [&] (juce::AudioBuffer<float>& b) { lm.process(b); }, 0.0f, 12.0f);
        }
    }

    // ---- Filter gain discipline (D-047) ----------------------------------
    /* A resonant filter must not become a weapon. Sweep resonance across the
       whole range for every model, with drive engaged, and measure the gain
       it applies to broadband program material. Anything that amplifies its
       input by more than a few dB at high resonance will scream on real
       audio, which is exactly what "Acid Line" did. */
    {
        static const char* names[] = { "clean12", "clean24", "ladder",
                                       "acid", "comb", "formant" };
        float worstGain = -99.0f;
        juce::String worstWhere;
        bool ok = true;

        for (int type = 0; type < 6; ++type)
            for (float res : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
            {
                vesper::dsp::CreativeFilter f;
                f.prepare(ctx);
                f.set(type, 900.0f, res, 8.0f);   // the Acid Line drive setting

                juce::AudioBuffer<float> buf(2, 512);
                double inSum = 0.0, outSum = 0.0;
                int n = 0;
                auto& rng = juce::Random::getSystemRandom();
                for (int b = 0; b < 40; ++b)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        const float v = 0.25f * (float) (rng.nextDouble() * 2.0 - 1.0);
                        buf.setSample(0, i, v);
                        buf.setSample(1, i, v);
                        if (b >= 10) { inSum += (double) v * v; ++n; }
                    }
                    f.process(buf);
                    if (b >= 10)
                        for (int i = 0; i < 512; ++i)
                        {
                            const double o = buf.getSample(0, i);
                            outSum += o * o;
                        }
                }
                const float gainDb = (float) juce::Decibels::gainToDecibels(
                    std::sqrt(juce::jmax(1.0e-12, outSum / juce::jmax(1, n)))
                    / std::sqrt(juce::jmax(1.0e-12, inSum / juce::jmax(1, n))));
                if (gainDb > worstGain)
                {
                    worstGain = gainDb;
                    worstWhere = juce::String(names[type]) + " @ res " + juce::String((int) res);
                }
                if (! std::isfinite(gainDb) || gainDb > 6.0f) ok = false;
                std::printf("      %-8s res %3d -> %+6.1f dB\n",
                            names[type], (int) res, gainDb);
            }

        std::printf("      [worst filter gain: %+.1f dB (%s)]\n",
                    worstGain, worstWhere.toRawUTF8());
        check(ok, "filter: no model/resonance combination amplifies by more than +6 dB");
    }

    /* ---- Every band TYPE must draw a curve (D-064) ----------------------
       Symptom: some EQ points did not draw a curve on the graph.
       The analyzer plots ParametricEQ::responseDbAt and only strokes the
       path when the span exceeds 0.2 dB, so a type whose response is flat
       at its own default gain is invisible on screen even though the node
       sits there. Probe every type the way the UI does. */
    {
        using PEQ = vesper::dsp::ParametricEQ;
        static const char* tName[] = { "bell", "lo-shelf", "hi-shelf",
                                       "HPF", "LPF", "notch" };
        auto spanOf = [] (int type, float gainDb, float q)
        {
            std::array<PEQ::Band, PEQ::numBands> bands {};
            bands[0] = { true, type, 1000.0f, gainDb, q, 0.0f };
            float span = 0.0f;
            for (double hz = 20.0; hz < 20000.0; hz *= 1.05)
                span = juce::jmax(span, std::abs(
                    PEQ::responseDbAt(bands.data(), fs, hz)));
            return span;
        };

        /* Gain-shaping types (bell/shelves) are legitimately flat at 0 dB —
           that is what "no boost" means. HPF / LPF / NOTCH ignore gain and
           must ALWAYS shape, at any gain value. */
        bool cutsAlwaysDraw = true;
        for (int type : { 3, 4, 5 })
            for (float g : { 0.0f, 6.0f, -6.0f })
                if (spanOf(type, g, 0.71f) <= 0.2f)
                {
                    cutsAlwaysDraw = false;
                    std::printf("  [eq-curve] %s at gain %+.0f dB is FLAT (span %.3f)\n",
                                tName[type], g, spanOf(type, g, 0.71f));
                }
        check(cutsAlwaysDraw,
              "eq-curve: HPF/LPF/NOTCH always shape the curve, whatever the gain");

        bool boostsDraw = true;
        for (int type : { 0, 1, 2 })
            for (float g : { 3.0f, -3.0f })
                if (spanOf(type, g, 0.71f) <= 0.2f) boostsDraw = false;
        check(boostsDraw, "eq-curve: bell/shelves draw as soon as gain leaves 0 dB");

        // A band switched off contributes nothing, whatever its settings.
        std::array<PEQ::Band, PEQ::numBands> off {};
        off[0] = { false, 3, 1000.0f, 0.0f, 0.71f, 0.0f };
        check(std::abs(PEQ::responseDbAt(off.data(), fs, 100.0)) < 1.0e-4f,
              "eq-curve: a disabled band contributes nothing");

        /* The UI dims GAIN and refuses vertical drags for types where
           eqTypeHasGain() is false. That claim must match the DSP or the
           UI is lying: prove gain is inert for exactly those types. */
        bool claimMatchesDsp = true;
        for (int type = 0; type < 6; ++type)
        {
            std::array<PEQ::Band, PEQ::numBands> a {}, b {};
            a[0] = { true, type, 1000.0f,  0.0f, 0.71f, 0.0f };
            b[0] = { true, type, 1000.0f, 12.0f, 0.71f, 0.0f };
            float maxDelta = 0.0f;
            for (double hz = 20.0; hz < 20000.0; hz *= 1.05)
                maxDelta = juce::jmax(maxDelta,
                    std::abs(PEQ::responseDbAt(a.data(), fs, hz)
                             - PEQ::responseDbAt(b.data(), fs, hz)));
            const bool gainDoesSomething = maxDelta > 0.1f;
            if (gainDoesSomething != vesper::eqTypeHasGain(type))
            {
                claimMatchesDsp = false;
                std::printf("  [eq-gain] %s: eqTypeHasGain=%d but DSP delta %.3f dB\n",
                            tName[type], (int) vesper::eqTypeHasGain(type), maxDelta);
            }
        }
        check(claimMatchesDsp,
              "eq-gain: eqTypeHasGain() agrees with the DSP for every band type");
    }

    // ---- Linear-phase EQ (D-036): band curve realized as a symmetric FIR ----
    {
        using LPE = vesper::dsp::LinearPhaseEQ;
        constexpr int N = LPE::fftSize;

        auto responseDb = [](const std::array<float, N>& k, double hz, double sr)
        {
            static juce::dsp::FFT fft(LPE::order);
            std::array<juce::dsp::Complex<float>, N> in {}, outC {};
            for (int i = 0; i < N; ++i) in[(size_t) i] = { k[(size_t) i], 0.0f };
            fft.perform(in.data(), outC.data(), false);
            const int bin = juce::jlimit(0, N / 2, (int) std::round(hz * N / sr));
            return vesper::dsp::gainToDb(std::abs(outC[(size_t) bin]));
        };

        std::array<vesper::dsp::ParametricEQ::Band, 6> bands {};
        bands[0] = { true, 0, 1000.0f, 6.0f, 1.0f, 0.0f };   // bell +6 dB @ 1 kHz
        bands[1] = { true, 3, 60.0f, 0.0f, 0.71f, 0.0f };    // HP @ 60 Hz
        std::array<float, N> k {};
        LPE::designFromBands(bands.data(), fs, k);

        const float at1k = (float) responseDb(k, 1000.0, fs);
        const float at8k = (float) responseDb(k, 8000.0, fs);
        const float at20 = (float) responseDb(k, 20.0, fs);
        check(std::abs(at1k - 6.0f) < 1.5f && std::abs(at8k) < 1.0f && at20 < -6.0f,
              "lp-eq: FIR realizes bell +6dB @1k, flat @8k, HP rolls off 20Hz");

        float maxAsym = 0.0f; // linear phase = symmetric about centre
        for (int d = 1; d < N / 2; ++d)
            maxAsym = juce::jmax(maxAsym,
                                 std::abs(k[(size_t) (N / 2 + d)] - k[(size_t) (N / 2 - d)]));
        check(maxAsym < 1.0e-4f, "lp-eq: designed FIR is linear-phase (symmetric)");

        vesper::dsp::LinearPhaseEQ lpe;
        lpe.prepare(ctx);
        const bool idle = lpe.latencySamples() == 0;
        lpe.setArmed(true);
        check(idle && lpe.latencySamples() == LPE::latency,
              "lp-eq: latency reported only while armed");
    }

    // ---- Match EQ: linear-phase FIR design + apply (D-032) ----
    {
        using MEQ = vesper::dsp::MatchEQ;
        constexpr int N = MEQ::fftSize, B = MEQ::bins;

        // realized magnitude response (dB) of a kernel at a bin, via forward FFT
        auto responseDb = [](const std::array<float, N>& k, int bin)
        {
            static juce::dsp::FFT fft(MEQ::order);
            std::array<juce::dsp::Complex<float>, N> in {}, out {};
            for (int i = 0; i < N; ++i) in[(size_t) i] = { k[(size_t) i], 0.0f };
            fft.perform(in.data(), out.data(), false);
            return vesper::dsp::gainToDb(std::abs(out[(size_t) bin]));
        };

        // A) flat curve -> perfect unit-delta kernel, flat response
        {
            std::array<float, B> curve {};
            std::array<float, N> k {};
            MEQ::designKernel(curve, k);
            const bool centreUnity = std::abs(k[(size_t) (N / 2)] - 1.0f) < 0.02f;
            float maxDev = 0.0f;
            for (int bin = 2; bin < B; ++bin) maxDev = juce::jmax(maxDev, std::abs(responseDb(k, bin)));
            check(centreUnity && maxDev < 0.5f,
                  "match: flat curve -> unit-delta kernel, flat response");
        }

        // B) a +6 dB bump around 1 kHz is realized there, flat far away
        {
            std::array<float, B> curve {};
            for (int i = 0; i < B; ++i)
            {
                const double hz = fs * i / N;
                const double x  = (hz - 1000.0) / 400.0;
                curve[(size_t) i] = 6.0f * (float) std::exp(-x * x); // smooth bump
            }
            std::array<float, N> k {};
            MEQ::designKernel(curve, k);
            const int binC = (int) std::round(1000.0 * N / fs);
            const int binF = (int) std::round(6000.0 * N / fs);
            check(std::abs(responseDb(k, binC) - 6.0f) < 2.0f && std::abs(responseDb(k, binF)) < 1.5f,
                  "match: +6 dB bump realized at 1 kHz, flat at 6 kHz");
        }

        // C) linear phase: designed FIR is symmetric about its centre
        {
            std::array<float, B> curve {};
            for (int i = 0; i < B; ++i) curve[(size_t) i] = 4.0f * std::sin(0.01f * (float) i);
            std::array<float, N> k {};
            MEQ::designKernel(curve, k);
            float maxAsym = 0.0f;
            for (int d = 1; d < N / 2; ++d)
                maxAsym = juce::jmax(maxAsym,
                                     std::abs(k[(size_t) (N / 2 + d)] - k[(size_t) (N / 2 - d)]));
            check(maxAsym < 1.0e-4f, "match: designed FIR is linear-phase (symmetric)");
        }

        // D2) A/B reference slots are independent (D-039)
        {
            vesper::dsp::MatchEQ meq;
            meq.prepare(ctx);
            juce::AudioBuffer<float> blk(2, 512);

            auto learnRefWith = [&] (int slot, double hz)
            {
                meq.setActiveReference(slot);
                meq.startLearn(true);
                for (int i = 0; i < 12; ++i)
                {
                    fillSine(blk, hz, fs, 0.5f);
                    meq.pushInput(blk);
                    meq.pumpLearn();
                }
                meq.stopLearn();
            };
            learnRefWith(0, 300.0);
            const bool aOnly = meq.referenceReady(0) && ! meq.referenceReady(1);
            learnRefWith(1, 4000.0);
            const bool bothNow = meq.referenceReady(0) && meq.referenceReady(1);

            // a source, then a match from each slot must differ (different refs)
            meq.startLearn(false);
            for (int i = 0; i < 12; ++i)
            {
                fillSine(blk, 1000.0, fs, 0.5f);
                meq.pushInput(blk);
                meq.pumpLearn();
            }
            meq.stopLearn();

            meq.setActiveReference(0); meq.computeMatch();
            const float aAt300 = meq.displayDbAtHz(300.0);
            meq.setActiveReference(1); meq.computeMatch();
            const float bAt300 = meq.displayDbAtHz(300.0);

            check(aOnly && bothNow && std::abs(aAt300 - bAt300) > 1.0f,
                  "match A/B: slots are independent and produce different curves");
        }

        // D) integration surface: latency toggles with active state; processing
        //    stays finite; and (via deterministic direct convolution) the flat
        //    kernel is a pure fftSize/2 delay of unit gain.
        {
            vesper::dsp::MatchEQ meq;
            meq.prepare(ctx);
            const bool zeroLatIdle = meq.latencySamples() == 0;

            std::array<float, B> raw {}; // flat correction
            meq.loadCurve(juce::Base64::toBase64(raw.data(), sizeof(float) * (size_t) B));
            meq.setParams(true, 100.0f, 0.0f, 12.0f);
            const bool latOn = meq.latencySamples() == MEQ::latency && meq.hasMatch();
            meq.setParams(false, 100.0f, 0.0f, 12.0f);
            const bool latOff = meq.latencySamples() == 0;
            meq.setParams(true, 100.0f, 0.0f, 12.0f);

            bool finite = true; // the convolution engine may still be loading — must never NaN
            juce::AudioBuffer<float> blk(2, 512);
            for (int s = 0; s < 8192; s += 512)
            {
                fillSine(blk, 500.0, fs, 0.4f);
                meq.process(blk);
                finite = finite && isFiniteBuffer(blk);
            }

            // direct linear convolution: an impulse in -> peak at N/2 with unit gain
            std::array<float, N> k {};
            MEQ::designKernel(raw, k); // flat
            std::vector<float> y(4096 + N, 0.0f);
            for (int t = 0; t < N; ++t) y[(size_t) (100 + t)] += k[(size_t) t]; // x[100]=1
            int pk = 0; float pv = 0.0f;
            for (int i = 0; i < (int) y.size(); ++i)
                if (std::abs(y[(size_t) i]) > pv) { pv = std::abs(y[(size_t) i]); pk = i; }
            const bool delayOk = pk == 100 + MEQ::latency && std::abs(pv - 1.0f) < 0.02f;

            check(zeroLatIdle && latOn && latOff && finite && delayOk,
                  "match: integration — latency toggles, finite, flat kernel = N/2 delay");
        }
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
