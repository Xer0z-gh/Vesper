#pragma once
/*  TimeFX.h — stereo delay (digital / tape / analog color models, ping-pong,
    diffusion) and an 8-line FDN reverb with shimmer. */

#include "Core.h"
#include "Saturation.h"

namespace vesper::dsp {

// ------------------------------------------------------------ Allpass unit --
class Allpass
{
public:
    void prepare(double fs, double maxMs) { line.prepare(fs, maxMs * 0.001 + 0.005); }
    void set(double delaySamples, float gain) noexcept { d = delaySamples; g = gain; }
    float process(float x) noexcept
    {
        const float z = line.readLinear(d);
        const float y = -g * x + z;
        line.write(flushDenorm(x + g * y));
        return y;
    }
    void clear() { line.clear(); }

private:
    DelayLine line;
    double d = 100.0;
    float g = 0.6f;
};

// ---------------------------------------------------------------- Delay ----
class StereoDelay
{
public:
    void prepare(const Context& ctx)
    {
        fs = ctx.sampleRate;
        for (auto& l : line) l.prepare(fs, 2.2);
        for (int ch = 0; ch < 2; ++ch)
        {
            lo[(size_t) ch].setCoefficients(Biquad::Type::highPass, fs, 120.0, 0.71, 0.0);
            hi[(size_t) ch].setCoefficients(Biquad::Type::lowPass, fs, 9000.0, 0.71, 0.0);
            diff[(size_t) ch][0].prepare(fs, 12.0);
            diff[(size_t) ch][1].prepare(fs, 23.0);
            diff[(size_t) ch][0].set(fs * 0.0071, 0.62f);
            diff[(size_t) ch][1].set(fs * 0.0137, 0.58f);
        }
        wowLFO.prepare(fs);
        timeSlew[0].setTau(120.0, fs); timeSlew[1].setTau(120.0, fs);
        mixSm.reset(fs, 0.03); fbSm.reset(fs, 0.03); widthSm.reset(fs, 0.03);
        reset();
    }

    void set(bool sync, float timeMs, int division, double bpm, float feedbackPct,
             bool pingpong, int color, float wowPct, float locutHz, float hicutHz,
             float diffusePct, float widthPct, float mixPct) noexcept
    {
        double t = timeMs;
        if (sync && bpm > 1.0)
            t = 60000.0 / bpm * juce::jlimit(0.03125, 8.0, delayDivBeatsLocal(division));
        targetSamples = juce::jlimit(1.0, fs * 2.0, t * 0.001 * fs);
        fbSm.setTargetValue(juce::jlimit(0.0f, 1.15f, feedbackPct * 0.01f));
        ping    = pingpong;
        colorIn = color;
        wow     = wowPct * 0.01f;
        wowLFO.setRate(0.6 + wow * 1.6);
        if (! juce::exactlyEqual(locutHz, lastLo)) { lastLo = locutHz;
            for (auto& f : lo) f.setCoefficients(Biquad::Type::highPass, fs, locutHz, 0.71, 0.0); }
        if (! juce::exactlyEqual(hicutHz, lastHi)) { lastHi = hicutHz;
            for (auto& f : hi) f.setCoefficients(Biquad::Type::lowPass, fs, hicutHz, 0.71, 0.0); }
        diffuse = diffusePct * 0.01f;
        widthSm.setTargetValue(widthPct * 0.01f);
        mixSm.setTargetValue(mixPct * 0.01f);
    }

    void process(juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        auto* l = buf.getWritePointer(0);
        auto* r = buf.getWritePointer(1);

        for (int i = 0; i < n; ++i)
        {
            // Time changes glide (tape-style repitch instead of clicks)
            const double dl = timeSlew[0].process(targetSamples);
            const double dr = timeSlew[1].process(targetSamples * (ping ? 1.0 : 1.0));
            const float  wobble = colorIn == 1 ? wowLFO.tick() * wow * 0.004f : 0.0f;

            float tapL = line[0].readHermite(dl * (1.0 + wobble));
            float tapR = line[1].readHermite(dr * (1.0 - wobble));

            // Color the TAP — filters, saturation and diffusion shape every
            // repeat (echo one included), and the colored tap is what feeds
            // back ("dead first echo" found by the knob harness, 2026-07-18).
            tapL = (float) hi[0].process(lo[0].process(tapL));
            tapR = (float) hi[1].process(lo[1].process(tapR));
            if (colorIn == 1) { tapL = fastTanh(tapL * 1.1f) * 0.92f; tapR = fastTanh(tapR * 1.1f) * 0.92f; }
            if (colorIn == 2) { tapL = fastTanh(tapL * 1.4f) * 0.75f; tapR = fastTanh(tapR * 1.4f) * 0.75f; }
            if (diffuse > 0.001f)
            {
                tapL = tapL + (diff[0][0].process(diff[0][1].process(tapL)) - tapL) * diffuse;
                tapR = tapR + (diff[1][0].process(diff[1][1].process(tapR)) - tapR) * diffuse;
            }

            const float fb  = fbSm.getNextValue();
            const float fbL = tapL * fb;
            const float fbR = tapR * fb;
            if (ping) { line[0].write(l[i] + fbR); line[1].write(fbL); }
            else      { line[0].write(l[i] + fbL); line[1].write(r[i] + fbR); }

            // Wet width
            const float w   = widthSm.getNextValue();
            const float mid = (tapL + tapR) * 0.5f, side = (tapL - tapR) * 0.5f * w;
            tapL = mid + side; tapR = mid - side;

            const float mix = mixSm.getNextValue();
            l[i] = equalPowerMix(l[i], tapL, mix);
            r[i] = equalPowerMix(r[i], tapR, mix);
        }
    }

    void reset()
    {
        for (auto& li : line) li.clear();
        for (auto& f : lo) f.reset();
        for (auto& f : hi) f.reset();
        for (auto& ch : diff) for (auto& a : ch) a.clear();
        timeSlew[0].reset(targetSamples); timeSlew[1].reset(targetSamples);
    }

private:
    static double delayDivBeatsLocal(int idx)
    {
        static const double b[] = { 4.0, 2.0, 3.0, 1.0, 1.5, 2.0 / 3.0,
                                    0.5, 0.75, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
        return b[juce::jlimit(0, 11, idx)];
    }

    double fs = 48000.0, targetSamples = 12000.0;
    bool   ping = false;
    int    colorIn = 1;
    float  wow = 0.2f, diffuse = 0.0f, lastLo = -1.0f, lastHi = -1.0f;
    std::array<DelayLine, 2> line;
    std::array<Biquad, 2> lo, hi;
    std::array<std::array<Allpass, 2>, 2> diff;
    std::array<OnePole, 2> timeSlew;
    LFO wowLFO;
    SmoothLin mixSm, fbSm, widthSm;
};

// --------------------------------------------------------------- Reverb ----
/* 8-line FDN, Householder feedback, modulated taps, damping per line,
   octave-up shimmer in the feedback loop. */
class FDNReverb
{
public:
    static constexpr int N = 8;

    void prepare(const Context& ctx)
    {
        fs = ctx.sampleRate;
        pre[0].prepare(fs, 0.3); pre[1].prepare(fs, 0.3);
        for (int k = 0; k < N; ++k)
        {
            lines[(size_t) k].prepare(fs, 0.35);
            damp[(size_t) k].setCutoff(6000.0, fs);
            lfo[(size_t) k].prepare(fs);
            lfo[(size_t) k].setRate(0.11 + 0.037 * k);
            lfo[(size_t) k].setPhase(k / (double) N);
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int a = 0; a < 3; ++a)
                inDiff[(size_t) ch][(size_t) a].prepare(fs, 30.0);
        shimmerL.prepare(fs); shimmerR.prepare(fs);
        shimmerL.setRatio(2.0); shimmerR.setRatio(2.0);
        locut[0].setCoefficients(Biquad::Type::highPass, fs, 140.0, 0.71, 0.0);
        locut[1].setCoefficients(Biquad::Type::highPass, fs, 140.0, 0.71, 0.0);
        mixSm.reset(fs, 0.05); widthSm.reset(fs, 0.05); shimSm.reset(fs, 0.1);
        configure(1, 0.6f, true);
        reset();
    }

    void set(int type, float sizePct, float decaySec, float predelayMs, float dampPct,
             float locutHz, float shimmerPct, float widthPct, float mixPct) noexcept
    {
        if (type != curType || std::abs(sizePct * 0.01f - curSize) > 0.001f)
            configure(type, sizePct * 0.01f, false);
        decay = decaySec;
        preSamples = juce::jlimit(0.0, fs * 0.25, predelayMs * 0.001 * fs);
        const double dampHz = juce::jmap((double) dampPct * 0.01, 16000.0, 1400.0);
        if (std::abs(dampHz - lastDamp) > 1.0)
        {
            lastDamp = dampHz;
            for (auto& d : damp) d.setCutoff(dampHz, fs);
        }
        if (std::abs(locutHz - lastLocut) > 0.5f)
        {
            lastLocut = locutHz;
            for (auto& f : locut) f.setCoefficients(Biquad::Type::highPass, fs, locutHz, 0.71, 0.0);
        }
        shimSm.setTargetValue(shimmerPct * 0.01f);
        widthSm.setTargetValue(widthPct * 0.01f);
        mixSm.setTargetValue(mixPct * 0.01f);
        updateFeedbackGains();
    }

    void process(juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        auto* l = buf.getWritePointer(0);
        auto* r = buf.getWritePointer(1);

        for (int i = 0; i < n; ++i)
        {
            pre[0].write(l[i]); pre[1].write(r[i]);
            float inL = pre[0].readLinear(juce::jmax(1.0, preSamples));
            float inR = pre[1].readLinear(juce::jmax(1.0, preSamples));
            for (int a = 0; a < diffStages; ++a)
            {
                inL = inDiff[0][(size_t) a].process(inL);
                inR = inDiff[1][(size_t) a].process(inR);
            }

            // Read all lines (modulated)
            std::array<float, N> v;
            float sum = 0.0f;
            for (int k = 0; k < N; ++k)
            {
                const double mod = 1.0 + lfo[(size_t) k].tick() * 0.0011;
                v[(size_t) k] = lines[(size_t) k].readHermite(lenSamples[(size_t) k] * mod);
                sum += v[(size_t) k];
            }
            // Householder: y = v - (2/N) * sum
            const float hs = sum * (2.0f / N);

            const float shim = shimSm.getNextValue();
            const float shimInL = shim > 0.001f ? shimmerL.process(v[0] + v[2]) : 0.0f;
            const float shimInR = shim > 0.001f ? shimmerR.process(v[1] + v[3]) : 0.0f;

            for (int k = 0; k < N; ++k)
            {
                // Shimmer trades feedback energy instead of adding on top —
                // keeps the tank's loop gain < 1 at any shimmer amount
                // (unstable runaway reported from Ableton, 2026-07-18).
                float fb = (v[(size_t) k] - hs) * fbGain[(size_t) k]
                           * (1.0f - 0.50f * shim);
                fb = (float) damp[(size_t) k].process(fb);
                const float inj = (k & 1) == 0 ? inL : inR;
                const float sh  = (k & 1) == 0 ? shimInL : shimInR;
                lines[(size_t) k].write(flushDenorm(fb + inj * 0.35f + sh * shim * 0.24f));
            }

            float wetL = (v[0] + v[2] + v[4] + v[6]) * 0.45f;
            float wetR = (v[1] + v[3] + v[5] + v[7]) * 0.45f;
            wetL = (float) locut[0].process(wetL);
            wetR = (float) locut[1].process(wetR);

            const float w   = widthSm.getNextValue();
            const float mid = (wetL + wetR) * 0.5f, side = (wetL - wetR) * 0.5f * w;
            wetL = mid + side; wetR = mid - side;

            const float mix = mixSm.getNextValue();
            l[i] = equalPowerMix(l[i], wetL, mix);
            r[i] = equalPowerMix(r[i], wetR, mix);
        }
    }

    void reset()
    {
        for (auto& li : lines) li.clear();
        pre[0].clear(); pre[1].clear();
        for (auto& d : damp) d.reset();
        for (auto& ch : inDiff) for (auto& a : ch) a.clear();
        for (auto& f : locut) f.reset();
        shimmerL.reset(); shimmerR.reset();
    }

private:
    void configure(int type, float size, bool force)
    {
        juce::ignoreUnused(force);
        curType = type; curSize = size;
        // Mutually-prime base lengths (ms) per space type
        static const double room[N]  = { 13.7, 19.1, 23.3, 29.9, 34.1, 39.7, 44.3, 51.1 };
        static const double hall[N]  = { 29.7, 37.1, 43.7, 53.3, 61.1, 71.9, 83.3, 97.7 };
        static const double plate[N] = { 7.1, 11.3, 15.7, 19.9, 24.1, 28.3, 33.5, 38.9 };
        const double* base = type == 0 ? room : type == 2 ? plate : hall;
        const double scale = 0.55 + 0.9 * size;
        for (int k = 0; k < N; ++k)
            lenSamples[(size_t) k] = juce::jlimit(32.0, fs * 0.33,
                                                  base[k] * scale * 0.001 * fs);
        // Input diffusion: plate & bloom are denser
        diffStages = type == 2 || type == 3 ? 3 : 2;
        static const double apMs[3] = { 5.3, 8.9, 14.2 };
        for (int ch = 0; ch < 2; ++ch)
            for (int a = 0; a < 3; ++a)
                inDiff[(size_t) ch][(size_t) a].set(
                    apMs[a] * 0.001 * fs * (1.0 + 0.13 * ch) * (type == 3 ? 1.7 : 1.0),
                    type == 3 ? 0.72f : 0.62f);
        updateFeedbackGains();
    }

    void updateFeedbackGains()
    {
        for (int k = 0; k < N; ++k)
        {
            const double t60 = juce::jmax(0.1f, decay);
            fbGain[(size_t) k] =
                (float) std::pow(10.0, -3.0 * lenSamples[(size_t) k] / (t60 * fs));
        }
    }

    double fs = 48000.0, preSamples = 0.0, lastDamp = -1.0;
    float  decay = 2.6f, curSize = 0.6f, lastLocut = -1.0f;
    int    curType = 1, diffStages = 2;
    std::array<DelayLine, N> lines;
    std::array<double, N> lenSamples {};
    std::array<float, N> fbGain {};
    std::array<OnePole, N> damp;
    std::array<LFO, N> lfo;
    std::array<DelayLine, 2> pre;
    std::array<std::array<Allpass, 3>, 2> inDiff;
    std::array<Biquad, 2> locut;
    OctaveShifter shimmerL, shimmerR;
    SmoothLin mixSm, widthSm, shimSm;
};

} // namespace vesper::dsp
