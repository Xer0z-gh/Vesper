#pragma once
/*  Dynamics.h — compressor (down + upward, 3 characters), transient designer,
    and a lookahead brickwall limiter. Detectors run in double precision; all
    gain moves are smoothed so parameter rides never zipper. */

#include "Core.h"
#include "Saturation.h"
#include <atomic>

namespace vesper::dsp {

// ------------------------------------------------------------- Compressor --
class Compressor
{
public:
    void prepare(const Context& ctx)
    {
        fs = ctx.sampleRate;
        env.prepare(fs);
        relSlow.setTau(900.0, fs);
        makeupSm.reset(fs, 0.03); mixSm.reset(fs, 0.03);
        reset();
    }

    /* Host-buffer sidechain tap; pointers are valid for the current block
       only, set by the processor every processBlock (D-021). */
    void setSidechain(const float* l, const float* r) noexcept { scL = l; scR = r; }

    void set(float threshDb, float ratio, float attackMs, float releaseMs, float kneeDb,
             float makeupDb, float upwardDb, float mixPct, int detector, int character,
             bool sidechainOn = false) noexcept
    {
        useSC = sidechainOn;
        thresh = threshDb; knee = juce::jmax(0.01f, kneeDb);
        slope  = 1.0f - 1.0f / juce::jmax(1.0f, ratio);
        upward = upwardDb;
        chara  = character;
        env.mode = detector == 1 ? EnvelopeFollower::Mode::rms : EnvelopeFollower::Mode::peak;

        // Character tweaks the detector, not the curve.
        float atk = attackMs, rel = releaseMs;
        if (character == 1) { atk *= 0.6f; }              // Punchy: faster grab
        if (character == 2) { atk *= 1.4f; rel *= 1.2f; } // Vintage: lazier
        env.setTimes(atk, rel);
        makeupSm.setTargetValue(dbToGain(makeupDb));
        mixSm.setTargetValue(mixPct * 0.01f);
    }

    void process(juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        auto* l = buf.getWritePointer(0);
        auto* r = buf.getWritePointer(1);
        float grMin = 1.0f;

        const bool sc = useSC && scL != nullptr;
        for (int i = 0; i < n; ++i)
        {
            const double link = sc
                ? juce::jmax(std::abs((double) scL[i]), std::abs((double) scR[i]))
                : juce::jmax(std::abs((double) l[i]), std::abs((double) r[i]));
            double lvl = env.process(link);
            if (chara == 2) // Vintage: program-dependent release (2-stage)
                lvl = 0.7 * lvl + 0.3 * relSlow.process(lvl);

            const float lvlDb = gainToDb((float) lvl);

            // Downward, soft knee
            float overDb = lvlDb - thresh;
            float gr = 0.0f;
            if (overDb > -knee * 0.5f)
            {
                if (overDb < knee * 0.5f)
                {
                    const float t = overDb + knee * 0.5f;
                    gr = -slope * (t * t) / (2.0f * knee);
                }
                else
                    gr = -slope * overDb;
            }
            // Upward: gentle lift below threshold, capped, fades out near silence
            if (upward > 0.01f && lvlDb < thresh)
            {
                const float below = thresh - lvlDb;
                const float lift  = juce::jmin(upward, below * 0.5f);
                const float floorFade = juce::jlimit(0.0f, 1.0f, (lvlDb + 70.0f) / 20.0f);
                gr += lift * floorFade;
            }

            const float g      = dbToGain(gr);
            const float makeup = makeupSm.getNextValue();
            const float mix    = mixSm.getNextValue();
            grMin = juce::jmin(grMin, g);

            l[i] = l[i] + (l[i] * g * makeup - l[i]) * mix;
            r[i] = r[i] + (r[i] * g * makeup - r[i]) * mix;
        }
        grDb.store(gainToDb(grMin), std::memory_order_relaxed);
    }

    void reset() { env.reset(); relSlow.reset(); }

    std::atomic<float> grDb { 0.0f }; // most-negative GR in last block, for UI

private:
    double fs = 48000.0;
    float  thresh = -18.0f, knee = 9.0f, slope = 0.66f, upward = 0.0f;
    int    chara = 0;
    bool   useSC = false;
    const float* scL = nullptr;
    const float* scR = nullptr;
    EnvelopeFollower env;
    OnePole relSlow;
    SmoothLin makeupSm, mixSm;
};

// ------------------------------------------------------ Transient designer --
class TransientDesigner
{
public:
    void prepare(const Context& ctx)
    {
        fs = ctx.sampleRate;
        fast.prepare(fs); slow.prepare(fs);
        setSpeed(1);
        reset();
    }

    void set(float attackPct, float sustainPct, int speed) noexcept
    {
        attackAmt  = attackPct * 0.01f;
        sustainAmt = sustainPct * 0.01f;
        if (speed != speedIdx) { speedIdx = speed; setSpeed(speed); }
    }

    void process(juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        auto* l = buf.getWritePointer(0);
        auto* r = buf.getWritePointer(1);
        for (int i = 0; i < n; ++i)
        {
            const double x  = juce::jmax(std::abs((double) l[i]), std::abs((double) r[i]));
            const double ef = fast.process(x);
            const double es = slow.process(x);
            const double diff = juce::jmax(0.0, ef - es);            // transient portion
            const double sus  = juce::jmax(0.0, es - diff * 0.5);    // body portion
            // Map to gain: ±100% ≈ ±12 dB on each component
            const float gAtk = dbToGain((float) (attackAmt  * 12.0 * juce::jmin(1.0, diff * 6.0)));
            const float gSus = dbToGain((float) (sustainAmt * 12.0 * juce::jmin(1.0, sus  * 4.0)));
            const float g = gAtk * gSus;
            l[i] *= g; r[i] *= g;
        }
    }

    void reset() { fast.reset(); slow.reset(); }

private:
    void setSpeed(int s)
    {
        static const double fastAtk[] = { 0.1, 0.4, 1.2 };
        static const double slowAtk[] = { 8.0, 22.0, 60.0 };
        fast.setTimes(fastAtk[s], 45.0);
        slow.setTimes(slowAtk[s], 220.0);
    }

    double fs = 48000.0;
    float  attackAmt = 0.0f, sustainAmt = 0.0f;
    int    speedIdx = 1;
    EnvelopeFollower fast, slow;
};

// ---------------------------------------------------------------- Limiter --
/* Fixed 1.5 ms lookahead brickwall. The gain envelope attacks over the full
   lookahead window (no overshoot), release is user-set, and an optional soft
   clip catches residual energy at the ceiling.

   True-peak mode (D-019): the detector reconstructs 3 inter-sample points
   per sample with a 16-tap windowed-sinc interpolator, so the gain envelope
   answers the analog waveform, not the sample grid. Detection sits 3
   samples behind the input — well inside the 72-sample lookahead. */
class Limiter
{
public:
    static constexpr double lookaheadMs = 1.5;

    void prepare(const Context& ctx)
    {
        fs = ctx.sampleRate;
        lookahead = (int) std::ceil(fs * lookaheadMs * 0.001);
        for (auto& d : delay) d.prepare(fs, lookaheadMs * 0.002 + 0.001);
        gainSm.reset(fs, 0.02);
        attackCoeff = std::exp(-1.0 / (double) lookahead);

        // 16-tap Blackman-windowed sinc at fractions 1/4, 1/2, 3/4
        for (int ph = 0; ph < 3; ++ph)
        {
            const double frac = 0.25 * (ph + 1);
            double sum = 0.0;
            for (int t = 0; t < tpLen; ++t)
            {
                const double x = (double) t - (tpLen / 2 - 1) - frac;
                const double sinc = std::abs(x) < 1.0e-9
                                        ? 1.0
                                        : std::sin(juce::MathConstants<double>::pi * x)
                                              / (juce::MathConstants<double>::pi * x);
                const double w = 0.42
                               - 0.5  * std::cos(juce::MathConstants<double>::twoPi * t / (tpLen - 1.0))
                               + 0.08 * std::cos(2.0 * juce::MathConstants<double>::twoPi * t / (tpLen - 1.0));
                tpTaps[(size_t) ph][(size_t) t] = sinc * w;
                sum += tpTaps[(size_t) ph][(size_t) t];
            }
            for (auto& tap : tpTaps[(size_t) ph]) tap /= sum;
        }
        reset();
    }

    void set(float gainDb, float ceilingDb, float releaseMs,
             bool softClipOn, bool truePeakOn) noexcept
    {
        gainSm.setTargetValue(dbToGain(gainDb));
        ceiling = dbToGain(ceilingDb);
        relCoeff = std::exp(-1.0 / (0.001 * juce::jmax(1.0f, releaseMs) * fs));
        clip = softClipOn;
        truePeak = truePeakOn;
    }

    int latencySamples() const noexcept { return lookahead; }

    void process(juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        auto* l = buf.getWritePointer(0);
        auto* r = buf.getWritePointer(1);
        float grMin = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            const float g  = gainSm.getNextValue();
            const float xl = l[i] * g, xr = r[i] * g;
            delay[0].write(xl); delay[1].write(xr);

            const double peak = truePeak
                                    ? juce::jmax(truePeakOf(0, xl), truePeakOf(1, xr))
                                    : juce::jmax(std::abs((double) xl), std::abs((double) xr));
            const double wanted = peak > ceiling ? ceiling / peak : 1.0;

            // Attack: fall fast enough to be there when the peak exits lookahead.
            // Release: user one-pole back to unity.
            if (wanted < gainEnv)
                gainEnv = wanted + attackCoeff * (gainEnv - wanted);
            else
                gainEnv = wanted + relCoeff * (gainEnv - wanted);
            gainEnv = flushDenorm(juce::jmin(gainEnv, 1.0));

            float yl = delay[0].readLinear((double) lookahead) * (float) gainEnv;
            float yr = delay[1].readLinear((double) lookahead) * (float) gainEnv;
            if (clip)
            {
                yl = softClipTo(yl, ceiling);
                yr = softClipTo(yr, ceiling);
            }
            l[i] = yl; r[i] = yr;
            grMin = juce::jmin(grMin, (float) gainEnv);
        }
        grDb.store(gainToDb(grMin), std::memory_order_relaxed);
    }

    void reset()
    {
        for (auto& d : delay) d.clear();
        gainEnv = 1.0;
        for (auto& h : hist) h.fill(0.0);
        histIdx.fill(0);
    }

    std::atomic<float> grDb { 0.0f };

private:
    /* Push x into the channel's 8-sample ring; return the true peak around
       the window centre (the centre sample + 3 reconstructed points). */
    double truePeakOf(int ch, float x) noexcept
    {
        constexpr int mask = tpLen - 1;
        auto& h   = hist[(size_t) ch];
        auto& idx = histIdx[(size_t) ch];
        idx = (idx + 1) & mask;
        h[(size_t) idx] = x;

        double best = juce::jmax(std::abs((double) x),
                                 std::abs(h[(size_t) ((idx - (tpLen / 2 - 1)) & mask)]));
        for (int ph = 0; ph < 3; ++ph)
        {
            double v = 0.0;
            for (int t = 0; t < tpLen; ++t)
                v += tpTaps[(size_t) ph][(size_t) t]
                     * h[(size_t) ((idx - (tpLen - 1) + t) & mask)];
            best = juce::jmax(best, std::abs(v));
        }
        return best;
    }

    double fs = 48000.0;
    int    lookahead = 72;
    float  ceiling = 0.966f;
    bool   clip = true, truePeak = true;
    double gainEnv = 1.0, attackCoeff = 0.99, relCoeff = 0.999;
    SmoothLin gainSm;
    std::array<DelayLine, 2> delay;
    static constexpr int tpLen = 16;
    std::array<std::array<double, tpLen>, 2> hist {};
    std::array<std::array<double, tpLen>, 3> tpTaps {};
    std::array<int, 2> histIdx {};
};

} // namespace vesper::dsp
