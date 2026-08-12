#pragma once
/*  VesperProcessor — hosts the module chain, parameter tree, preset system,
    A/B states and analysis taps.

    Threading contract:
      * processBlock reads parameters through cached atomics only.
      * The chain order is a packed uint64 atomic — reordering from the UI is
        lock-free and click-safe (modules keep their state when moved).
      * Latency is constant for a given oversampling setting: disabled
        latency-bearing modules are replaced by matched delays, so toggling
        them never shifts the mix. */

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <map>
#include "core/Creative.h"
#include "core/Params.h"
#include "core/PresetManager.h"
#include "dsp/Analysis.h"
#include "dsp/ConvReverb.h"
#include "dsp/Dynamics.h"
#include "dsp/DynamicsPlus.h"
#include "dsp/EQFilter.h"
#include "dsp/LinearPhaseEQ.h"
#include "dsp/MatchEQ.h"
#include "dsp/ModFX.h"
#include "dsp/PitchFX.h"
#include "dsp/Saturation.h"
#include "dsp/TimeFX.h"

class VesperProcessor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    VesperProcessor();
    ~VesperProcessor() override;

    // ------------------------------------------------------- AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock; // keep the double overload visible
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Vesper"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ------------------------------------------------------------ chain API
    std::array<int, vesper::numModules> getChainOrder() const
    {
        return vesper::unpackOrder(chainOrder.load(std::memory_order_relaxed));
    }
    void setChainOrder(const std::array<int, vesper::numModules>& order)
    {
        chainOrder.store(vesper::packOrder(order), std::memory_order_relaxed);
        apvts.state.setProperty("chainOrder", orderToString(order), nullptr);
    }

    // -------------------------------------------------------------- A/B API
    void toggleAB();
    void copyActiveToOther();
    bool isStateB() const noexcept { return stateIsB; }
    const juce::ValueTree& getOtherState() const noexcept { return otherState; }

    // -------------------------------------------------------------- shared
    juce::AudioProcessorValueTreeState apvts;
    juce::UndoManager undoManager;
    vesper::PresetManager presets;
    vesper::MacroEngine macros { apvts };

    vesper::dsp::MeterProbe meterIn, meterOut;
    vesper::dsp::FFTCapture fftTap;
    vesper::dsp::Compressor    comp;      // exposed for GR metering
    vesper::dsp::Limiter       limiter;
    vesper::dsp::Gate          gate;
    vesper::dsp::MultibandComp mbComp;
    vesper::dsp::ParametricEQ  eq;        // exposed for the per-band DYN meters
    vesper::dsp::MatchEQ       matchEQ;   // linear-phase spectral match (D-032)
    vesper::dsp::LinearPhaseEQ lpEQ;      // linear-phase EQ mode (D-036)

    static juce::String orderToString(const std::array<int, vesper::numModules>&);
    static std::array<int, vesper::numModules> orderFromString(const juce::String&);

    // ---- user impulse response for the convolution reverb (D-038) ----
    bool loadUserIR(const juce::File&);            // message thread
    juce::String getUserIRPath() const { return userIRPath; }
    juce::String getUserIRName() const
    {
        return userIRPath.isEmpty() ? juce::String()
                                    : juce::File(userIRPath).getFileNameWithoutExtension();
    }
    bool userIRFailed() const { return userIRLoadFailed; }

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void updateModuleParams();
    void refreshLatency();

    // modules
    vesper::dsp::CreativeFilter    filter;
    vesper::dsp::Saturator         sat;
    vesper::dsp::TransientDesigner transient;
    vesper::dsp::ModulationFX      modFX;
    vesper::dsp::PitchFX           pitch;
    vesper::dsp::StereoDelay       delay;
    vesper::dsp::FDNReverb         reverb;
    vesper::dsp::ConvReverb        convReverb;   // Spring/Cathedral spaces (D-029)
    vesper::dsp::StereoTools       stereo;

    // Convolution IR is (re)built on the message thread, never on audio.
    struct ConvRegen : juce::AsyncUpdater
    {
        std::function<void()> fn;
        void handleAsyncUpdate() override { if (fn) fn(); }
    } convRegen, matchRegen, lpRegen;
    void regenerateConvIR();
    void regenerateMatch();
    void regenerateLpEQ();

    juce::String userIRPath;              // D-038, persisted in state
    bool userIRLoadFailed = false;        // UI reports a missing/unreadable file

    // chain
    std::atomic<juce::uint64> chainOrder { vesper::defaultOrder() };

    // cached raw parameter atomics (filled once in the constructor).
    // std::map: juce::String has no std::hash, and lookups are per-block only.
    std::map<juce::String, std::atomic<float>*> raw;
    std::atomic<float>* rp(const char* id) { return raw.at(id); }
    float rv(const char* id) const { return raw.at(id)->load(std::memory_order_relaxed); }
    bool  rb(const char* id) const { return rv(id) > 0.5f; }

    // routing / compensation
    juce::AudioBuffer<float> wet, dry;
    std::array<vesper::dsp::DelayLine, 2> dryDelay, satComp, limComp, eqLpComp;
    vesper::dsp::SmoothLin inGainSm, outGainSm, mixSm;
    double currentLatency = 0.0;
    std::atomic<bool> latencyDirty { true };
    double hostBpm = 120.0;
    double fs = 48000.0;

    // A/B
    juce::ValueTree otherState;
    bool stateIsB = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VesperProcessor)
};
