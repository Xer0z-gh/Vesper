#pragma once
/*  Analysis.h — audio-thread-safe metering & FFT capture.
    The audio thread only writes atomics / a lock-free FIFO; the UI thread
    pulls at 60 fps and owns all smoothing/ballistics. */

#include "Core.h"
#include <atomic>

namespace vesper::dsp {

// ---------------------------------------------------------------- Meters ---
/* Peak + RMS per channel, correlation, and momentary LUFS (K-weighted,
   400 ms window per ITU-R BS.1770, mono-summed simplification). */
class MeterProbe
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        // K-weighting: shelving pre-filter + RLB highpass (BS.1770 constants)
        for (int ch = 0; ch < 2; ++ch)
        {
            kShelf[(size_t) ch].setCoefficients(Biquad::Type::highShelf, fs, 1681.97, 0.7071, 3.99958);
            kHP[(size_t) ch].setCoefficients(Biquad::Type::highPass, fs, 38.13, 0.5, 0.0);
        }
        lufsWin.setTau(400.0, fs);
        corrWin.setTau(300.0, fs);
        reset();
    }

    void push(const juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        const auto* l = buf.getReadPointer(0);
        const auto* r = buf.getNumChannels() > 1 ? buf.getReadPointer(1) : l;

        float pkL = 0.0f, pkR = 0.0f;
        double sumSq = 0.0, sumLR = 0.0, sumLL = 1.0e-12, sumRR = 1.0e-12;

        for (int i = 0; i < n; ++i)
        {
            pkL = juce::jmax(pkL, std::abs(l[i]));
            pkR = juce::jmax(pkR, std::abs(r[i]));
            const double kl = kHP[0].process(kShelf[0].process(l[i]));
            const double kr = kHP[1].process(kShelf[1].process(r[i]));
            sumSq += kl * kl + kr * kr;
            sumLR += (double) l[i] * r[i];
            sumLL += (double) l[i] * l[i];
            sumRR += (double) r[i] * r[i];
        }

        peakL.store(juce::jmax(peakL.load(std::memory_order_relaxed), pkL), std::memory_order_relaxed);
        peakR.store(juce::jmax(peakR.load(std::memory_order_relaxed), pkR), std::memory_order_relaxed);

        const double meanSq = lufsWin.process(sumSq / juce::jmax(1, n));
        lufs.store((float) (-0.691 + 10.0 * std::log10(juce::jmax(1.0e-12, meanSq))),
                   std::memory_order_relaxed);
        rms.store((float) std::sqrt(juce::jmax(0.0, meanSq)), std::memory_order_relaxed);

        const double corr = corrWin.process(sumLR / std::sqrt(sumLL * sumRR));
        correlation.store((float) juce::jlimit(-1.0, 1.0, corr), std::memory_order_relaxed);
    }

    /* UI reads then clears the peak-hold. */
    float consumePeak(int ch)
    {
        auto& a = ch == 0 ? peakL : peakR;
        return a.exchange(0.0f, std::memory_order_relaxed);
    }

    void reset()
    {
        peakL = 0.0f; peakR = 0.0f; rms = 0.0f;
        lufs = -70.0f; correlation = 1.0f;
        lufsWin.reset(); corrWin.reset(1.0);
        for (auto& f : kShelf) f.reset();
        for (auto& f : kHP) f.reset();
    }

    std::atomic<float> rms { 0.0f }, lufs { -70.0f }, correlation { 1.0f };

private:
    double fs = 48000.0;
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::array<Biquad, 2> kShelf, kHP;
    OnePole lufsWin, corrWin;
};

// ------------------------------------------------------------- FFT capture --
/* Single-producer/single-consumer sample FIFO for the spectrum analyzer.
   Audio thread pushes mono-summed samples; the UI performs the FFT. */
class FFTCapture
{
public:
    static constexpr int fftOrder = 11;               // 2048
    static constexpr int fftSize  = 1 << fftOrder;

    void push(const juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        const auto* l = buf.getReadPointer(0);
        const auto* r = buf.getNumChannels() > 1 ? buf.getReadPointer(1) : l;
        for (int i = 0; i < n; ++i)
        {
            fifo[(size_t) writeIdx] = (l[i] + r[i]) * 0.5f;
            writeIdx = (writeIdx + 1) & (fftSize - 1);
        }
        count.store(juce::jmin(fftSize, count.load(std::memory_order_relaxed) + n),
                    std::memory_order_release);
    }

    /* UI thread: copies the newest fftSize samples in time order into dest
       (which must hold fftSize floats). Returns false if not enough data yet. */
    bool read(float* dest) const
    {
        if (count.load(std::memory_order_acquire) < fftSize) return false;
        const int w = writeIdx; // racy by one block — fine for visualization
        for (int i = 0; i < fftSize; ++i)
            dest[i] = fifo[(size_t) ((w + i) & (fftSize - 1))];
        return true;
    }

private:
    std::array<float, fftSize> fifo {};
    int writeIdx = 0;
    std::atomic<int> count { 0 };
};

} // namespace vesper::dsp
