#pragma once
/*  DynamicsPlus.h — gate/expander and a 3-band Linkwitz-Riley multiband
    compressor. Same contract as every Vesper module: prepare(Context),
    set(...) from cached atomics once per block, process(AudioBuffer). */

#include "Core.h"
#include <atomic>

namespace vesper::dsp {

// ------------------------------------------------------------------- Gate --
/* Downward gate / expander with hysteresis and hold. The detector is a fast
   peak follower; open/close moves ride separate attack/release one-poles so
   short attacks never click. */
class Gate
{
public:
    void prepare(const Context& ctx)
    {
        fs = ctx.sampleRate;
        det.prepare(fs);
        det.setTimes(0.05, 8.0); // detector is fast; musical timing lives in the gain smoothing
        reset();
    }

    /* Host-buffer sidechain tap; block-scoped pointers (D-021). */
    void setSidechain(const float* l, const float* r) noexcept { scL = l; scR = r; }

    void set(float threshDb, float rangeDb, float attackMs, float releaseMs,
             float holdMs, int modeIn, bool sidechainOn = false) noexcept
    {
        useSC = sidechainOn;
        openThresh  = dbToGain(threshDb);
        closeThresh = dbToGain(threshDb - 3.0f); // 3 dB hysteresis
        threshDbS   = threshDb;
        floorGain   = dbToGain(rangeDb);
        aAtk = std::exp(-1.0 / (0.001 * juce::jmax(0.05f, attackMs) * fs));
        aRel = std::exp(-1.0 / (0.001 * juce::jmax(1.0f, releaseMs) * fs));
        holdSamples = (int) (holdMs * 0.001 * fs);
        mode = modeIn;
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
            const double lvl  = det.process(link);

            double target;
            if (mode == 0) // Gate: binary with hysteresis + hold
            {
                if (lvl >= (open ? closeThresh : openThresh))
                {
                    open = true;
                    holdCount = holdSamples;
                }
                else if (open && --holdCount <= 0)
                    open = false;
                target = open ? 1.0 : floorGain;
            }
            else // Expander: 2:1 downward below threshold, floored at range
            {
                const float lvlDb = gainToDb((float) lvl);
                const float under = threshDbS - lvlDb;
                target = under <= 0.0f ? 1.0
                                       : juce::jmax((double) floorGain, (double) dbToGain(-under));
            }

            // Opening rides the attack pole, closing rides the release pole.
            gain = flushDenorm(target + (target > gain ? aAtk : aRel) * (gain - target));

            l[i] *= (float) gain;
            r[i] *= (float) gain;
            grMin = juce::jmin(grMin, (float) gain);
        }
        grDb.store(gainToDb(grMin), std::memory_order_relaxed);
    }

    void reset()
    {
        det.reset();
        gain = 1.0; open = false; holdCount = 0;
    }

    std::atomic<float> grDb { 0.0f };

private:
    double fs = 48000.0;
    double openThresh = 0.005, closeThresh = 0.0035, floorGain = 0.001;
    float  threshDbS = -45.0f;
    double aAtk = 0.99, aRel = 0.999, gain = 1.0;
    int    holdSamples = 0, holdCount = 0, mode = 0;
    bool   open = false, useSC = false;
    const float* scL = nullptr;
    const float* scR = nullptr;
    EnvelopeFollower det;
};

// -------------------------------------------------------- Multiband comp ---
/* 3-band Linkwitz-Riley (24 dB/oct) split. The low band passes through a
   matched allpass at the upper crossover so the recombined sum stays flat.
   Bands share attack/release; each has threshold, ratio and output gain. */
class MultibandComp
{
public:
    void prepare(const Context& ctx)
    {
        fs = ctx.sampleRate;
        for (auto& e : env) { e.prepare(fs); e.mode = EnvelopeFollower::Mode::rms; }
        setCrossovers(150.0f, 2500.0f, true);
        for (auto& g : gainSm) g.reset(fs, 0.02);
        reset();
    }

    void set(float xLowHz, float xHighHz,
             float loT, float loR, float loG,
             float midT, float midR, float midG,
             float hiT, float hiR, float hiG,
             float attackMs, float releaseMs) noexcept
    {
        setCrossovers(xLowHz, xHighHz, false);
        thresh[0] = loT;  thresh[1] = midT;  thresh[2] = hiT;
        slope[0] = 1.0f - 1.0f / juce::jmax(1.0f, loR);
        slope[1] = 1.0f - 1.0f / juce::jmax(1.0f, midR);
        slope[2] = 1.0f - 1.0f / juce::jmax(1.0f, hiR);
        gainSm[0].setTargetValue(dbToGain(loG));
        gainSm[1].setTargetValue(dbToGain(midG));
        gainSm[2].setTargetValue(dbToGain(hiG));
        for (auto& e : env) e.setTimes(attackMs, releaseMs);
    }

    void process(juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        auto* l = buf.getWritePointer(0);
        auto* r = buf.getWritePointer(1);
        std::array<float, 3> grMin { 1.0f, 1.0f, 1.0f };

        for (int i = 0; i < n; ++i)
        {
            std::array<double, 3> bl, br;
            splitSample(0, l[i], bl);
            splitSample(1, r[i], br);

            float outL = 0.0f, outR = 0.0f;
            for (int b = 0; b < 3; ++b)
            {
                const double link = juce::jmax(std::abs(bl[(size_t) b]), std::abs(br[(size_t) b]));
                const float lvlDb = gainToDb((float) env[(size_t) b].process(link));

                // 6 dB soft knee downward
                const float overDb = lvlDb - thresh[(size_t) b];
                float grDbV = 0.0f;
                if (overDb > -3.0f)
                    grDbV = overDb < 3.0f
                                ? -slope[(size_t) b] * ((overDb + 3.0f) * (overDb + 3.0f)) / 12.0f
                                : -slope[(size_t) b] * overDb;

                const float g = dbToGain(grDbV) * gainSm[(size_t) b].getNextValue();
                grMin[(size_t) b] = juce::jmin(grMin[(size_t) b], dbToGain(grDbV));
                outL += (float) bl[(size_t) b] * g;
                outR += (float) br[(size_t) b] * g;
            }
            l[i] = outL; r[i] = outR;
        }
        for (int b = 0; b < 3; ++b)
            bandGrDb[(size_t) b].store(gainToDb(grMin[(size_t) b]), std::memory_order_relaxed);
    }

    void reset()
    {
        for (auto& ch : split) for (auto& f : ch) f.reset();
        for (auto& e : env) e.reset();
    }

    std::array<std::atomic<float>, 3> bandGrDb { 0.0f, 0.0f, 0.0f };

private:
    /* Filters per channel:
       [0][1] LP2@xLow  x2  -> low     [2][3] HP2@xLow x2 -> rest
       [4][5] LP2@xHigh x2  -> mid     [6][7] HP2@xHigh x2 -> high
       [8][9] AP2@xHigh x2 on the low band (phase match with mid/high split) */
    void setCrossovers(float xLow, float xHigh, bool force)
    {
        xHigh = juce::jmax(xHigh, xLow * 1.5f);
        if (! force && std::abs(xLow - lastLow) < 0.5f && std::abs(xHigh - lastHigh) < 0.5f)
            return;
        lastLow = xLow; lastHigh = xHigh;
        constexpr double q = 0.70710678;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = split[(size_t) ch];
            f[0].setCoefficients(Biquad::Type::lowPass,  fs, xLow,  q, 0.0);
            f[1].setCoefficients(Biquad::Type::lowPass,  fs, xLow,  q, 0.0);
            f[2].setCoefficients(Biquad::Type::highPass, fs, xLow,  q, 0.0);
            f[3].setCoefficients(Biquad::Type::highPass, fs, xLow,  q, 0.0);
            f[4].setCoefficients(Biquad::Type::lowPass,  fs, xHigh, q, 0.0);
            f[5].setCoefficients(Biquad::Type::lowPass,  fs, xHigh, q, 0.0);
            f[6].setCoefficients(Biquad::Type::highPass, fs, xHigh, q, 0.0);
            f[7].setCoefficients(Biquad::Type::highPass, fs, xHigh, q, 0.0);
            f[8].setCoefficients(Biquad::Type::allPass,  fs, xHigh, q, 0.0);
            f[9].setCoefficients(Biquad::Type::allPass,  fs, xHigh, q, 0.0);
        }
    }

    void splitSample(int ch, float x, std::array<double, 3>& bands)
    {
        auto& f = split[(size_t) ch];
        const double low  = f[1].process(f[0].process(x));
        const double rest = f[3].process(f[2].process(x));
        bands[0] = f[9].process(f[8].process(low)); // phase-matched low
        bands[1] = f[5].process(f[4].process(rest));
        bands[2] = f[7].process(f[6].process(rest));
    }

    double fs = 48000.0;
    float  lastLow = -1.0f, lastHigh = -1.0f;
    std::array<float, 3> thresh { -20.0f, -20.0f, -20.0f };
    std::array<float, 3> slope { 0.5f, 0.5f, 0.5f };
    std::array<std::array<Biquad, 10>, 2> split;
    std::array<EnvelopeFollower, 3> env;
    std::array<SmoothLin, 3> gainSm;
};

} // namespace vesper::dsp
