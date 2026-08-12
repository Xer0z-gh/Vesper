#pragma once
/*  Creative.h — the intelligent-workflow layer:
      * MacroMoves  — curated one-gesture transformations ("More Punch",
                      "Wider", "Air"…) applied as a single undoable step.
      * Randomizer  — musical randomization with per-parameter policies,
                      never touching gain staging or bypass.
    Both operate on the message thread through normal parameter gestures, so
    every action is host-automatable and undoable. */

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include "Params.h"

namespace vesper {

// ------------------------------------------------------------- MacroEngine --
/* The macro system (D-020): M1–M4 drive user-assigned parameter targets.
   Assignments live in the session state (MACROS tree: TARGET {macro, param,
   lo, hi} with normalized endpoints), so they save with the project.
   Macro changes may arrive on the audio thread (host automation) — trigger()
   is realtime-safe and the targets are applied on the message thread. */
class MacroEngine : private juce::AsyncUpdater
{
public:
    explicit MacroEngine(juce::AudioProcessorValueTreeState& s) : apvts(s) {}
    ~MacroEngine() override { cancelPendingUpdate(); }

    static bool assignable(const juce::String& paramID)
    {
        return ! paramID.startsWith("macro") && paramID != "bypass";
    }

    void assign(const juce::String& paramID, int macro /*1..4*/)
    {
        if (! assignable(paramID)) return;
        remove(paramID); // a parameter follows at most one macro
        auto t = ensureTree();
        juce::ValueTree target("TARGET");
        target.setProperty("macro", juce::jlimit(1, 4, macro), nullptr);
        target.setProperty("param", paramID, nullptr);
        target.setProperty("lo", 0.0, nullptr);
        target.setProperty("hi", 1.0, nullptr);
        t.appendChild(target, nullptr);
        trigger();
        if (onAssignmentsChanged) onAssignmentsChanged();
    }

    void remove(const juce::String& paramID)
    {
        auto t = apvts.state.getChildWithName("MACROS");
        if (! t.isValid()) return;
        for (int i = t.getNumChildren(); --i >= 0;)
            if (t.getChild(i).getProperty("param").toString() == paramID)
                t.removeChild(i, nullptr);
        if (onAssignmentsChanged) onAssignmentsChanged();
    }

    /* Message thread: fired when the target set changes, so the M knobs can
       refresh their assignment-count badges (D-041). */
    std::function<void()> onAssignmentsChanged;

    /* How many parameters follow a macro (1..4) — the M knobs show it so an
       assigned macro is visibly different from an inert one (D-041). */
    int targetCount(int macro) const
    {
        const auto t = apvts.state.getChildWithName("MACROS");
        int n = 0;
        for (int i = 0; i < t.getNumChildren(); ++i)
            if ((int) t.getChild(i).getProperty("macro") == macro) ++n;
        return n;
    }

    /* 0 = unassigned, else 1..4. */
    int macroFor(const juce::String& paramID) const
    {
        const auto t = apvts.state.getChildWithName("MACROS");
        for (int i = 0; i < t.getNumChildren(); ++i)
            if (t.getChild(i).getProperty("param").toString() == paramID)
                return (int) t.getChild(i).getProperty("macro");
        return 0;
    }

    /* Pin one end of the target's travel to the parameter's current value. */
    void setEndToCurrent(const juce::String& paramID, bool hiEnd)
    {
        auto t = apvts.state.getChildWithName("MACROS");
        auto* p = apvts.getParameter(paramID);
        if (! t.isValid() || p == nullptr) return;
        for (int i = 0; i < t.getNumChildren(); ++i)
        {
            auto child = t.getChild(i);
            if (child.getProperty("param").toString() == paramID)
                child.setProperty(hiEnd ? "hi" : "lo", (double) p->getValue(), nullptr);
        }
    }

    void trigger() { triggerAsyncUpdate(); }  // safe from any thread

    /* Message-thread apply (public so tests can drive it synchronously). */
    void applyNow()
    {
        const auto t = apvts.state.getChildWithName("MACROS");
        if (! t.isValid()) return;
        float mv[4] = {};
        for (int m = 0; m < 4; ++m)
            if (auto* raw = apvts.getRawParameterValue("macro" + juce::String(m + 1)))
                mv[m] = raw->load();

        for (int i = 0; i < t.getNumChildren(); ++i)
        {
            const auto target = t.getChild(i);
            auto* p = apvts.getParameter(target.getProperty("param").toString());
            if (p == nullptr) continue;
            const float lo = (float) (double) target.getProperty("lo");
            const float hi = (float) (double) target.getProperty("hi");
            const int   m  = juce::jlimit(1, 4, (int) target.getProperty("macro")) - 1;
            const float v  = lo + (hi - lo) * mv[m];
            if (std::abs(p->getValue() - v) > 1.0e-4f)
                p->setValueNotifyingHost(v);
        }
    }

private:
    juce::ValueTree ensureTree()
    {
        auto t = apvts.state.getChildWithName("MACROS");
        if (! t.isValid())
        {
            t = juce::ValueTree("MACROS");
            apvts.state.appendChild(t, nullptr);
        }
        return t;
    }

    void handleAsyncUpdate() override { applyNow(); }

    juce::AudioProcessorValueTreeState& apvts;
};

// ------------------------------------------------------------- MorphEngine --
/* Morphs every parameter between the live state (t = 0) and the A/B "other"
   slot (t = 1). Endpoints are captured when a gesture starts; positions are
   applied as ordinary parameter moves, so morphing is host-visible,
   automatable per-parameter, and one undo step. */
class MorphEngine
{
public:
    void begin(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& otherState)
    {
        targets.clear();
        if (apvts.undoManager != nullptr)
            apvts.undoManager->beginNewTransaction("Morph A/B");

        // Index the other slot's plain values by parameter id.
        std::map<juce::String, float> other;
        for (int i = 0; i < otherState.getNumChildren(); ++i)
        {
            const auto child = otherState.getChild(i);
            if (child.hasType("PARAM"))
                other[child.getProperty("id").toString()] =
                    (float) (double) child.getProperty("value");
        }

        for (auto* p : apvts.processor.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p);
            if (rp == nullptr) continue;
            Target t;
            t.param = rp;
            t.from  = rp->getValue();
            const auto it = other.find(rp->paramID);
            t.to = it != other.end() ? rp->convertTo0to1(it->second) : t.from;
            if (std::abs(t.to - t.from) > 1.0e-6f)
                targets.push_back(t);
        }
    }

    void apply(float t)
    {
        for (auto& tg : targets)
            tg.param->setValueNotifyingHost(tg.from + (tg.to - tg.from) * t);
    }

    bool active() const noexcept { return ! targets.empty(); }
    void finish() noexcept { targets.clear(); }

private:
    struct Target { juce::RangedAudioParameter* param = nullptr; float from = 0.0f, to = 0.0f; };
    std::vector<Target> targets;
};

// ------------------------------------------------------------- MacroMoves --
/* A move is a set of relative parameter nudges expressed in *plain* units.
   Amount in [0,1] scales the whole gesture. */
struct MacroMove
{
    struct Nudge { const char* id; float delta; bool absolute = false; bool enableModule = false; };
    const char* name;
    std::vector<Nudge> nudges;
};

inline const std::vector<MacroMove>& macroMoves()
{
    static const std::vector<MacroMove> moves {
        { "More Punch",  { { "cmp_on", 1.0f, true, true }, { "cmp_attack", 18.0f, true },
                           { "cmp_release", 90.0f, true }, { "cmp_ratio", 1.5f },
                           { "trn_on", 1.0f, true, true }, { "trn_attack", 35.0f } } },
        { "Wider",       { { "st_on", 1.0f, true, true }, { "st_width", 40.0f },
                           { "mod_on", 1.0f, true, true }, { "mod_mode", 2.0f, true },
                           { "mod_mix", 18.0f } } },
        { "Brighter",    { { "eq_on", 1.0f, true, true }, { "eq_b6_on", 1.0f, true },
                           { "eq_b6_gain", 3.5f } } },
        { "Darker",      { { "eq_on", 1.0f, true, true }, { "eq_b6_on", 1.0f, true },
                           { "eq_b6_gain", -4.0f }, { "sat_tone", -0.3f } } },
        { "Air",         { { "eq_on", 1.0f, true, true }, { "eq_b6_on", 1.0f, true },
                           { "eq_b6_freq", 14500.0f, true }, { "eq_b6_gain", 4.0f },
                           { "eq_b6_q", 0.5f, true } } },
        { "Vintage",     { { "sat_on", 1.0f, true, true }, { "sat_algo", 1.0f, true },
                           { "sat_drive", 8.0f }, { "dly_color", 1.0f, true },
                           { "eq_on", 1.0f, true, true }, { "eq_b6_on", 1.0f, true },
                           { "eq_b6_gain", -2.0f } } },
        { "Aggressive",  { { "sat_on", 1.0f, true, true }, { "sat_algo", 10.0f, true },
                           { "sat_drive", 14.0f }, { "cmp_on", 1.0f, true, true },
                           { "cmp_ratio", 3.0f }, { "cmp_thresh", -8.0f } } },
        { "Smooth",      { { "sat_on", 1.0f, true, true }, { "sat_algo", 3.0f, true },
                           { "sat_drive", 3.0f }, { "cmp_on", 1.0f, true, true },
                           { "cmp_knee", 6.0f }, { "cmp_attack", 10.0f } } },
        { "Glue",        { { "cmp_on", 1.0f, true, true }, { "cmp_ratio", 0.8f },
                           { "cmp_thresh", -4.0f }, { "cmp_attack", 30.0f, true },
                           { "cmp_release", 120.0f, true }, { "cmp_knee", 12.0f, true } } },
        { "Loud",        { { "lim_on", 1.0f, true, true }, { "lim_gain", 5.0f },
                           { "sat_on", 1.0f, true, true }, { "sat_drive", 4.0f } } },
        { "Dreamy",      { { "rev_on", 1.0f, true, true }, { "rev_mix", 18.0f },
                           { "rev_decay", 3.0f }, { "rev_shimmer", 30.0f },
                           { "mod_on", 1.0f, true, true }, { "mod_mix", 15.0f } } },
        { "Huge",        { { "rev_on", 1.0f, true, true }, { "rev_type", 1.0f, true },
                           { "rev_size", 25.0f }, { "rev_decay", 4.0f },
                           { "st_on", 1.0f, true, true }, { "st_width", 30.0f } } },
        { "Intimate",    { { "rev_on", 1.0f, true, true }, { "rev_type", 0.0f, true },
                           { "rev_size", -25.0f }, { "rev_mix", 10.0f },
                           { "cmp_on", 1.0f, true, true }, { "cmp_thresh", -6.0f } } },
        { "Clean",       { { "sat_on", 0.0f, true }, { "eq_on", 1.0f, true, true },
                           { "eq_b1_on", 1.0f, true } } },
        { "Dirty",       { { "sat_on", 1.0f, true, true }, { "sat_algo", 13.0f, true },
                           { "sat_drive", 10.0f }, { "flt_on", 1.0f, true, true },
                           { "flt_cutoff", -4000.0f } } },
        { "Modern",      { { "mbc_on", 1.0f, true, true }, { "mbc_hi_thresh", -30.0f, true },
                           { "eq_on", 1.0f, true, true }, { "eq_b6_on", 1.0f, true },
                           { "eq_b6_gain", 2.5f }, { "st_on", 1.0f, true, true },
                           { "st_width", 20.0f } } },
        { "Presence",    { { "eq_on", 1.0f, true, true }, { "eq_b5_on", 1.0f, true },
                           { "eq_b5_freq", 3400.0f, true }, { "eq_b5_gain", 3.0f },
                           { "sat_on", 1.0f, true, true }, { "sat_algo", 6.0f, true },
                           { "sat_drive", 3.0f } } },
    };
    return moves;
}

inline void applyMacroMove(juce::AudioProcessorValueTreeState& apvts,
                           const MacroMove& move, float amount = 1.0f)
{
    if (apvts.undoManager != nullptr)
        apvts.undoManager->beginNewTransaction(juce::String("Apply: ") + move.name);

    for (const auto& n : move.nudges)
    {
        auto* p = apvts.getParameter(n.id);
        if (p == nullptr) continue;
        auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p);
        if (rp == nullptr) continue;

        const float current = rp->convertFrom0to1(rp->getValue());
        const float target  = (n.absolute || n.enableModule)
                                  ? n.delta
                                  : current + n.delta * amount;
        rp->beginChangeGesture();
        rp->setValueNotifyingHost(rp->convertTo0to1(target));
        rp->endChangeGesture();
    }
}

// -------------------------------------------------------------- Randomizer --
/* Policy per parameter: within musical bounds, gains untouched, timing snaps
   handled by choice params. Focus lets the dice hit one module or the world. */
class Randomizer
{
public:
    /* focusModule == -1 randomizes the whole chain (gently). */
    static void randomize(juce::AudioProcessorValueTreeState& apvts,
                          int focusModule = -1, float wildness = 0.5f)
    {
        auto& rng = juce::Random::getSystemRandom();
        if (apvts.undoManager != nullptr)
            apvts.undoManager->beginNewTransaction("Randomize");

        struct Policy { const char* id; float lo; float hi; };
        // Plain-unit musical ranges. Anything not listed is never randomized.
        static const std::vector<std::pair<int, Policy>> policies {
            { mFilter, { "flt_cutoff", 150.0f, 12000.0f } },
            { mFilter, { "flt_res", 0.0f, 65.0f } },
            { mFilter, { "flt_type", 0.0f, 5.0f } },
            { mSat,    { "sat_algo", 0.0f, 14.0f } },
            { mSat,    { "sat_drive", 2.0f, 20.0f } },
            { mSat,    { "sat_tone", -0.6f, 0.6f } },
            { mComp,   { "cmp_thresh", -32.0f, -8.0f } },
            { mComp,   { "cmp_ratio", 1.5f, 6.0f } },
            { mComp,   { "cmp_attack", 2.0f, 40.0f } },
            { mComp,   { "cmp_release", 60.0f, 400.0f } },
            { mModFX,  { "mod_mode", 0.0f, 7.0f } },
            { mModFX,  { "mod_rate", 0.1f, 4.0f } },
            { mModFX,  { "mod_depth", 20.0f, 80.0f } },
            { mModFX,  { "mod_mix", 15.0f, 60.0f } },
            { mDelay,  { "dly_div", 3.0f, 10.0f } },
            { mDelay,  { "dly_feedback", 15.0f, 60.0f } },
            { mDelay,  { "dly_color", 0.0f, 2.0f } },
            { mDelay,  { "dly_mix", 10.0f, 40.0f } },
            { mReverb, { "rev_type", 0.0f, 3.0f } },
            { mReverb, { "rev_size", 20.0f, 90.0f } },
            { mReverb, { "rev_decay", 0.8f, 8.0f } },
            { mReverb, { "rev_shimmer", 0.0f, 60.0f } },
            { mReverb, { "rev_mix", 8.0f, 40.0f } },
            { mTransient, { "trn_attack", -40.0f, 60.0f } },
            { mTransient, { "trn_sustain", -40.0f, 40.0f } },
            { mStereo, { "st_width", 80.0f, 160.0f } },
            { mPitch,  { "pit_mode", 0.0f, 4.0f } },
            { mPitch,  { "pit_amount", 10.0f, 60.0f } },
            { mPitch,  { "pit_mix", 15.0f, 50.0f } },
            { mMBComp, { "mbc_lo_thresh", -35.0f, -15.0f } },
            { mMBComp, { "mbc_hi_thresh", -35.0f, -15.0f } },
            { mEQ,     { "eq_b3_gain", -4.0f, 4.0f } },
            { mEQ,     { "eq_b4_gain", -4.0f, 4.0f } },
            { mEQ,     { "eq_b6_gain", -3.0f, 5.0f } },
        };

        for (const auto& [module, pol] : policies)
        {
            if (focusModule >= 0 && module != focusModule) continue;
            // Whole-chain mode only mutates ~40% of eligible params per roll.
            if (focusModule < 0 && rng.nextFloat() > 0.4f) continue;

            auto* p = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(pol.id));
            if (p == nullptr) continue;

            const float span   = (pol.hi - pol.lo) * juce::jlimit(0.15f, 1.0f, wildness);
            const float centre = (pol.hi + pol.lo) * 0.5f;
            const float value  = centre + (rng.nextFloat() - 0.5f) * span;
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(value));
            p->endChangeGesture();
        }

        // Focused module gets switched on so you always hear the result.
        if (focusModule >= 0)
            if (auto* on = apvts.getParameter(moduleOnID(focusModule)))
            {
                on->beginChangeGesture();
                on->setValueNotifyingHost(1.0f);
                on->endChangeGesture();
            }
    }
};

} // namespace vesper
