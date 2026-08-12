#pragma once
/*  MatchEQ.h — spectral Match EQ (ROADMAP Phase 2 depth; D-032).

    Workflow: play a reference tone through the plugin and LEARN it, then play
    your own material and LEARN that as the source. MATCH designs a filter that
    pushes the source's long-term spectrum toward the reference's, and applies
    it as a LINEAR-PHASE FIR (frequency-sampling design) via a partitioned
    convolution — the FabFilter/iZotope-grade result: it traces fine spectral
    detail with zero phase distortion, at the cost of a fixed latency
    (fftSize/2 samples, reported to the host and compensated on the dry path).

    Threading contract (mirrors ConvReverb, D-029):
      · pushInput()  — audio thread, near-free when not learning (a mono FIFO
                       write only while a Learn is armed).
      · pumpLearn()  — message thread (editor timer): pull a frame, window,
                       FFT, accumulate into the active profile's running mean.
      · computeMatch/setParams/loadCurve/rebuild — message thread: design the
                       kernel and hand it to the convolution (loadImpulseResponse
                       is thread-safe, loads on its own background thread).
      · process()    — audio thread: run the loaded FIR, or pass through
                       (and add zero latency) when inactive.

    The kernel design (designKernel) is a pure function, unit-tested against a
    known target curve. Only the raw reference-minus-source dB curve is stored
    in state; smoothing / max-dB / amount stay live parameters applied at
    design time. */

#include "Core.h"
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace vesper::dsp {

class MatchEQ
{
public:
    static constexpr int order    = 11;            // 2048-point analysis + FIR
    static constexpr int fftSize  = 1 << order;    // 2048
    static constexpr int bins     = fftSize / 2;   // 1024 unique magnitude bins
    static constexpr int latency  = fftSize / 2;   // linear-phase group delay

    enum class Learn { idle, reference, source };

    // ------------------------------------------------------------- lifecycle --
    void prepare(const Context& ctx)
    {
        fs = ctx.sampleRate;
        juce::dsp::ProcessSpec spec { fs, (juce::uint32) ctx.maxBlockSize, 2 };
        conv.prepare(spec);
        prepared = true;
        reset();
        if (haveCurve) rebuild();       // a preset may have loaded a curve first
    }

    void reset() { conv.reset(); }

    // -------------------------------------------------------- audio-thread tap --
    /* Called at the plugin input (pre-chain). Near-free unless a Learn is armed. */
    void pushInput(const juce::AudioBuffer<float>& buf) noexcept
    {
        if (learn.load(std::memory_order_relaxed) == 0) return;
        const int nn = buf.getNumSamples();
        const auto* l = buf.getReadPointer(0);
        const auto* r = buf.getNumChannels() > 1 ? buf.getReadPointer(1) : l;
        for (int i = 0; i < nn; ++i)
        {
            ring[(size_t) wr] = (l[i] + r[i]) * 0.5f;
            wr = (wr + 1) & (fftSize - 1);
        }
        ringCount.store(juce::jmin(fftSize, ringCount.load(std::memory_order_relaxed) + nn),
                        std::memory_order_release);
    }

    // -------------------------------------------------------- message-thread ---
    /* Editor timer: accumulate one frame into the active profile. */
    void pumpLearn()
    {
        const int st = learn.load(std::memory_order_relaxed);
        if (st == 0) return;
        if (ringCount.load(std::memory_order_acquire) < fftSize) return;

        const int rr = wr; // newest fftSize samples in time order
        for (int i = 0; i < fftSize; ++i)
            time[(size_t) i] = ring[(size_t) ((rr + i) & (fftSize - 1))];

        window.multiplyWithWindowingTable(time.data(), (size_t) fftSize);
        std::copy(time.begin(), time.end(), freq.begin());
        std::fill(freq.begin() + fftSize, freq.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform(freq.data()); // |X| in [0..bins]

        (st == 1 ? refProf[(size_t) activeRef] : srcProf).accumulate(freq.data());
    }

    void startLearn(bool reference)
    {
        (reference ? refProf[(size_t) activeRef] : srcProf).reset();
        ringCount.store(0, std::memory_order_release);
        learn.store(reference ? 1 : 2, std::memory_order_release);
    }

    /* Two reference slots (D-039): learn a second reference into B and flip
       between them to compare matches without re-learning either. */
    void setActiveReference(int slot) noexcept
    {
        activeRef = juce::jlimit(0, 1, slot);
    }
    int  activeReference() const noexcept { return activeRef; }
    bool referenceReady(int slot) const noexcept
    {
        return refProf[(size_t) juce::jlimit(0, 1, slot)].valid;
    }
    void stopLearn() { learn.store(0, std::memory_order_release); }

    Learn learnState() const noexcept
    {
        const int s = learn.load(std::memory_order_relaxed);
        return s == 1 ? Learn::reference : s == 2 ? Learn::source : Learn::idle;
    }
    bool referenceReady() const noexcept { return refProf[(size_t) activeRef].valid; }
    bool sourceReady()    const noexcept { return srcProf.valid; }
    bool hasMatch()       const noexcept { return haveCurve; }

    /* Design the raw correction from the two learned profiles. */
    void computeMatch()
    {
        const auto& ref = refProf[(size_t) activeRef];
        if (! ref.valid || ! srcProf.valid) return;
        for (int i = 0; i < bins; ++i)
        {
            const float rdb = gainToDb((float) juce::jmax(1.0e-9, ref.avg[(size_t) i]));
            const float sdb = gainToDb((float) juce::jmax(1.0e-9, srcProf.avg[(size_t) i]));
            rawCorrDb[(size_t) i] = rdb - sdb;
        }
        haveCurve = true;
        stopLearn();
        rebuild();
    }

    void clearMatch()
    {
        haveCurve = false; active = false;
        for (auto& r : refProf) r.reset();
        srcProf.reset();
        conv.reset();
    }

    /* Live parameters (message thread). */
    void setParams(bool on, float amountPct, float smoothOct, float maxDbIn)
    {
        onFlag = on;
        amount = juce::jlimit(0.0f, 1.0f, amountPct * 0.01f);
        smoothOct_ = juce::jlimit(0.0f, 3.0f, smoothOct);
        maxDb = juce::jlimit(1.0f, 30.0f, maxDbIn);
        if (haveCurve) rebuild();
        else active = false;
    }

    // ------------------------------------------------------------- audio path --
    void process(juce::AudioBuffer<float>& buf)
    {
        if (! active) return;
        juce::dsp::AudioBlock<float> block(buf);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        conv.process(ctx);
    }

    int latencySamples() const noexcept { return active ? latency : 0; }

    // --------------------------------------------------------------- state ------
    juce::String saveCurve() const
    {
        if (! haveCurve) return {};
        return juce::Base64::toBase64(rawCorrDb.data(), sizeof(float) * (size_t) bins);
    }
    void loadCurve(const juce::String& b64)
    {
        if (b64.isEmpty()) { haveCurve = false; active = false; return; }
        juce::MemoryOutputStream os;
        if (juce::Base64::convertFromBase64(os, b64)
            && os.getDataSize() == sizeof(float) * (size_t) bins)
        {
            std::memcpy(rawCorrDb.data(), os.getData(), sizeof(float) * (size_t) bins);
            haveCurve = true;
            if (prepared) rebuild();
        }
    }

    /* UI: final (smoothed/clamped/amount) correction in dB at a bin, or the
       display array. Returns false when there is no match to draw. */
    bool getDisplayCurve(std::array<float, bins>& dst) const
    {
        if (! haveCurve) return false;
        dst = displayDb;
        return true;
    }
    double binToHz(int i) const noexcept { return fs * (double) i / (double) fftSize; }

    /* Final correction (dB) at an arbitrary frequency — for the UI overlay. */
    float displayDbAtHz(double hz) const noexcept
    {
        if (! haveCurve) return 0.0f;
        const int i = juce::jlimit(0, bins - 1, (int) std::round(hz * fftSize / fs));
        return displayDb[(size_t) i];
    }

    // ---- pure, unit-tested: frequency-sampling linear-phase FIR design ----
    static void designKernel(const std::array<float, bins>& curveDb,
                             std::array<float, fftSize>& out)
    {
        std::array<juce::dsp::Complex<float>, fftSize> spec {}, timeC {};
        for (int i = 0; i <= bins; ++i)
        {
            const float db = curveDb[(size_t) juce::jmin(bins - 1, i)];
            spec[(size_t) i] = { dbToGain(db), 0.0f };
        }
        for (int i = 1; i < bins; ++i)             // Hermitian mirror (real, even)
            spec[(size_t) (fftSize - i)] = { spec[(size_t) i].real(), 0.0f };

        juce::dsp::FFT ifft(order);
        ifft.perform(spec.data(), timeC.data(), true); // normalized inverse (1/N)

        static const juce::dsp::WindowingFunction<float> win(
            (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false);
        for (int n = 0; n < fftSize; ++n)          // circular-shift by N/2 → causal
            out[(size_t) n] = timeC[(size_t) ((n + fftSize / 2) & (fftSize - 1))].real();
        win.multiplyWithWindowingTable(out.data(), (size_t) fftSize);
    }

private:
    // running-mean magnitude spectrum over learned frames
    struct Profile
    {
        std::array<double, bins> avg {};
        double count = 0.0;
        bool   valid = false;
        void reset() { avg.fill(0.0); count = 0.0; valid = false; }
        void accumulate(const float* mags)
        {
            count += 1.0;
            const double k = 1.0 / count;          // true running mean
            for (int i = 0; i < bins; ++i)
                avg[(size_t) i] += ((double) mags[(size_t) i] - avg[(size_t) i]) * k;
            if (count >= 8.0) valid = true;         // a few frames = representative
        }
    };

    void rebuild() // message thread: raw curve → smooth/normalize/clamp/amount → FIR
    {
        if (! prepared) return; // prepare() will rebuild once the convolution is ready
        std::array<float, bins> curve {};
        smoothCurve(rawCorrDb, curve, smoothOct_, fs);

        // remove the broadband offset (match tone, not level): subtract the mean
        // over the audible band so 100% amount doesn't just change loudness.
        double sum = 0.0; int cnt = 0;
        for (int i = 0; i < bins; ++i)
        {
            const double hz = fs * (double) i / (double) fftSize;
            if (hz >= 40.0 && hz <= 16000.0) { sum += curve[(size_t) i]; ++cnt; }
        }
        const float offset = cnt > 0 ? (float) (sum / cnt) : 0.0f;

        for (int i = 0; i < bins; ++i)
        {
            float db = (curve[(size_t) i] - offset);
            db = juce::jlimit(-maxDb, maxDb, db) * amount;
            curve[(size_t) i] = db;
        }
        displayDb = curve;

        designKernel(curve, kernel);
        juce::AudioBuffer<float> ir(2, fftSize);
        ir.copyFrom(0, 0, kernel.data(), fftSize);
        ir.copyFrom(1, 0, kernel.data(), fftSize);
        conv.loadImpulseResponse(std::move(ir), fs,
                                 juce::dsp::Convolution::Stereo::yes,
                                 juce::dsp::Convolution::Trim::no,
                                 juce::dsp::Convolution::Normalise::no);
        active = onFlag && amount > 0.001f;
    }

    /* Fractional-octave (constant-Q) smoothing of a dB curve. */
    static void smoothCurve(const std::array<float, bins>& in,
                            std::array<float, bins>& out, float oct, double /*fs*/)
    {
        if (oct <= 0.01f) { out = in; return; }
        const float half = std::pow(2.0f, oct * 0.5f);
        for (int i = 0; i < bins; ++i)
        {
            const int lo = juce::jlimit(0, bins - 1, (int) std::floor((float) i / half));
            const int hi = juce::jlimit(0, bins - 1, (int) std::ceil ((float) i * half));
            double s = 0.0; int c = 0;
            for (int k = lo; k <= hi; ++k) { s += in[(size_t) k]; ++c; }
            out[(size_t) i] = c > 0 ? (float) (s / c) : in[(size_t) i];
        }
    }

    double fs = 48000.0;
    bool   prepared = false;

    juce::dsp::Convolution conv;
    juce::dsp::FFT fft { order };
    juce::dsp::WindowingFunction<float> window {
        (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };

    // audio→message learn FIFO
    std::array<float, fftSize> ring {};
    int wr = 0;
    std::atomic<int> ringCount { 0 };
    std::atomic<int> learn { 0 };   // 0 idle, 1 reference, 2 source

    // scratch (message thread)
    std::array<float, fftSize>     time {};
    std::array<float, fftSize * 2> freq {};

    std::array<Profile, 2> refProf; // A/B reference slots (D-039)
    Profile srcProf;
    int activeRef = 0;

    std::array<float, bins>    rawCorrDb {};   // reference − source, pre-shaping
    std::array<float, bins>    displayDb {};   // final curve for the UI overlay
    std::array<float, fftSize> kernel {};

    bool  haveCurve = false, active = false, onFlag = false;
    float amount = 1.0f, smoothOct_ = 0.33f, maxDb = 12.0f;
};

} // namespace vesper::dsp
