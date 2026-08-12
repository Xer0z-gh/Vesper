#pragma once
/*  VesperEditor — layout, keyboard map and scaling.

    ┌──────────────────────── header ────────────────────────┐
    │ VESPER   ‹ preset ›  +        ↩ ↪  A ⇥  Moves ⚄  4x ⚙ ⏻│
    ├──────────────────────── analyzer ──────────────────────┤
    ├──────────────── module rack (drag to reorder) ─────────┤
    ├──────────────── selected module panel ─────────────────┤
    ├─ in · out · mix ─┬─ macros 1-4 ─┬───── meters ─────────┤
    └─────────────────────────────────────────────────────────┘

    Keyboard: Ctrl+Z/Ctrl+Shift+Z undo/redo · P presets · X A/B · B bypass
              R randomize selected · [ ] prev/next preset · 1-0 select module */

#include "VesperProcessor.h"
#include "core/Creative.h"
#include "ui/AnalyzerView.h"
#include "ui/HeaderBar.h"
#include "ui/Knob.h"
#include "ui/MeterStrip.h"
#include "ui/ModuleRack.h"
#include "ui/Panels.h"
#include "ui/Motion.h"
#include "ui/PresetBrowser.h"
#include "ui/VesperLNF.h"

class VesperEditor : public juce::AudioProcessorEditor
{
public:
    explicit VesperEditor(VesperProcessor&);
    ~VesperEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void parentHierarchyChanged() override;

    static constexpr int baseWidth  = 1080;
    static constexpr int baseHeight = 680;

private:
    // ctor wiring, one concern each (10-AUDIT Workstream 4)
    void initPanels();
    void initKnobs();
    void initMorph();
    void initOverlays();
    void initHeaderCallbacks();
    void initSizing();

    void selectModule(int module);
    void setUIScale(float scale, bool store = true);

    /* First-run UI scale, chosen from the display rather than hardcoded, so
       the plugin opens at a usable size on a 4K panel and still fits a
       laptop (D-050; D-060 raised the target after "too small" feedback).
       Reads the display the editor is actually ON when a peer exists — at
       ctor time in a host the primary display can be the wrong monitor. */
    float autoScaleForDisplay() const
    {
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const auto* d = getPeer() != nullptr
                            ? displays.getDisplayForRect(getScreenBounds())
                            : displays.getPrimaryDisplay();
        if (d != nullptr)
        {
            const auto usable = (float) d->userArea.getHeight();
            if (usable > 200.0f)                       // sane reading
                return juce::jlimit(1.0f, 2.5f, usable * 0.72f / (float) baseHeight);
        }
        return 1.0f;
    }
    void applyStandaloneChrome(); // D-033: theme the standalone shell

    /* Uniform fit of the fixed reference canvas into whatever bounds we are
       given, centred — never truncated (D-044). */
    float contentScale() const
    {
        return juce::jmin((float) getWidth()  / (float) baseWidth,
                          (float) getHeight() / (float) baseHeight);
    }
    juce::Point<float> contentOrigin() const
    {
        const float s = contentScale();
        return { ((float) getWidth()  - baseWidth  * s) * 0.5f,
                 ((float) getHeight() - baseHeight * s) * 0.5f };
    }

    VesperProcessor& proc;
    vesper::VesperLNF lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Component content; // fixed-size canvas, scaled by transform

    vesper::HeaderBar header { proc };
    vesper::AnalyzerView analyzer { proc.fftTap };
    vesper::ModuleRack rack { proc };
    std::array<std::unique_ptr<juce::Component>, vesper::numModules> panels;
    vesper::EQPanel* eqPanel = nullptr;

    std::unique_ptr<vesper::Knob> inKnob, outKnob, mixKnob;
    std::array<std::unique_ptr<vesper::Knob>, 4> macroKnobs;
    vesper::MorphEngine morphEngine;
    juce::Slider morphSlider;
    juce::Label  morphLabel;             // caption above; value painted (D-056)
    juce::Rectangle<int> morphValueRect; // value line under the morph knob
    juce::String morphNumeral { "0" };
    vesper::MeterStrip meters { proc };
    vesper::PresetBrowser browser { proc };

    juce::Rectangle<int> panelArea, wsRect, perfRect;
    int sepX1 = 0, sepX2 = 0; // performance-group separators (unscaled)
    float uiScale = 1.0f;
    vesper::motion::Tween bloomTween;
    std::unique_ptr<juce::Component> mutedBackplate; // D-033 (standalone only)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VesperEditor)
};
