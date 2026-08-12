#pragma once
/*  EQFilter.h — 6-band parametric EQ (RBJ biquads, double state) and the
    multi-model creative filter (TPT SVF, ZDF ladder, comb, formant). */

#include "Core.h"

namespace vesper::dsp {

// --------------------------------------------------------------------- EQ --
/* 6 bands, each optionally *dynamic* (docs DECISIONS D-018): the band's
   bandpass envelope is compared against its own slow-moving reference, and
   the excess drives a gain shift of up to ±dyn dB. No threshold parameter —
   the reference adapts to the material. Detection is chunk-rate (32 smp),
   coefficient updates are slew-guarded, so moves never zipper. */
class ParametricEQ
{
public:
    static constexpr int numBands = 6;
    static constexpr int chunk    = 32;

    struct Band
    {
        bool  on = false;
        int   type = 0;      // Bell, LowShelf, HighShelf, HP, LP, Notch
        float freq = 1000.0f, gain = 0.0f, q = 0.71f;
        float dyn = 0.0f;    // dynamic amount, dB (0 = static band)
        /* Cache identity, not tolerance — exact compare is the intent. */
        bool operator== (const Band& o) const noexcept
        {
            return on == o.on && type == o.type
                && juce::exactlyEqual(freq, o.freq)
                && juce::exactlyEqual(gain, o.gain)
                && juce::exactlyEqual(q, o.q)
                && juce::exactlyEqual(dyn, o.dyn);
        }
    };

    void prepare(const Context& ctx)
    {
        fs = ctx.sampleRate;
        for (auto& b : cached) b = Band { true, -1, 0, 0, 0, 0 }; // force refresh
        for (auto& d : dynState)
        {
            d.det.prepare(fs);
            d.env.prepare(fs);
            d.env.setTimes(4.0, 90.0);
            d.ref.setTau(600.0, fs);
        }
        reset();
    }

    void setBand(int idx, const Band& b)
    {
        if (cached[(size_t) idx] == b) return;
        const bool retune = ! juce::exactlyEqual(cached[(size_t) idx].freq, b.freq);
        cached[(size_t) idx] = b;
        if (retune)
            dynState[(size_t) idx].det.set(juce::jlimit(30.0f, 18000.0f, b.freq), 0.5);
        applyCoefficients(idx, b.gain + dynState[(size_t) idx].smoothed);
    }

    void process(juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        auto* L = buf.getWritePointer(0);
        auto* R = buf.getWritePointer(1);

        for (int start = 0; start < n; start += chunk)
        {
            const int m = juce::jmin(chunk, n - start);

            // 1) detectors on the pre-EQ mono sum; dynamic gains at chunk rate
            for (int b = 0; b < numBands; ++b)
            {
                auto& band = cached[(size_t) b];
                auto& d    = dynState[(size_t) b];
                if (! band.on || std::abs(band.dyn) < 0.05f)
                {
                    if (std::abs(d.applied - band.gain) > 0.01f) // knob returned to 0
                    {
                        d.smoothed = 0.0f;
                        applyCoefficients(b, band.gain);
                    }
                    dynMeterDb[(size_t) b].store(0.0f, std::memory_order_relaxed);
                    continue;
                }

                double env = 0.0;
                for (int i = 0; i < m; ++i)
                {
                    double lp, bp, hp;
                    d.det.process(((double) L[start + i] + R[start + i]) * 0.5, lp, bp, hp);
                    env = d.env.process(bp);
                }
                const float envDb = gainToDb((float) env);
                const float refDb = gainToDb((float) juce::jmax(1.0e-6, d.ref.process(env)));
                const float over  = envDb - refDb - 3.0f;  // 3 dB grace above average
                const float amt   = juce::jlimit(0.0f, 1.0f, over / 9.0f);
                d.smoothed += (band.dyn * amt - d.smoothed) * 0.35f;
                dynMeterDb[(size_t) b].store(d.smoothed, std::memory_order_relaxed);
                const float eff = band.gain + d.smoothed;
                if (std::abs(eff - d.applied) > 0.15f)
                    applyCoefficients(b, eff);
            }

            // 2) filter the chunk
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* p = ch == 0 ? L : R;
                for (int b = 0; b < numBands; ++b)
                {
                    if (! cached[(size_t) b].on) continue;
                    auto& f = filt[(size_t) ch][(size_t) b];
                    for (int i = 0; i < m; ++i)
                        p[start + i] = (float) f.process(p[start + i]);
                }
            }
        }
    }

    void reset()
    {
        for (auto& ch : filt)
            for (auto& f : ch) f.reset();
        for (auto& d : dynState)
        {
            d.det.reset();
            d.env.reset();
            d.ref.reset();
            d.smoothed = 0.0f;
            d.applied  = 1.0e9f; // force first apply
        }
    }

    /* Composite magnitude response in dB — used by the UI curve. Static so the
       editor can compute it from parameter values without touching DSP state. */
    static float responseDbAt(const Band* bands, double sampleRate, double freq)
    {
        static const Biquad::Type map[] = { Biquad::Type::bell, Biquad::Type::lowShelf,
                                            Biquad::Type::highShelf, Biquad::Type::highPass,
                                            Biquad::Type::lowPass, Biquad::Type::notch };
        double mag = 1.0;
        for (int b = 0; b < numBands; ++b)
        {
            if (! bands[b].on) continue;
            Biquad tmp;
            tmp.setCoefficients(map[juce::jlimit(0, 5, bands[b].type)], sampleRate,
                                bands[b].freq, bands[b].q, bands[b].gain);
            mag *= tmp.magnitudeAt(freq, sampleRate);
        }
        return gainToDb((float) mag);
    }

private:
    void applyCoefficients(int idx, float effectiveGain)
    {
        static const Biquad::Type map[] = { Biquad::Type::bell, Biquad::Type::lowShelf,
                                            Biquad::Type::highShelf, Biquad::Type::highPass,
                                            Biquad::Type::lowPass, Biquad::Type::notch };
        const auto& b = cached[(size_t) idx];
        for (int ch = 0; ch < 2; ++ch)
            filt[(size_t) ch][(size_t) idx].setCoefficients(
                map[juce::jlimit(0, 5, b.type)], fs, b.freq, b.q, effectiveGain);
        dynState[(size_t) idx].applied = effectiveGain;
    }

    struct DynState
    {
        SVF det;                 // bandpass detector at the band frequency
        EnvelopeFollower env;    // fast band envelope
        OnePole ref;             // slow adaptive reference
        float smoothed = 0.0f;   // current dynamic offset, dB
        float applied  = 1.0e9f; // gain the coefficients were last built with
    };

    double fs = 48000.0;
    std::array<Band, numBands> cached;
    std::array<std::array<Biquad, numBands>, 2> filt;
    std::array<DynState, numBands> dynState;

public:
    /* Current dynamic offset per band, dB — the UI's "listening" meters. */
    std::array<std::atomic<float>, numBands> dynMeterDb {};
};

// -------------------------------------------------------------- ZDF ladder --
/* 4-pole transistor ladder (Huovilainen-style, tanh feedback), double state. */
class LadderFilter
{
public:
    void prepare(double sampleRate) { fs = sampleRate; reset(); }

    void set(double cutoff, double res, double extraDrive = 1.0) noexcept
    {
        cutoff = juce::jlimit(20.0, fs * 0.45, cutoff);
        const double wc = std::tan(juce::MathConstants<double>::pi * cutoff / fs);
        G = wc / (1.0 + wc); // TPT one-pole gain
        k = 4.0 * juce::jlimit(0.0, 1.05, res);
        drive = extraDrive;
    }

    double process(double x) noexcept
    {
        const double fb = z[3];
        double u = std::tanh(drive * (x - k * fb));
        for (auto& s : z)
        {
            const double v = (u - s) * G; // TPT integrator
            const double y = v + s;
            s = flushDenorm(y + v);
            u = y;
        }
        return u;
    }

    void reset() { z.fill(0.0); }

private:
    double fs = 48000.0, G = 0.1, k = 0.0, drive = 1.0;
    std::array<double, 4> z {};
};

// ---------------------------------------------------------- Creative filter --
class CreativeFilter
{
public:
    enum Type { clean12 = 0, clean24, ladder, acid, comb, formant };

    void prepare(const Context& ctx)
    {
        fs = ctx.sampleRate;
        for (auto& s : svf1) s.prepare(fs);
        for (auto& s : svf2) s.prepare(fs);
        for (auto& l : lad)  l.prepare(fs);
        for (auto& c : combLine) c.prepare(fs, 0.06);
        for (auto& f : formantA) f.prepare(fs);
        for (auto& f : formantB) f.prepare(fs);
        cutoffSm.reset(fs, 0.008);
        reset();
    }

    void set(int typeIn, float cutoffHz, float resPct, float driveDb) noexcept
    {
        type = typeIn;
        cutoffSm.setTargetValue(cutoffHz);
        res   = resPct * 0.01f;
        drive = dbToGain(driveDb);
        softClip = driveDb > 0.05f;   // clean models stay linear at unity drive

        /* D-047 — resonance level compensation. Every model concentrates
           energy at the cutoff as resonance rises, so without this the
           output level climbs with the knob: measured +11.8 dB (clean24),
           +8.5 dB (comb), +7.6 dB (acid) on broadband material. That is
           what made a resonant sweep painful rather than musical. The
           constants below are calibrated against the filter-gain table in
           the test suite, which fails if any model/resonance combination
           amplifies by more than +6 dB. Timbre is untouched — only level. */
        static constexpr float k[6] = { 2.10f,   // clean12
                                        2.30f,   // clean24
                                        0.95f,   // ladder
                                        1.10f,   // acid
                                        0.30f,   // comb (mostly a flat trim)
                                        0.85f }; // formant
        static constexpr float base[6] = { 1.0f, 1.0f, 1.0f, 1.0f, 0.42f, 1.0f };
        const int t = juce::jlimit(0, 5, typeIn);
        resComp = base[t] / (1.0f + res * res * k[t]);
    }

    void process(juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        auto* l = buf.getWritePointer(0);
        auto* r = buf.getWritePointer(1);

        for (int i = 0; i < n; ++i)
        {
            const double fc = (double) cutoffSm.getNextValue();
            switch (type)
            {
                case clean12:
                case clean24:
                {
                    svf1[0].set(fc, res); svf1[1].set(fc, res);
                    double lp, bp, hp;
                    svf1[0].process(l[i] * drive, lp, bp, hp); l[i] = (float) lp;
                    svf1[1].process(r[i] * drive, lp, bp, hp); r[i] = (float) lp;
                    if (type == clean24)
                    {
                        svf2[0].set(fc, res * 0.5f); svf2[1].set(fc, res * 0.5f);
                        svf2[0].process(l[i], lp, bp, hp); l[i] = (float) lp;
                        svf2[1].process(r[i], lp, bp, hp); r[i] = (float) lp;
                    }
                    /* D-047: the clean models have no nonlinearity of their
                       own, so Drive was pure level and resonance stacked on
                       top of it (measured +11.8 dB). Compensate the resonant
                       lift always; soft-clip only once Drive is actually
                       engaged, so "clean" stays bit-clean at 0 dB drive and
                       Drive makes harmonics instead of headroom. */
                    if (softClip)
                    {
                        l[i] = fastTanh(l[i] * resComp);
                        r[i] = fastTanh(r[i] * resComp);
                    }
                    else
                    {
                        l[i] *= resComp;
                        r[i] *= resComp;
                    }
                    break;
                }
                case ladder:
                case acid:
                {
                    const double rr = type == acid ? res * 1.05 : res * 0.95;
                    const double dd = type == acid ? drive * 1.8 : drive;
                    lad[0].set(fc, rr, dd); lad[1].set(fc, rr, dd);
                    /* D-047: this used to BOOST with resonance
                       (1 + rr*0.45), which is backwards — the ladder already
                       concentrates energy at the cutoff, so the boost made a
                       resonant sweep painful. Compensate downward instead;
                       the tanh inside the ladder still gives the growl. */
                    l[i] = (float) lad[0].process(l[i]) * resComp;
                    r[i] = (float) lad[1].process(r[i]) * resComp;
                    break;
                }
                case comb:
                {
                    const double dSamp = juce::jlimit(2.0, fs * 0.05, fs / juce::jmax(30.0, fc));
                    const float fb = 0.55f + res * 0.38f; // cap 0.93: resonant, never ringing-forever
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        float& x = ch == 0 ? l[i] : r[i];
                        const float dl = combLine[(size_t) ch].readHermite(dSamp);
                        const float y  = fastTanh((x * drive + dl * fb) * 0.9f);
                        combLine[(size_t) ch].write(y);
                        x = y * resComp; // D-047: comb ran ~+8 dB at every setting
                    }
                    break;
                }
                case formant:
                {
                    // Morph A(ah)->E->I(ee) with cutoff; two bandpasses per voice
                    const double t  = juce::jlimit(0.0, 1.0, (std::log2(fc) - 6.0) / 8.0);
                    const double f1 = juce::jmap(t, 700.0, 280.0);
                    const double f2 = juce::jmap(t, 1150.0, 2250.0);
                    const double q  = 4.0 + res * 10.0;
                    double lp, bp1, bp2, hp;
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        float& x = ch == 0 ? l[i] : r[i];
                        formantA[(size_t) ch].set(f1, 1.0 - 1.0 / q);
                        formantB[(size_t) ch].set(f2, 1.0 - 1.0 / q);
                        formantA[(size_t) ch].process(x * drive, lp, bp1, hp);
                        formantB[(size_t) ch].process(x * drive, lp, bp2, hp);
                        x = (float) (bp1 * 1.2 + bp2 * 0.8) * resComp; // D-047
                    }
                    break;
                }
            }
        }
    }

    void reset()
    {
        for (auto& s : svf1) s.reset();
        for (auto& s : svf2) s.reset();
        for (auto& lf : lad) lf.reset();
        for (auto& c : combLine) c.clear();
        for (auto& f : formantA) f.reset();
        for (auto& f : formantB) f.reset();
    }

private:
    double fs = 48000.0;
    int    type = 2;
    float  res = 0.15f, drive = 1.0f, resComp = 1.0f;
    bool   softClip = false;
    SmoothLin cutoffSm;
    std::array<SVF, 2> svf1, svf2, formantA, formantB;
    std::array<LadderFilter, 2> lad;
    std::array<DelayLine, 2> combLine;
};

} // namespace vesper::dsp
