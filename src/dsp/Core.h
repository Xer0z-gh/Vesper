#pragma once
/*  Core.h — shared DSP primitives for Vesper.
    All filter/delay state is kept in double precision; audio I/O is float.
    Everything here is allocation-free after prepare() and safe on the audio thread. */

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <vector>

namespace vesper::dsp {

// ---------------------------------------------------------------- helpers --
inline float dbToGain(float db) noexcept { return juce::Decibels::decibelsToGain(db, -120.0f); }
inline float gainToDb(float g)  noexcept { return juce::Decibels::gainToDecibels(g, -120.0f); }

/* Rational tanh-style soft clip, ~6x faster than std::tanh.
   Equals exactly ±1 at x = ±3, so clamping there keeps it continuous and
   monotonic (max deviation from true tanh ≈ 0.02 — inaudible as a
   saturation curve; what matters is smoothness and boundedness). */
inline float fastTanh(float x) noexcept
{
    if (x >=  3.0f) return  1.0f;
    if (x <= -3.0f) return -1.0f;
    const float x2 = x * x;
    // jlimit guards the one-ulp float overshoot just inside +/-3.
    return juce::jlimit(-1.0f, 1.0f, x * (27.0f + x2) / (27.0f + 9.0f * x2));
}

inline float flushDenorm(float v) noexcept
{
    return std::abs(v) < 1.0e-30f ? 0.0f : v;
}
inline double flushDenorm(double v) noexcept
{
    return std::abs(v) < 1.0e-60 ? 0.0 : v;
}

/* Equal-power dry/wet. mix in [0,1]. */
inline float equalPowerMix(float dry, float wet, float mix) noexcept
{
    const float a = std::cos(mix * juce::MathConstants<float>::halfPi);
    const float b = std::sin(mix * juce::MathConstants<float>::halfPi);
    return dry * a + wet * b;
}

using SmoothLin = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;
using SmoothMul = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;

// ---------------------------------------------------------------- OnePole --
/* One-pole lowpass, double state. Also usable as an envelope smoother. */
class OnePole
{
public:
    void setTau(double ms, double fs) noexcept
    {
        a = std::exp(-1.0 / (0.001 * juce::jmax(0.01, ms) * fs));
    }
    void setCutoff(double hz, double fs) noexcept
    {
        a = std::exp(-juce::MathConstants<double>::twoPi * hz / fs);
    }
    double process(double x) noexcept { z = flushDenorm(x + a * (z - x)); return z; }
    void reset(double v = 0.0) noexcept { z = v; }
    double state() const noexcept { return z; }

private:
    double a = 0.0, z = 0.0;
};

// ----------------------------------------------------------------- Biquad --
/* RBJ biquad, transposed direct form II, double precision state + coeffs. */
class Biquad
{
public:
    enum class Type { bell, lowShelf, highShelf, lowPass, highPass, notch, allPass };

    void setCoefficients(Type type, double fs, double freq, double q, double gainDb) noexcept
    {
        freq = juce::jlimit(10.0, fs * 0.49, freq);
        q    = juce::jmax(0.025, q);
        const double A     = std::pow(10.0, gainDb / 40.0);
        const double w0    = juce::MathConstants<double>::twoPi * freq / fs;
        const double cw    = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * q);
        double b0i = 1, b1i = 0, b2i = 0, a0 = 1, a1i = 0, a2i = 0;

        switch (type)
        {
            case Type::bell:
                b0i = 1 + alpha * A; b1i = -2 * cw; b2i = 1 - alpha * A;
                a0  = 1 + alpha / A; a1i = -2 * cw; a2i = 1 - alpha / A;
                break;
            case Type::lowShelf:
            {
                const double s = 2.0 * std::sqrt(A) * alpha;
                b0i = A * ((A + 1) - (A - 1) * cw + s);
                b1i = 2 * A * ((A - 1) - (A + 1) * cw);
                b2i = A * ((A + 1) - (A - 1) * cw - s);
                a0  = (A + 1) + (A - 1) * cw + s;
                a1i = -2 * ((A - 1) + (A + 1) * cw);
                a2i = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case Type::highShelf:
            {
                const double s = 2.0 * std::sqrt(A) * alpha;
                b0i = A * ((A + 1) + (A - 1) * cw + s);
                b1i = -2 * A * ((A - 1) + (A + 1) * cw);
                b2i = A * ((A + 1) + (A - 1) * cw - s);
                a0  = (A + 1) - (A - 1) * cw + s;
                a1i = 2 * ((A - 1) - (A + 1) * cw);
                a2i = (A + 1) - (A - 1) * cw - s;
                break;
            }
            case Type::lowPass:
                b0i = (1 - cw) / 2; b1i = 1 - cw; b2i = (1 - cw) / 2;
                a0  = 1 + alpha;    a1i = -2 * cw; a2i = 1 - alpha;
                break;
            case Type::highPass:
                b0i = (1 + cw) / 2; b1i = -(1 + cw); b2i = (1 + cw) / 2;
                a0  = 1 + alpha;    a1i = -2 * cw;   a2i = 1 - alpha;
                break;
            case Type::notch:
                b0i = 1; b1i = -2 * cw; b2i = 1;
                a0  = 1 + alpha; a1i = -2 * cw; a2i = 1 - alpha;
                break;
            case Type::allPass:
                b0i = 1 - alpha; b1i = -2 * cw; b2i = 1 + alpha;
                a0  = 1 + alpha; a1i = -2 * cw; a2i = 1 - alpha;
                break;
        }
        b0 = b0i / a0; b1 = b1i / a0; b2 = b2i / a0;
        a1 = a1i / a0; a2 = a2i / a0;
    }

    double process(double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = flushDenorm(b1 * x - a1 * y + z2);
        z2 = flushDenorm(b2 * x - a2 * y);
        return y;
    }

    void reset() noexcept { z1 = z2 = 0.0; }

    /* |H(f)| for UI response curves. */
    double magnitudeAt(double freq, double fs) const noexcept
    {
        const double w = juce::MathConstants<double>::twoPi * freq / fs;
        const std::complex<double> ejw(std::cos(-w), std::sin(-w));
        const auto ejw2 = ejw * ejw;
        const auto num  = b0 + b1 * ejw + b2 * ejw2;
        const auto den  = 1.0 + a1 * ejw + a2 * ejw2;
        return std::abs(num / den);
    }

private:
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
};

// ---------------------------------------------------------------- TPT SVF --
/* Zero-delay-feedback state variable filter (Zavalishin / cytomic form). */
class SVF
{
public:
    void prepare(double sampleRate) noexcept { fs = sampleRate; reset(); }
    void set(double cutoff, double res) noexcept
    {
        cutoff = juce::jlimit(10.0, fs * 0.49, cutoff);
        g = std::tan(juce::MathConstants<double>::pi * cutoff / fs);
        k = 2.0 - 1.98 * juce::jlimit(0.0, 1.0, res);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    /* Returns {lp, bp, hp} for one sample. */
    void process(double x, double& lp, double& bp, double& hp) noexcept
    {
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushDenorm(2.0 * v1 - ic1);
        ic2 = flushDenorm(2.0 * v2 - ic2);
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    void reset() noexcept { ic1 = ic2 = 0.0; }

private:
    double fs = 48000.0, g = 0.1, k = 1.0, a1 = 0, a2 = 0, a3 = 0;
    double ic1 = 0, ic2 = 0;
};

// ------------------------------------------------------------- Delay line --
/* Power-of-two circular buffer with cubic (Hermite) fractional read. */
class DelayLine
{
public:
    void prepare(double fs, double maxSeconds)
    {
        const auto needed = (size_t) std::ceil(fs * maxSeconds) + 8;
        size_t n = 1;
        while (n < needed) n <<= 1;
        buffer.assign(n, 0.0f);
        mask = n - 1;
        writePos = 0;
    }
    void write(float v) noexcept
    {
        buffer[writePos] = flushDenorm(v);
        writePos = (writePos + 1) & mask;
    }
    float readLinear(double delaySamples) const noexcept
    {
        delaySamples = juce::jlimit(1.0, (double) mask - 2.0, delaySamples);
        const double r  = wrapPosition((double) writePos - delaySamples);
        const auto   i0 = (size_t) r; // non-negative after wrap
        const float  fr = (float) (r - (double) i0);
        const float  a  = buffer[i0 & mask];
        const float  b  = buffer[(i0 + 1) & mask];
        return a + fr * (b - a);
    }
    float readHermite(double delaySamples) const noexcept
    {
        delaySamples = juce::jlimit(2.0, (double) mask - 3.0, delaySamples);
        const double r  = wrapPosition((double) writePos - delaySamples);
        const auto   i1 = (size_t) r; // non-negative after wrap
        const float  t  = (float) (r - (double) i1);
        const float xm1 = buffer[(i1 - 1) & mask];
        const float x0  = buffer[i1 & mask];
        const float x1  = buffer[(i1 + 1) & mask];
        const float x2  = buffer[(i1 + 2) & mask];
        const float c   = (x1 - xm1) * 0.5f;
        const float v   = x0 - x1;
        const float w   = c + v;
        const float a   = w + v + (x2 - x0) * 0.5f;
        const float b   = w + a;
        return ((a * t - b) * t + c) * t + x0;
    }
    void clear() noexcept { std::fill(buffer.begin(), buffer.end(), 0.0f); }

private:
    /* Wraps a (possibly negative) read position into [0, size). Casting a
       negative double to size_t is UB — this must happen before any cast. */
    double wrapPosition(double r) const noexcept
    {
        const double n = (double) (mask + 1);
        if (r < 0.0)  r += n * std::ceil(-r / n);
        if (r >= n)   r -= n * std::floor(r / n);
        return r;
    }

    std::vector<float> buffer;
    size_t mask = 0, writePos = 0;
};

// -------------------------------------------------------------------- LFO --
class LFO
{
public:
    enum class Shape { sine, triangle };
    void prepare(double sampleRate) noexcept { fs = sampleRate; }
    void setRate(double hz) noexcept { inc = hz / fs; }
    void setPhase(double p) noexcept { phase = p - std::floor(p); }
    /* Advances and returns value in [-1, 1]. phaseOffset in cycles. */
    float tick(double phaseOffset = 0.0) noexcept
    {
        phase += inc;
        if (phase >= 1.0) phase -= 1.0;
        double p = phase + phaseOffset;
        p -= std::floor(p);
        if (shape == Shape::sine)
            return (float) std::sin(juce::MathConstants<double>::twoPi * p);
        return (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
    }
    Shape shape = Shape::sine;

private:
    double fs = 48000.0, inc = 0.0, phase = 0.0;
};

// ------------------------------------------------------ Envelope follower --
class EnvelopeFollower
{
public:
    enum class Mode { peak, rms };
    void prepare(double sampleRate) noexcept { fs = sampleRate; setTimes(atkMs, relMs); }
    void setTimes(double attackMs, double releaseMs) noexcept
    {
        atkMs = attackMs; relMs = releaseMs;
        aAtk = std::exp(-1.0 / (0.001 * juce::jmax(0.01, attackMs) * fs));
        aRel = std::exp(-1.0 / (0.001 * juce::jmax(0.1, releaseMs) * fs));
    }
    double process(double x) noexcept
    {
        const double in = mode == Mode::rms ? x * x : std::abs(x);
        const double a  = in > env ? aAtk : aRel;
        env = flushDenorm(in + a * (env - in));
        return mode == Mode::rms ? std::sqrt(juce::jmax(0.0, env)) : env;
    }
    void reset() noexcept { env = 0.0; }
    Mode mode = Mode::peak;

private:
    double fs = 48000.0, atkMs = 10.0, relMs = 100.0, aAtk = 0.0, aRel = 0.0, env = 0.0;
};

// ------------------------------------------------- Octave shifter (shimmer) --
/* Dual-tap crossfading delay pitch shifter — the classic "shimmer" building
   block. ratio 2.0 = +1 octave. Cheap, smooth, allocation-free. */
class OctaveShifter
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        line.prepare(fs, 0.25);
        windowSamples = fs * 0.060; // 60 ms grain
        tap = 0.0;
    }
    void setRatio(double r) noexcept { ratio = r; }
    float process(float x) noexcept
    {
        line.write(x);
        tap += (ratio - 1.0);
        if (tap >= windowSamples) tap -= windowSamples;
        if (tap < 0.0) tap += windowSamples;
        const double t2 = tap + windowSamples * 0.5 >= windowSamples
                              ? tap - windowSamples * 0.5 : tap + windowSamples * 0.5;
        // Triangular crossfade between the two moving taps.
        const float w1 = (float) (1.0 - std::abs(2.0 * tap / windowSamples - 1.0));
        const float w2 = 1.0f - w1;
        return line.readHermite(1.0 + tap) * w1 + line.readHermite(1.0 + t2) * w2;
    }
    void reset() noexcept { line.clear(); tap = 0.0; }

private:
    DelayLine line;
    double fs = 48000.0, windowSamples = 2880.0, tap = 0.0, ratio = 2.0;
};

// -------------------------------------------------------- process context --
struct Context
{
    double sampleRate = 48000.0;
    int    maxBlockSize = 512;
    int    numChannels = 2;
};

} // namespace vesper::dsp
