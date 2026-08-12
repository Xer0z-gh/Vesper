#pragma once
/*  Panels.h — the workspace (docs/design/08). Hierarchy is curated, not
    automatic: one hero arc (the module's musical identity), a supporting
    row of mid arcs, and an advanced band — quieter, smaller, right-aligned.
    No watermark, no boxes; unfilled workspace is composed Space. */

#include "../VesperProcessor.h"
#include "CurveView.h"
#include "Knob.h"
#include "Stepper.h"
#include "EQPanel.h"

namespace vesper {

// ------------------------------------------------------------ ModulePanel --
class ModulePanel : public juce::Component
{
public:
    struct Item
    {
        enum Kind { knob, combo, toggle };
        const char* id;
        const char* label;
        Kind kind = knob;
    };
    struct Spec
    {
        Item hero;
        std::vector<Item> supporting; // knobs only (Arc M)
        std::vector<Item> advanced;   // knobs (Arc S), combos, toggles
    };

    /* D-051: the card carries its own header — enable dot, then the module
       name centred — so an open panel states what it is instead of relying
       on the strip tile above it. Straight from the Subculture reference. */
    void setModule(VesperProcessor& proc, int moduleIndex)
    {
        moduleId = moduleIndex;
        title = juce::String(moduleName(moduleIndex)).toUpperCase();
        enableAtt = std::make_unique<juce::ParameterAttachment>(
            *proc.apvts.getParameter(moduleOnID(moduleIndex)),
            [this] (float v) { enabled = v > 0.5f; repaint(); });
        enableAtt->sendInitialUpdate();
        resized();
    }

    ModulePanel(VesperProcessor& proc, Spec specIn) : spec(std::move(specIn))
    {
        hero = std::make_unique<Knob>(proc.apvts, spec.hero.id, spec.hero.label);
        hero->macroEngine = &proc.macros;
        addAndMakeVisible(*hero);

        for (const auto& item : spec.supporting)
        {
            supporting.push_back(std::make_unique<Knob>(proc.apvts, item.id, item.label));
            supporting.back()->macroEngine = &proc.macros;
            addAndMakeVisible(*supporting.back());
        }
        for (const auto& item : spec.advanced)
            makeAdvanced(proc, item);
    }

    /* Find a knob by parameter id (hero, supporting or advanced) — used by
       the factory to attach cross-parameter behaviour like sync readouts. */
    Knob* knobFor(const juce::String& id)
    {
        if (id == spec.hero.id) return hero.get();
        for (size_t i = 0; i < supporting.size(); ++i)
            if (id == spec.supporting[i].id) return supporting[i].get();
        for (size_t i = 0; i < advancedComps.size() && i < spec.advanced.size(); ++i)
            if (advancedComps[i].knob != nullptr && id == spec.advanced[i].id)
                return advancedComps[i].knob.get();
        return nullptr;
    }

    /* An unbound push button in the advanced band (no parameter behind it) —
       used for actions like loading a user IR (D-038). */
    juce::TextButton* addAction(const juce::String& text, std::function<void()> onClick)
    {
        AdvancedComp ac;
        ac.wide = true;
        ac.toggle = std::make_unique<juce::TextButton>(text);
        ac.toggle->onClick = std::move(onClick);
        addAndMakeVisible(*ac.toggle);
        auto* raw = ac.toggle.get();
        advancedComps.push_back(std::move(ac));
        resized();
        return raw;
    }

    /* D-063: gate a control on a boolean parameter, so a setting that can't
       do anything right now doesn't look live. DIVISION only reaches the
       DSP while SYNC is on (see VesperProcessor mod_sync / TimeFX dly_sync)
       — same honesty D-056 gave the MATCH button. */
    void gateOn(VesperProcessor& proc, const juce::String& boolParamID,
                const juce::String& gatedID)
    {
        int idx = -1;
        for (size_t i = 0; i < advancedComps.size() && i < spec.advanced.size(); ++i)
            if (gatedID == spec.advanced[i].id) idx = (int) i;
        if (idx < 0) return;

        auto& ac = advancedComps[(size_t) idx];
        juce::Component* target = ac.combo   ? (juce::Component*) ac.combo.get()
                                : ac.stepper ? (juce::Component*) ac.stepper.get()
                                             : nullptr;
        if (target == nullptr) return;

        auto* p = proc.apvts.getParameter(boolParamID);
        if (p == nullptr) return;
        gates.push_back(std::make_unique<juce::ParameterAttachment>(
            *p, [this, target, idx] (float v)
            {
                const bool live = v > 0.5f;
                target->setEnabled(live);
                target->setAlpha(live ? 1.0f : 0.45f);
                gatedOffIndex = live ? -1 : idx; // dims the painted caption too
                repaint();
            }));
        gates.back()->sendInitialUpdate();
    }

    /* D-066: give this card a transfer-curve tile. `watch` lists the
       parameters whose changes redraw it — the curve is only honest if it
       follows the controls. */
    void setCurve(VesperProcessor& proc, juce::String captionText,
                  std::function<float(float)> fn,
                  std::initializer_list<const char*> watch)
    {
        curveView = std::make_unique<CurveView>();
        curveView->caption = std::move(captionText);
        curveView->shape = std::move(fn);
        addAndMakeVisible(*curveView);
        for (const char* id : watch)
            if (auto* p = proc.apvts.getParameter(id))
                curveWatch.push_back(std::make_unique<juce::ParameterAttachment>(
                    *p, [this] (float) { if (curveView) curveView->repaint(); }));
        resized();
    }

    /* Owns async file choosers launched from action buttons. */
    std::unique_ptr<juce::FileChooser> chooser;

    juce::TextButton* actionButton()
    {
        return advancedComps.empty() ? nullptr : advancedComps.back().toggle.get();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(theme::pad, 4);
        // D-065: 2px under the card header put the module title right on top
        // of the first control row; give the header its own breathing space.
        if (moduleId >= 0) { headerArea = r.removeFromTop(headerH); r.removeFromTop(7); }

        // Hero zone: 280 wide, Arc L (152) centred
        auto heroZone = r.removeFromLeft(280);
        hero->setBounds(heroZone.withSizeKeepingCentre(theme::sizeL,
                                                       juce::jmin(heroZone.getHeight(),
                                                                  theme::sizeL + 16)));
        r.removeFromLeft(theme::gap);

        // Supporting row (upper) — Arc M, left-aligned
        auto top = r.removeFromTop(r.getHeight() * 3 / 5);
        int x = top.getX();
        for (auto& k : supporting)
        {
            const int side = juce::jmin(theme::sizeM + 14, top.getHeight());
            k->setBounds(juce::Rectangle<int>(x, top.getY(), theme::sizeM + 14, top.getHeight())
                             .withSizeKeepingCentre(side, side));
            x += theme::sizeM + 14 + theme::gap;
        }

        /* D-066: the transfer curve tile sits in the bottom-left of the
           workspace — the space the supporting row leaves empty — so it
           reads beside the controls that shape it, never over them. Only
           modules that define a curve get one; the rest keep their Space. */
        auto adv = r;
        if (curveView != nullptr)
        {
            // keep clear of the supporting row's value line above it (D-065)
            const int side = juce::jmin(adv.getHeight() - 8, 96);
            adv.removeFromLeft(theme::gap);
            curveView->setBounds(adv.removeFromLeft(side + theme::gap)
                                    .withSizeKeepingCentre(side, side));
        }
        int xr = adv.getRight();
        for (auto it = advancedComps.rbegin(); it != advancedComps.rend(); ++it)
        {
            auto& ac = *it;
            const int w = ac.toggle != nullptr ? 84 : ac.wide ? 96 : 64;
            if (xr - w < adv.getX() + 4) break; // never intrude into the hero zone
            auto cell = juce::Rectangle<int>(xr - w, adv.getY(), w, adv.getHeight());
            if (ac.knob != nullptr)
            {
                const int side = juce::jmin(64, cell.getHeight());
                ac.knob->setBounds(cell.withSizeKeepingCentre(62, side));
            }
            else if (ac.stepper != nullptr)
            {
                ac.stepper->setBounds(cell.withSizeKeepingCentre(w, 48));
            }
            else if (ac.combo != nullptr)
            {
                // D-065: caption gets its own line clear of the field
                auto inner = cell.withSizeKeepingCentre(w, 44);
                ac.captionArea = inner.removeFromTop(12);
                inner.removeFromTop(3);
                ac.combo->setBounds(inner.reduced(0, 2));
            }
            else if (ac.toggle != nullptr)
            {
                ac.toggle->setBounds(cell.withSizeKeepingCentre(w, 24));
            }
            xr -= w + theme::gap / 2;
        }
    }

    /* The card header (D-051) plus captions for the advanced-band selectors.
       Painted, so both re-read the palette on every theme switch (D-049). */
    void paintOverChildren(juce::Graphics& g) override
    {
        auto& p = theme::palette();

        if (moduleId >= 0 && ! headerArea.isEmpty())
        {
            /* Title only — no enable dot here (D-051). The strip tile above
               already carries this module's enable point; a second control
               for the same parameter is redundant, and the header's job is
               to say which card is open. Its state is still legible: the
               title dims when the module is off. */
            g.setFont(theme::labelFont(10.5f));
            g.setColour(enabled ? p.text.withAlpha(0.88f) : p.textFaint);
            g.drawText(title, headerArea, juce::Justification::centred);

            g.setColour(p.ink(0.07f));
            g.fillRect((float) headerArea.getX(), (float) headerArea.getBottom() - 1.0f,
                       (float) headerArea.getWidth(), 1.0f);
        }

        g.setFont(theme::microFont(9.0f));
        for (size_t i = 0; i < advancedComps.size(); ++i)
        {
            const auto& ac = advancedComps[i];
            if (ac.caption.isEmpty() || ac.captionArea.isEmpty()) continue;
            g.setColour((int) i == gatedOffIndex ? p.textFaint.withAlpha(0.45f)
                                                 : p.textFaint);
            g.drawText(ac.caption, ac.captionArea, juce::Justification::centredLeft);
        }
    }


private:
    static constexpr int headerH = 18;

    struct AdvancedComp
    {
        std::unique_ptr<Knob> knob;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<Stepper> stepper;      // short choice lists (D-051)
        std::unique_ptr<juce::TextButton> toggle;
        juce::String caption;            // painted, so it follows the theme
        juce::Rectangle<int> captionArea;
        bool wide = false;
    };

    std::vector<std::unique_ptr<juce::ParameterAttachment>> gates; // D-063
    int gatedOffIndex = -1;   // advanced control currently inert (dimmed)

    std::unique_ptr<CurveView> curveView;                          // D-066
    std::vector<std::unique_ptr<juce::ParameterAttachment>> curveWatch;

    void makeAdvanced(VesperProcessor& proc, const Item& item)
    {
        AdvancedComp ac;
        switch (item.kind)
        {
            case Item::knob:
                ac.knob = std::make_unique<Knob>(proc.apvts, item.id, item.label);
                ac.knob->macroEngine = &proc.macros;
                addAndMakeVisible(*ac.knob);
                break;
            case Item::combo:
            {
                ac.wide = true;
                /* Short lists become ‹ value › steppers (D-051): the whole
                   range is visible, one click changes it, and the wheel
                   scrubs. Long lists (17 saturation algorithms) stay a
                   pick-list — stepping those would be worse, not better. */
                if (auto* ch = dynamic_cast<juce::AudioParameterChoice*>(
                        proc.apvts.getParameter(item.id)))
                {
                    if (ch->choices.size() <= Stepper::maxDots)
                    {
                        ac.stepper = std::make_unique<Stepper>(
                            proc.apvts, item.id, juce::String(item.label).toUpperCase());
                        addAndMakeVisible(*ac.stepper);
                        advancedComps.push_back(std::move(ac));
                        return;
                    }
                }
                ac.combo = std::make_unique<juce::ComboBox>();
                if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(
                        proc.apvts.getParameter(item.id)))
                    ac.combo->addItemList(p->choices, 1);
                ac.combo->setTitle(item.label);
                ac.combo->setTooltip(item.label);
                comboAtts.push_back(
                    std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                        proc.apvts, item.id, *ac.combo));
                addAndMakeVisible(*ac.combo);
                /* The caption is painted, not a Label (D-049). A Label given
                   an explicit colour keeps it across a theme switch and goes
                   invisible in the other mode — "ALGORITHM" disappeared in
                   light. Painting re-reads the palette every frame, which is
                   also what EQPanel already does for its TYPE caption. */
                ac.caption = juce::String(item.label).toUpperCase();
                break;
            }
            case Item::toggle:
                ac.wide = true;
                ac.toggle = std::make_unique<juce::TextButton>(item.label);
                ac.toggle->setClickingTogglesState(true);
                toggleAtts.push_back(
                    std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                        proc.apvts, item.id, *ac.toggle));
                addAndMakeVisible(*ac.toggle);
                break;
        }
        advancedComps.push_back(std::move(ac));
    }

    Spec spec;
    int moduleId = -1;                 // -1 = no header (EQ panel draws its own)
    juce::String title;
    bool enabled = false;
    std::unique_ptr<juce::ParameterAttachment> enableAtt;
    juce::Rectangle<int> headerArea;
    std::unique_ptr<Knob> hero;
    std::vector<std::unique_ptr<Knob>> supporting;
    std::vector<AdvancedComp> advancedComps;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtts;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> toggleAtts;
};

// --------------------------------------------------------- panel factory ---
/* Tiers per docs/design/08 — the hero is the module's musical identity,
   curated there and only there. */
inline std::unique_ptr<juce::Component> makePanelFor(int module, VesperProcessor& proc)
{
    using I = ModulePanel::Item;
    auto make = [&, module] (ModulePanel::Spec spec)
    {
        auto p = std::make_unique<ModulePanel>(proc, std::move(spec));
        p->setModule(proc, module);   // D-051: card header (name + enable dot)
        return p;
    };
    switch (module)
    {
        case mEQ: return std::make_unique<EQPanel>(proc);
        case mFilter: return make({ { "flt_cutoff", "CUTOFF" },
            { { "flt_res", "RESO" }, { "flt_drive", "DRIVE" } },
            { { "flt_type", "MODEL", I::combo } } });
        case mComp: return make({ { "cmp_thresh", "THRESH" },
            { { "cmp_ratio", "RATIO" }, { "cmp_attack", "ATTACK" }, { "cmp_release", "RELEASE" } },
            { { "cmp_knee", "KNEE" }, { "cmp_makeup", "MAKEUP" }, { "cmp_upward", "UPWARD" },
              { "cmp_mix", "MIX" }, { "cmp_detect", "DETECT", I::combo },
              { "cmp_character", "STYLE", I::combo },
              { "cmp_sc", "SIDECHAIN", I::toggle } } });
        case mSat:
        {
            auto panel = make({ { "sat_drive", "DRIVE" },
                { { "sat_mix", "MIX" }, { "sat_tone", "TONE" } },
                { { "sat_bias", "BIAS" }, { "sat_out", "TRIM" },
                  { "sat_algo", "ALGORITHM", I::combo } } });
            /* D-066: the saturator's character IS its curve — plot the real
               shaper the audio thread runs, driven by the live Drive/Bias,
               so the picture can never drift from the sound. */
            panel->setCurve(proc, "CURVE",
                [&proc] (float x)
                {
                    const int   algo  = (int) proc.apvts.getRawParameterValue("sat_algo")->load();
                    const float drive = dsp::dbToGain(
                        proc.apvts.getRawParameterValue("sat_drive")->load());
                    const float bias  = proc.apvts.getRawParameterValue("sat_bias")->load();
                    return dsp::Saturator::shapeStatic(algo, x * drive, bias);
                },
                { "sat_algo", "sat_drive", "sat_bias" });
            return panel;
        }
        case mTransient: return make({ { "trn_attack", "ATTACK" },
            { { "trn_sustain", "SUSTAIN" } },
            { { "trn_speed", "SPEED", I::combo } } });
        case mModFX:
        {
            auto panel = make({ { "mod_depth", "DEPTH" },
                { { "mod_rate", "RATE" }, { "mod_mix", "MIX" } },
                { { "mod_feedback", "FEEDBACK" }, { "mod_spread", "SPREAD" },
                  { "mod_div", "DIVISION", I::combo }, { "mod_mode", "MODE", I::combo },
                  { "mod_sync", "SYNC", I::toggle } } });
            if (auto* k = panel->knobFor("mod_rate")) // M6: "1/8 SYNC" readout
                k->followSync(proc.apvts, "mod_sync", "mod_div");
            panel->gateOn(proc, "mod_sync", "mod_div"); // D-063: inert unless synced
            return panel;
        }
        case mDelay:
        {
            auto panel = make({ { "dly_feedback", "FEEDBACK" },
                { { "dly_time", "TIME" }, { "dly_mix", "MIX" } },
                { { "dly_wow", "WOW" }, { "dly_locut", "LO CUT" }, { "dly_hicut", "HI CUT" },
                  { "dly_diffuse", "DIFFUSE" }, { "dly_width", "WIDTH" },
                  { "dly_div", "DIVISION", I::combo }, { "dly_color", "COLOR", I::combo },
                  { "dly_sync", "SYNC", I::toggle }, { "dly_pingpong", "PING", I::toggle } } });
            if (auto* k = panel->knobFor("dly_time")) // M6: "1/8 SYNC" readout
                k->followSync(proc.apvts, "dly_sync", "dly_div");
            panel->gateOn(proc, "dly_sync", "dly_div"); // D-063: inert unless synced
            return panel;
        }
        // SPACE spans the FDN spaces, the convolution spaces (D-029) and
        // User IR (D-038); SHIMMER now applies to every space.
        case mReverb:
        {
            auto panel = make({ { "rev_decay", "DECAY" },
                { { "rev_size", "SIZE" }, { "rev_mix", "MIX" } },
                { { "rev_predelay", "PRE-DLY" }, { "rev_damp", "DAMP" },
                  { "rev_locut", "LO CUT" }, { "rev_shimmer", "SHIMMER" },
                  { "rev_width", "WIDTH" }, { "rev_type", "SPACE", I::combo } } });
            auto* pnl = panel.get();
            auto label = [&proc] {
                if (proc.userIRFailed()) return juce::String("IR MISSING");
                const auto n = proc.getUserIRName();
                return n.isEmpty() ? juce::String("LOAD IR")
                                   : n.substring(0, 12).toUpperCase();
            };
            juce::TextButton* btn = pnl->addAction(label(), [pnl, &proc, label]
            {
                pnl->chooser = std::make_unique<juce::FileChooser>(
                    "Load an impulse response", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
                pnl->chooser->launchAsync(
                    juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectFiles,
                    [pnl, &proc, label] (const juce::FileChooser& fc)
                    {
                        const auto f = fc.getResult();
                        if (f == juce::File()) return; // cancelled
                        proc.loadUserIR(f);
                        // switch SPACE to User IR so you hear what you loaded
                        if (auto* t = proc.apvts.getParameter("rev_type"))
                        {
                            t->beginChangeGesture();
                            t->setValueNotifyingHost(t->convertTo0to1(7.0f));
                            t->endChangeGesture();
                        }
                        if (auto* b = pnl->actionButton())
                            b->setButtonText(label());
                    });
            });
            btn->setTooltip("Load a .wav/.aiff impulse response into the User IR space");
            return panel;
        }
        case mStereo: return make({ { "st_width", "WIDTH" },
            { { "st_mid", "MID" }, { "st_side", "SIDE" } },
            { { "st_haas", "HAAS" }, { "st_pan", "PAN" }, { "st_monofreq", "MONO" } } });
        case mLimiter: return make({ { "lim_gain", "GAIN" },
            { { "lim_ceiling", "CEILING" } },
            { { "lim_release", "RELEASE" }, { "lim_softclip", "SOFT CLIP", I::toggle },
              { "lim_tp", "TRUE PEAK", I::toggle } } });
        case mGate: return make({ { "gte_thresh", "THRESH" },
            { { "gte_range", "RANGE" }, { "gte_release", "RELEASE" } },
            { { "gte_attack", "ATTACK" }, { "gte_hold", "HOLD" },
              { "gte_mode", "MODE", I::combo },
              { "gte_sc", "SIDECHAIN", I::toggle } } });
        case mMBComp: return make({ { "mbc_xlow", "X-LOW" },
            { { "mbc_xhigh", "X-HIGH" }, { "mbc_lo_thresh", "LO THR" },
              { "mbc_mid_thresh", "MID THR" }, { "mbc_hi_thresh", "HI THR" } },
            { { "mbc_lo_ratio", "LO RAT" }, { "mbc_mid_ratio", "MID RAT" },
              { "mbc_hi_ratio", "HI RAT" }, { "mbc_lo_gain", "LO GAIN" },
              { "mbc_mid_gain", "MID GAIN" }, { "mbc_hi_gain", "HI GAIN" },
              { "mbc_attack", "ATTACK" }, { "mbc_release", "RELEASE" } } });
        case mPitch: return make({ { "pit_amount", "AMOUNT" },
            { { "pit_mix", "MIX" } },
            { { "pit_mode", "MODE", I::combo } } });
        default: return std::make_unique<juce::Component>();
    }
}

} // namespace vesper
