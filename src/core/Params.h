#pragma once
/*  Params.h — single source of truth for every parameter in Vesper.
    IDs, ranges, defaults, choice lists and the APVTS layout all live here so
    the processor, UI, preset system and randomizer can never disagree. */

#include <juce_audio_processors/juce_audio_processors.h>

namespace vesper {

// ------------------------------------------------------------- module ids --
enum ModuleID : int { mEQ = 0, mFilter, mComp, mSat, mTransient,
                      mModFX, mDelay, mReverb, mStereo, mLimiter,
                      mGate, mMBComp, mPitch, // appended in 1.0 — ids are stable
                      numModules };

inline const char* moduleName(int m)
{
    static const char* names[] = { "EQ", "Filter", "Compress", "Saturate", "Transient",
                                   "Motion", "Delay", "Reverb", "Stereo", "Limit",
                                   "Gate", "Multiband", "Pitch" };
    return names[m];
}
inline const char* moduleKey(int m)
{
    static const char* keys[] = { "eq", "flt", "cmp", "sat", "trn",
                                  "mod", "dly", "rev", "st", "lim",
                                  "gte", "mbc", "pit" };
    return keys[m];
}

namespace ids {
// global
inline constexpr auto inGain   = "in_gain";
inline constexpr auto outGain  = "out_gain";
inline constexpr auto mix      = "mix";
inline constexpr auto oversamp = "oversample";
inline constexpr auto bypass   = "bypass";
inline constexpr auto macro1   = "macro1";
inline constexpr auto macro2   = "macro2";
inline constexpr auto macro3   = "macro3";
inline constexpr auto macro4   = "macro4";
// module enables are "<key>_on", e.g. "eq_on"
} // namespace ids

inline juce::String moduleOnID(int m) { return juce::String(moduleKey(m)) + "_on"; }
inline juce::String eqBandID(int band, const char* suffix)
{
    return "eq_b" + juce::String(band + 1) + "_" + suffix;
}

// ------------------------------------------------------------ choice lists --
inline const juce::StringArray& satAlgoNames()
{
    static const juce::StringArray a { "Tube", "Tape", "Transformer", "Console", "Soft Analog",
                                       "Warm", "Exciter", "Triode", "Pentode", "Germanium",
                                       "Silicon", "Foldback", "Wavefold", "Bitcrush", "Downsample",
                                       "Soft Clip", "Hard Clip" };
    return a;
}
inline const juce::StringArray& gateModeNames()
{
    static const juce::StringArray a { "Gate", "Expander" };
    return a;
}
inline const juce::StringArray& pitchModeNames()
{
    static const juce::StringArray a { "Micro", "Octave Up", "Octave Down", "Fifth", "Wide Double" };
    return a;
}
inline const juce::StringArray& filterTypeNames()
{
    static const juce::StringArray a { "Clean 12", "Clean 24", "Ladder", "Acid",
                                       "Comb", "Formant" };
    return a;
}
inline const juce::StringArray& modModeNames()
{
    static const juce::StringArray a { "Chorus", "Ensemble", "Doubler", "Flanger",
                                       "Phaser", "Vibrato", "Tremolo", "Rotary" };
    return a;
}
inline const juce::StringArray& delayColorNames()
{
    static const juce::StringArray a { "Digital", "Tape", "Analog" };
    return a;
}
inline const juce::StringArray& delayDivNames()
{
    static const juce::StringArray a { "1/1", "1/2", "1/2D", "1/4", "1/4D", "1/4T",
                                       "1/8", "1/8D", "1/8T", "1/16", "1/16T", "1/32" };
    return a;
}
/* Division length in quarter-notes. */
inline double delayDivBeats(int idx)
{
    static const double b[] = { 4.0, 2.0, 3.0, 1.0, 1.5, 2.0 / 3.0,
                                0.5, 0.75, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return b[juce::jlimit(0, 11, idx)];
}
inline const juce::StringArray& reverbTypeNames()
{
    // 0-3 = FDN spaces; 4-7 = convolution spaces (D-029, User IR D-038)
    static const juce::StringArray a { "Room", "Hall", "Plate", "Bloom",
                                       "Spring", "Cathedral", "Reverse", "User IR" };
    return a;
}
inline const juce::StringArray& eqBandTypeNames()
{
    static const juce::StringArray a { "Bell", "Low Shelf", "High Shelf",
                                       "High Pass", "Low Pass", "Notch" };
    return a;
}
/* Does this band type actually use its `gain` parameter? High Pass, Low
   Pass and Notch do not — their coefficient designs never touch A (see
   Biquad::setCoefficients). The UI asks so it can stop offering a gain
   axis that does nothing: no vertical drag on the curve node, and the
   GAIN knob dims (D-064). */
inline bool eqTypeHasGain(int type) noexcept
{
    return type != 3 && type != 4 && type != 5;
}
inline const juce::StringArray& oversampleNames()
{
    static const juce::StringArray a { "1x", "2x", "4x", "8x", "16x" };
    return a;
}
inline const juce::StringArray& compDetectNames()
{
    static const juce::StringArray a { "Peak", "RMS" };
    return a;
}
inline const juce::StringArray& compCharacterNames()
{
    static const juce::StringArray a { "Clean", "Punchy", "Vintage" };
    return a;
}
inline const juce::StringArray& transientSpeedNames()
{
    static const juce::StringArray a { "Fast", "Medium", "Slow" };
    return a;
}

// -------------------------------------------------------------- the layout --
/* The parameter set itself. Split out from createParameterLayout() so tests
   can enumerate ids and ranges (preset QA, D-040) without an AudioProcessor. */
inline std::vector<std::unique_ptr<juce::RangedAudioParameter>> createParameterList()
{
    using FP  = juce::AudioParameterFloat;
    using CP  = juce::AudioParameterChoice;
    using BP  = juce::AudioParameterBool;
    using Rng = juce::NormalisableRange<float>;

    auto freqRange = [] (float lo, float hi) {
        return Rng(lo, hi, 0.0f, 0.25f); // log-ish skew
    };
    auto db  = [] (const char* id, const char* nm, float lo, float hi, float def) {
        return std::make_unique<FP>(juce::ParameterID(id, 1), nm, Rng(lo, hi, 0.01f), def,
            juce::AudioParameterFloatAttributes().withLabel("dB"));
    };
    auto pct = [] (const char* id, const char* nm, float def) {
        return std::make_unique<FP>(juce::ParameterID(id, 1), nm, Rng(0.0f, 100.0f, 0.1f), def,
            juce::AudioParameterFloatAttributes().withLabel("%"));
    };

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // ---- global ----
    p.push_back(db(ids::inGain,  "Input",  -24.0f, 24.0f, 0.0f));
    p.push_back(db(ids::outGain, "Output", -24.0f, 24.0f, 0.0f));
    p.push_back(pct(ids::mix, "Mix", 100.0f));
    p.push_back(std::make_unique<CP>(juce::ParameterID(ids::oversamp, 1), "Oversampling",
                                     oversampleNames(), 1));
    p.push_back(std::make_unique<BP>(juce::ParameterID(ids::bypass, 1), "Bypass", false));
    for (int i = 1; i <= 4; ++i)
        p.push_back(std::make_unique<FP>(
            juce::ParameterID("macro" + juce::String(i), 1), "Macro " + juce::String(i),
            Rng(0.0f, 1.0f, 0.001f), 0.0f));

    // ---- module enables ----
    for (int m = 0; m < numModules; ++m)
        p.push_back(std::make_unique<BP>(juce::ParameterID(moduleOnID(m), 1),
                                         juce::String(moduleName(m)) + " On", false));

    // ---- EQ: 6 bands ----
    static const float defFreq[6] = { 60.0f, 180.0f, 550.0f, 1800.0f, 5000.0f, 12000.0f };
    static const int   defType[6] = { 3, 0, 0, 0, 0, 2 }; // HP, bells, high shelf
    for (int b = 0; b < 6; ++b)
    {
        const auto n = "Band " + juce::String(b + 1) + " ";
        p.push_back(std::make_unique<BP>(juce::ParameterID(eqBandID(b, "on"), 1), n + "On", false));
        p.push_back(std::make_unique<CP>(juce::ParameterID(eqBandID(b, "type"), 1), n + "Type",
                                         eqBandTypeNames(), defType[b]));
        p.push_back(std::make_unique<FP>(juce::ParameterID(eqBandID(b, "freq"), 1), n + "Freq",
                                         freqRange(20.0f, 20000.0f), defFreq[b],
                                         juce::AudioParameterFloatAttributes().withLabel("Hz")));
        p.push_back(std::make_unique<FP>(juce::ParameterID(eqBandID(b, "gain"), 1), n + "Gain",
                                         Rng(-18.0f, 18.0f, 0.01f), 0.0f,
                                         juce::AudioParameterFloatAttributes().withLabel("dB")));
        p.push_back(std::make_unique<FP>(juce::ParameterID(eqBandID(b, "q"), 1), n + "Q",
                                         Rng(0.1f, 18.0f, 0.001f, 0.35f), 0.71f));
        // Dynamic amount (D-018): band gain shifts by up to +/-dyn dB when the
        // band's energy rises above its own adaptive reference.
        p.push_back(std::make_unique<FP>(juce::ParameterID(eqBandID(b, "dyn"), 1), n + "Dyn",
                                         Rng(-18.0f, 18.0f, 0.01f), 0.0f,
                                         juce::AudioParameterFloatAttributes().withLabel("dB")));
    }

    // ---- Linear-phase EQ mode (D-036): static curve as a 2048-tap FIR ----
    p.push_back(std::make_unique<BP>(juce::ParameterID("eq_lp", 1), "EQ Linear Phase", false));

    // ---- Match EQ (D-032): linear-phase spectral match, applied at input ----
    p.push_back(std::make_unique<BP>(juce::ParameterID("mch_on", 1), "Match On", false));
    p.push_back(pct("mch_amount", "Match Amount", 100.0f));
    p.push_back(std::make_unique<FP>(juce::ParameterID("mch_smooth", 1), "Match Smooth",
                                     Rng(0.0f, 2.0f, 0.01f), 0.33f,
                                     juce::AudioParameterFloatAttributes().withLabel("oct")));
    p.push_back(db("mch_maxdb", "Match Max", 1.0f, 30.0f, 12.0f));
    // D-039: which point the learn tap listens at, and the A/B reference slot
    p.push_back(std::make_unique<CP>(juce::ParameterID("mch_tap", 1), "Match Tap",
                                     juce::StringArray { "Input", "Output" }, 0));
    p.push_back(std::make_unique<CP>(juce::ParameterID("mch_ref", 1), "Match Reference",
                                     juce::StringArray { "A", "B" }, 0));

    // ---- Filter ----
    p.push_back(std::make_unique<CP>(juce::ParameterID("flt_type", 1), "Filter Type",
                                     filterTypeNames(), 2));
    p.push_back(std::make_unique<FP>(juce::ParameterID("flt_cutoff", 1), "Cutoff",
                                     freqRange(20.0f, 20000.0f), 1200.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("Hz")));
    p.push_back(pct("flt_res", "Resonance", 15.0f));
    p.push_back(db("flt_drive", "Filter Drive", 0.0f, 24.0f, 0.0f));

    // ---- Compressor ----
    p.push_back(db("cmp_thresh", "Threshold", -60.0f, 0.0f, -18.0f));
    p.push_back(std::make_unique<FP>(juce::ParameterID("cmp_ratio", 1), "Ratio",
                                     Rng(1.0f, 20.0f, 0.01f, 0.4f), 3.0f));
    p.push_back(std::make_unique<FP>(juce::ParameterID("cmp_attack", 1), "Attack",
                                     Rng(0.05f, 250.0f, 0.01f, 0.3f), 12.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("ms")));
    p.push_back(std::make_unique<FP>(juce::ParameterID("cmp_release", 1), "Release",
                                     Rng(5.0f, 2500.0f, 0.1f, 0.3f), 140.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("ms")));
    p.push_back(db("cmp_knee", "Knee", 0.0f, 24.0f, 9.0f));
    p.push_back(db("cmp_makeup", "Makeup", 0.0f, 24.0f, 0.0f));
    p.push_back(db("cmp_upward", "Upward", 0.0f, 12.0f, 0.0f));
    p.push_back(pct("cmp_mix", "Comp Mix", 100.0f));
    p.push_back(std::make_unique<CP>(juce::ParameterID("cmp_detect", 1), "Detector",
                                     compDetectNames(), 1));
    p.push_back(std::make_unique<CP>(juce::ParameterID("cmp_character", 1), "Character",
                                     compCharacterNames(), 0));
    p.push_back(std::make_unique<BP>(juce::ParameterID("cmp_sc", 1), "Comp Sidechain", false));

    // ---- Saturator ----
    p.push_back(std::make_unique<CP>(juce::ParameterID("sat_algo", 1), "Algorithm",
                                     satAlgoNames(), 0));
    p.push_back(db("sat_drive", "Drive", 0.0f, 36.0f, 6.0f));
    p.push_back(std::make_unique<FP>(juce::ParameterID("sat_bias", 1), "Bias",
                                     Rng(-1.0f, 1.0f, 0.001f), 0.0f));
    p.push_back(std::make_unique<FP>(juce::ParameterID("sat_tone", 1), "Tone",
                                     Rng(-1.0f, 1.0f, 0.001f), 0.0f));
    p.push_back(pct("sat_mix", "Sat Mix", 100.0f));
    p.push_back(db("sat_out", "Sat Trim", -12.0f, 12.0f, 0.0f));

    // ---- Transient ----
    p.push_back(std::make_unique<FP>(juce::ParameterID("trn_attack", 1), "Attack Amt",
                                     Rng(-100.0f, 100.0f, 0.1f), 0.0f));
    p.push_back(std::make_unique<FP>(juce::ParameterID("trn_sustain", 1), "Sustain Amt",
                                     Rng(-100.0f, 100.0f, 0.1f), 0.0f));
    p.push_back(std::make_unique<CP>(juce::ParameterID("trn_speed", 1), "Speed",
                                     transientSpeedNames(), 1));

    // ---- Modulation ----
    p.push_back(std::make_unique<CP>(juce::ParameterID("mod_mode", 1), "Mode",
                                     modModeNames(), 0));
    p.push_back(std::make_unique<FP>(juce::ParameterID("mod_rate", 1), "Rate",
                                     Rng(0.02f, 10.0f, 0.001f, 0.35f), 0.6f,
                                     juce::AudioParameterFloatAttributes().withLabel("Hz")));
    p.push_back(pct("mod_depth", "Depth", 50.0f));
    p.push_back(std::make_unique<FP>(juce::ParameterID("mod_feedback", 1), "Feedback",
                                     Rng(-0.95f, 0.95f, 0.001f), 0.0f));
    p.push_back(pct("mod_spread", "Spread", 100.0f));
    p.push_back(pct("mod_mix", "Motion Mix", 50.0f));
    // M6: host-synced LFO rate (division shares the delay's musical table)
    p.push_back(std::make_unique<BP>(juce::ParameterID("mod_sync", 1), "Motion Sync", false));
    p.push_back(std::make_unique<CP>(juce::ParameterID("mod_div", 1), "Motion Division",
                                     delayDivNames(), 6)); // 1/8

    // ---- Delay ----
    p.push_back(std::make_unique<BP>(juce::ParameterID("dly_sync", 1), "Sync", true));
    p.push_back(std::make_unique<FP>(juce::ParameterID("dly_time", 1), "Time",
                                     Rng(1.0f, 2000.0f, 0.1f, 0.3f), 350.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("ms")));
    p.push_back(std::make_unique<CP>(juce::ParameterID("dly_div", 1), "Division",
                                     delayDivNames(), 6));
    p.push_back(pct("dly_feedback", "Feedback", 35.0f));
    p.push_back(std::make_unique<BP>(juce::ParameterID("dly_pingpong", 1), "Ping Pong", false));
    p.push_back(std::make_unique<CP>(juce::ParameterID("dly_color", 1), "Color",
                                     delayColorNames(), 1));
    p.push_back(pct("dly_wow", "Wow", 20.0f));
    p.push_back(std::make_unique<FP>(juce::ParameterID("dly_locut", 1), "Low Cut",
                                     freqRange(20.0f, 1000.0f), 120.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("Hz")));
    p.push_back(std::make_unique<FP>(juce::ParameterID("dly_hicut", 1), "High Cut",
                                     freqRange(1000.0f, 20000.0f), 9000.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("Hz")));
    p.push_back(pct("dly_diffuse", "Diffuse", 0.0f));
    p.push_back(pct("dly_width", "Delay Width", 100.0f));
    p.push_back(pct("dly_mix", "Delay Mix", 25.0f));

    // ---- Reverb ----
    p.push_back(std::make_unique<CP>(juce::ParameterID("rev_type", 1), "Space",
                                     reverbTypeNames(), 1));
    p.push_back(pct("rev_size", "Size", 60.0f));
    p.push_back(std::make_unique<FP>(juce::ParameterID("rev_decay", 1), "Decay",
                                     Rng(0.2f, 30.0f, 0.01f, 0.35f), 2.6f,
                                     juce::AudioParameterFloatAttributes().withLabel("s")));
    p.push_back(std::make_unique<FP>(juce::ParameterID("rev_predelay", 1), "Pre-Delay",
                                     Rng(0.0f, 250.0f, 0.1f, 0.5f), 12.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("ms")));
    p.push_back(pct("rev_damp", "Damping", 45.0f));
    p.push_back(std::make_unique<FP>(juce::ParameterID("rev_locut", 1), "Verb Low Cut",
                                     freqRange(20.0f, 1000.0f), 140.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("Hz")));
    p.push_back(pct("rev_shimmer", "Shimmer", 0.0f));
    p.push_back(pct("rev_width", "Verb Width", 100.0f));
    p.push_back(pct("rev_mix", "Verb Mix", 22.0f));

    // ---- Stereo ----
    p.push_back(std::make_unique<FP>(juce::ParameterID("st_width", 1), "Width",
                                     Rng(0.0f, 200.0f, 0.1f), 100.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("%")));
    p.push_back(db("st_mid", "Mid", -12.0f, 12.0f, 0.0f));
    p.push_back(db("st_side", "Side", -12.0f, 12.0f, 0.0f));
    p.push_back(std::make_unique<FP>(juce::ParameterID("st_haas", 1), "Haas",
                                     Rng(0.0f, 40.0f, 0.01f), 0.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("ms")));
    p.push_back(std::make_unique<FP>(juce::ParameterID("st_pan", 1), "Pan",
                                     Rng(-1.0f, 1.0f, 0.001f), 0.0f));
    p.push_back(std::make_unique<FP>(juce::ParameterID("st_monofreq", 1), "Mono Below",
                                     Rng(0.0f, 500.0f, 1.0f, 0.5f), 0.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // ---- Gate ----
    p.push_back(db("gte_thresh", "Gate Threshold", -80.0f, 0.0f, -45.0f));
    p.push_back(db("gte_range", "Gate Range", -80.0f, 0.0f, -60.0f));
    p.push_back(std::make_unique<FP>(juce::ParameterID("gte_attack", 1), "Gate Attack",
                                     Rng(0.05f, 50.0f, 0.01f, 0.35f), 0.5f,
                                     juce::AudioParameterFloatAttributes().withLabel("ms")));
    p.push_back(std::make_unique<FP>(juce::ParameterID("gte_release", 1), "Gate Release",
                                     Rng(5.0f, 2000.0f, 0.1f, 0.3f), 100.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("ms")));
    p.push_back(std::make_unique<FP>(juce::ParameterID("gte_hold", 1), "Gate Hold",
                                     Rng(0.0f, 500.0f, 0.1f, 0.5f), 20.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("ms")));
    p.push_back(std::make_unique<CP>(juce::ParameterID("gte_mode", 1), "Gate Mode",
                                     gateModeNames(), 0));
    p.push_back(std::make_unique<BP>(juce::ParameterID("gte_sc", 1), "Gate Sidechain", false));

    // ---- Multiband compressor ----
    p.push_back(std::make_unique<FP>(juce::ParameterID("mbc_xlow", 1), "X-Over Low",
                                     freqRange(40.0f, 1000.0f), 150.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("Hz")));
    p.push_back(std::make_unique<FP>(juce::ParameterID("mbc_xhigh", 1), "X-Over High",
                                     freqRange(1000.0f, 12000.0f), 2500.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("Hz")));
    for (auto* band : { "lo", "mid", "hi" })
    {
        const auto pre = juce::String("mbc_") + band + "_";
        const auto nm  = juce::String(band).toUpperCase() + " ";
        p.push_back(std::make_unique<FP>(juce::ParameterID(pre + "thresh", 1), nm + "Threshold",
                                         Rng(-60.0f, 0.0f, 0.01f), -24.0f,
                                         juce::AudioParameterFloatAttributes().withLabel("dB")));
        p.push_back(std::make_unique<FP>(juce::ParameterID(pre + "ratio", 1), nm + "Ratio",
                                         Rng(1.0f, 10.0f, 0.01f, 0.5f), 2.5f));
        p.push_back(std::make_unique<FP>(juce::ParameterID(pre + "gain", 1), nm + "Gain",
                                         Rng(-12.0f, 12.0f, 0.01f), 0.0f,
                                         juce::AudioParameterFloatAttributes().withLabel("dB")));
    }
    p.push_back(std::make_unique<FP>(juce::ParameterID("mbc_attack", 1), "MB Attack",
                                     Rng(0.5f, 100.0f, 0.01f, 0.35f), 15.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("ms")));
    p.push_back(std::make_unique<FP>(juce::ParameterID("mbc_release", 1), "MB Release",
                                     Rng(20.0f, 1000.0f, 0.1f, 0.35f), 150.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("ms")));

    // ---- Pitch ----
    p.push_back(std::make_unique<CP>(juce::ParameterID("pit_mode", 1), "Pitch Mode",
                                     pitchModeNames(), 0));
    p.push_back(pct("pit_amount", "Pitch Amount", 25.0f));
    p.push_back(pct("pit_mix", "Pitch Mix", 50.0f));

    // ---- Limiter ----
    p.push_back(db("lim_gain", "Limiter Gain", 0.0f, 24.0f, 0.0f));
    p.push_back(db("lim_ceiling", "Ceiling", -2.0f, 0.0f, -0.3f));
    p.push_back(std::make_unique<FP>(juce::ParameterID("lim_release", 1), "Lim Release",
                                     Rng(10.0f, 1000.0f, 0.1f, 0.3f), 80.0f,
                                     juce::AudioParameterFloatAttributes().withLabel("ms")));
    p.push_back(std::make_unique<BP>(juce::ParameterID("lim_softclip", 1), "Soft Clip", true));
    p.push_back(std::make_unique<BP>(juce::ParameterID("lim_tp", 1), "True Peak", true));

    return p;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    auto p = createParameterList();
    return juce::AudioProcessorValueTreeState::ParameterLayout(p.begin(), p.end());
}

// --------------------------------------------------------- chain ordering --
/* The module order is packed into one uint64 (4 bits per slot) so the audio
   thread can read it lock-free while the UI reorders. */
inline juce::uint64 packOrder(const std::array<int, numModules>& order)
{
    juce::uint64 v = 0;
    for (int i = 0; i < numModules; ++i)
        v |= (juce::uint64) (order[(size_t) i] & 0xF) << (4 * i);
    return v;
}
inline std::array<int, numModules> unpackOrder(juce::uint64 v)
{
    std::array<int, numModules> o {};
    for (int i = 0; i < numModules; ++i)
        o[(size_t) i] = (int) ((v >> (4 * i)) & 0xF);
    return o;
}
/* Musical default: clean up first, color in the middle, space and safety last. */
inline juce::uint64 defaultOrder()
{
    static constexpr std::array<int, numModules> o {
        mGate, mEQ, mFilter, mComp, mMBComp, mSat, mTransient,
        mModFX, mPitch, mDelay, mReverb, mStereo, mLimiter };
    return packOrder(o);
}

} // namespace vesper
