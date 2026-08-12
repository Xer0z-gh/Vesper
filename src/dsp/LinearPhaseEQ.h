#pragma once
/*  LinearPhaseEQ.h — linear-phase mode for the 6-band EQ (D-036).

    The band set's static magnitude response is sampled per FFT bin and
    realized as a 2048-tap linear-phase FIR (the same frequency-sampling
    design the Match EQ uses — MatchEQ::designKernel), applied via
    juce::dsp::Convolution. Zero phase distortion; fixed fftSize/2-sample
    latency, reported to the host while the mode is armed and matched with
    a compensating delay when the EQ module is bypassed.

    Dynamic bands: DYN is inactive in linear-phase mode — the curve is the
    static response only (the biquad path remains the dynamic engine).

    Threading contract (mirrors ConvReverb / MatchEQ, D-029/D-032):
      · design()  — message thread only (allocates, loads the IR; the
                    convolution loads on its own background thread).
      · process() — audio thread; runs the loaded FIR, or passes through
                    until the first IR is ready.
      · setArmed()/latencySamples() — armed follows the eq_lp parameter so
                    the reported latency is stable against module bypass. */

#include "Core.h"
#include "EQFilter.h"
#include "MatchEQ.h"
#include <juce_dsp/juce_dsp.h>

namespace vesper::dsp {

class LinearPhaseEQ
{
public:
    static constexpr int order   = MatchEQ::order;    // 2048-tap, shared design
    static constexpr int fftSize = MatchEQ::fftSize;
    static constexpr int bins    = MatchEQ::bins;
    static constexpr int latency = MatchEQ::latency;  // fftSize / 2

    void prepare(const Context& ctx)
    {
        fs = ctx.sampleRate;
        juce::dsp::ProcessSpec spec { fs, (juce::uint32) ctx.maxBlockSize, 2 };
        conv.prepare(spec);
        prepared = true;
        haveIR = false;
        reset();
    }

    void reset() { conv.reset(); }

    /* Pure: sample the band set's static response and design the FIR. */
    static void designFromBands(const ParametricEQ::Band* bands, double sampleRate,
                                std::array<float, fftSize>& out)
    {
        std::array<float, bins> curveDb {};
        for (int i = 0; i < bins; ++i)
        {
            const double hz = juce::jlimit(1.0, sampleRate * 0.5,
                                           sampleRate * (double) i / (double) fftSize);
            curveDb[(size_t) i] = ParametricEQ::responseDbAt(bands, sampleRate, hz);
        }
        MatchEQ::designKernel(curveDb, out);
    }

    /* Message thread: redesign from the current band set and load it. */
    void design(const ParametricEQ::Band* bands)
    {
        if (! prepared) return;
        designFromBands(bands, fs, kernel);
        juce::AudioBuffer<float> ir(2, fftSize);
        ir.copyFrom(0, 0, kernel.data(), fftSize);
        ir.copyFrom(1, 0, kernel.data(), fftSize);
        conv.loadImpulseResponse(std::move(ir), fs,
                                 juce::dsp::Convolution::Stereo::yes,
                                 juce::dsp::Convolution::Trim::no,
                                 juce::dsp::Convolution::Normalise::no);
        haveIR = true;
    }

    /* Armed = the eq_lp parameter is on. Latency is reported while armed
       regardless of module enable, so bypass toggles never re-time the
       session (the processor adds a matched delay on the bypass path). */
    void setArmed(bool a) noexcept { armed = a; }
    bool isArmed() const noexcept { return armed; }
    bool ready() const noexcept { return haveIR.load(std::memory_order_relaxed); }
    int  latencySamples() const noexcept { return armed ? latency : 0; }

    void process(juce::AudioBuffer<float>& buf)
    {
        if (! haveIR) return; // first design still loading: pass through
        juce::dsp::AudioBlock<float> block(buf);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        conv.process(ctx);
    }

private:
    double fs = 48000.0;
    bool   prepared = false, armed = false;
    std::atomic<bool> haveIR { false };
    juce::dsp::Convolution conv;
    std::array<float, fftSize> kernel {};
};

} // namespace vesper::dsp
