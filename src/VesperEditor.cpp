#include "VesperEditor.h"
#include "core/Creative.h"

using namespace vesper;

VesperEditor::VesperEditor(VesperProcessor& p)
    : AudioProcessorEditor(p), proc(p)
{
    /* The ctor reads as a table of contents; the wiring lives in the
       initX() methods below it (10-AUDIT Workstream 4). */
    // Restore persisted theme (dark default) before the LNF caches colours.
    vesper::theme::setMode((int) proc.apvts.state.getProperty("themeMode", 0) == 1
                               ? vesper::theme::Mode::light : vesper::theme::Mode::dark);
    vesper::theme::setHighContrast((bool) proc.apvts.state.getProperty("highContrast", false));
    vesper::theme::setAccent((int) proc.apvts.state.getProperty("accentIndex", 0));
    lnf.applyColours();
    setLookAndFeel(&lnf);
    addAndMakeVisible(content);

    initPanels();
    initKnobs();
    initMorph();
    content.addAndMakeVisible(meters);
    initOverlays();
    initHeaderCallbacks();

    proc.macros.onAssignmentsChanged = [this]
    {
        for (auto& k : macroKnobs) if (k) k->repaint(); // refresh count badges
    };
    rack.onModuleSelected = [this] (int module) { selectModule(module); };
    selectModule(rack.getSelected());

    initSizing();

}

void VesperEditor::initPanels()
{
    content.addAndMakeVisible(header);
    content.addAndMakeVisible(analyzer);
    content.addAndMakeVisible(rack);

    analyzer.setSampleRate(proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0);

    for (int m = 0; m < numModules; ++m)
    {
        panels[(size_t) m] = makePanelFor(m, proc);
        content.addChildComponent(*panels[(size_t) m]);
    }
    eqPanel = dynamic_cast<EQPanel*>(panels[(size_t) mEQ].get());
    if (eqPanel != nullptr)
    {
        // two-way band selection sync between the panel tabs and the curve nodes
        eqPanel->onBandSelected = [this] (int b) { analyzer.setSelectedBand(b); };
        analyzer.onBandSelected = [this] (int b) { if (eqPanel) eqPanel->selectBandExternal(b); };
    }
}

void VesperEditor::initKnobs()
{
    inKnob  = std::make_unique<Knob>(proc.apvts, ids::inGain,  "IN");
    outKnob = std::make_unique<Knob>(proc.apvts, ids::outGain, "OUT");
    mixKnob = std::make_unique<Knob>(proc.apvts, ids::mix,     "MIX");
    for (auto* k : { inKnob.get(), outKnob.get(), mixKnob.get() })
        k->macroEngine = &proc.macros;
    content.addAndMakeVisible(*inKnob);
    content.addAndMakeVisible(*outKnob);
    content.addAndMakeVisible(*mixKnob);
    for (int i = 0; i < 4; ++i)
    {
        macroKnobs[(size_t) i] = std::make_unique<Knob>(
            proc.apvts, "macro" + juce::String(i + 1), "M" + juce::String(i + 1));
        macroKnobs[(size_t) i]->macroEngine = &proc.macros; // for the count badge
        content.addAndMakeVisible(*macroKnobs[(size_t) i]);
    }
}

void VesperEditor::initMorph()
{
    // Morph: live state (0) -> other A/B slot (1). Endpoints capture lazily so
    // wheel and double-click gestures work without an explicit drag start.
    morphSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    morphSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    morphSlider.setRange(0.0, 1.0, 0.0);
    morphSlider.setDoubleClickReturnValue(true, 0.0);
    morphSlider.setTitle("Morph A to B");
    morphSlider.setTooltip("Morph the live state toward the other A/B slot");
    morphSlider.setWantsKeyboardFocus(true);   // a11y parity with the arcs (D-046)
    morphSlider.textFromValueFunction = [] (double v)
    { return juce::String(juce::roundToInt(v * 100.0)) + " %"; };
    morphSlider.updateText();
    morphSlider.onValueChange = [this]
    {
        if (! morphEngine.active())
            morphEngine.begin(proc.apvts, proc.getOtherState());
        morphEngine.apply((float) morphSlider.getValue());
        // D-056: the value has its own line — the label never wobbles (03)
        morphNumeral = juce::String((int) std::round(morphSlider.getValue() * 100.0));
        repaint(); // value line lives in the editor's own paint
    };
    morphSlider.onDragEnd = [this] { morphEngine.finish(); };
    content.addAndMakeVisible(morphSlider);
    morphLabel.setText("MORPH", juce::dontSendNotification);
    morphLabel.setJustificationType(juce::Justification::centred);
    morphLabel.setFont(theme::labelFont());
    morphLabel.setColour(juce::Label::textColourId, theme::palette().textDim);
    morphLabel.setInterceptsMouseClicks(false, false);
    content.addAndMakeVisible(morphLabel);
}

void VesperEditor::initOverlays()
{
    content.addChildComponent(browser);
    browser.onClose = [this]
    {
        browser.setVisible(false);
        header.refreshPresetName();
        rack.refreshOrder(); // presets can carry their own chain order
        grabKeyboardFocus();
    };
}

void VesperEditor::initHeaderCallbacks()
{
    header.onOpenBrowser = [this] { browser.open(); };
    header.onSavePreset = [this] { browser.openForSaving(); }; // D-055
    header.onStateSwapped = [this] { rack.refreshOrder(); repaint(); };
    header.onChainChanged = [this] { rack.refreshOrder(); repaint(); };
    header.onThemeChanged = [this]
    {
        lnf.applyColours();
        proc.apvts.state.setProperty("themeMode", (int) vesper::theme::mode(), nullptr);
        proc.apvts.state.setProperty("highContrast", vesper::theme::highContrast(), nullptr);
        proc.apvts.state.setProperty("accentIndex", vesper::theme::accentIndex(), nullptr);
        // Labels with an explicit colour do not follow the palette on their
        // own — refresh them or they go invisible in the other mode (D-049).
        morphLabel.setColour(juce::Label::textColourId, theme::palette().textDim);
        applyStandaloneChrome(); // keep the shell in lockstep with the palette
        if (auto* top = getTopLevelComponent()) top->repaint(); else repaint();
    };
    header.onScaleRequest = [this] (float s)
    {
        proc.apvts.state.setProperty("uiScaleUserSet", true, nullptr); // intent
        setUIScale(s);
    };
}

void VesperEditor::initSizing()
{
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio((double) baseWidth / baseHeight);
    setResizeLimits(baseWidth * 3 / 4, baseHeight * 3 / 4,
                    baseWidth * 5 / 2, baseHeight * 5 / 2); // room for 2.5x (D-060)

    /* D-050/D-051/D-060: a fixed default cannot suit both a laptop and a 4K
       panel, so the window sizes itself against the display (~72% of its
       usable height; re-derived in parentHierarchyChanged once the editor
       knows which display it actually landed on). Only a scale the USER
       chose from the menu is treated as a preference — `uiScaleUserSet`
       records that intent; without it, auto-sizing runs every launch
       instead of freezing its own first result. */
    const bool userChose = (bool) proc.apvts.state.getProperty("uiScaleUserSet", false);
    const float initialScale = userChose
        ? (float) (double) proc.apvts.state.getProperty("uiScale", 1.0)
        : autoScaleForDisplay();
    setUIScale(juce::jlimit(0.75f, 2.5f, initialScale), false);
}

VesperEditor::~VesperEditor()
{
    proc.macros.onAssignmentsChanged = nullptr; // outlives us otherwise
    if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr); // lnf dies with us
    setLookAndFeel(nullptr);
}

//==============================================================================
/*  D-033 — the standalone shell must read as Vesper, not stock JUCE. In a DAW
    the host owns the window chrome, so all of this is standalone-only.

    · Default LNF -> ours: the JUCE-drawn title bar, the Options menu and the
      audio-settings dialog all pick up the palette (VesperLNF draws the bar
      and its buttons; the V4 scheme covers the rest).
    · The muted-input notice hardcodes goldenrod + black in JUCE 7.0.12's
      juce_StandaloneFilterWindow.h. We find it by its label text (stable in
      our pinned JUCE) and slip a night backplate behind its children. */

namespace
{
    struct ChromeBackplate : juce::Component
    {
        ChromeBackplate() { setInterceptsMouseClicks(false, false); }
        void paint(juce::Graphics& g) override
        {
            auto& p = vesper::theme::palette();
            g.fillAll(p.night);
            g.setColour(p.ink(0.07f));
            g.fillRect(getLocalBounds().removeFromBottom(1));
        }
        void parentSizeChanged() override
        {
            if (auto* pc = getParentComponent()) setBounds(pc->getLocalBounds());
        }
    };

    juce::Label* findMutedNoticeLabel(juce::Component* c)
    {
        if (auto* l = dynamic_cast<juce::Label*>(c))
            if (l->getText().startsWith("Audio input is muted")) return l;
        for (int i = 0; i < c->getNumChildComponents(); ++i)
            if (auto* found = findMutedNoticeLabel(c->getChildComponent(i)))
                return found;
        return nullptr;
    }
} // namespace

void VesperEditor::parentHierarchyChanged()
{
    applyStandaloneChrome(); // idempotent; fires again once the window exists

    /* D-060: the ctor guessed the scale from the PRIMARY display because no
       peer existed yet — in a host that can be the wrong monitor entirely,
       which is how the window kept opening too small. Once the editor is
       actually on screen, re-derive the size from the display it landed on.
       A scale the user chose from the menu still always wins. */
    if (! (bool) proc.apvts.state.getProperty("uiScaleUserSet", false)
        && getPeer() != nullptr)
    {
        const float want = autoScaleForDisplay();
        if (std::abs(want - uiScale) > 0.01f)
            setUIScale(want, false);
    }
}

void VesperEditor::applyStandaloneChrome()
{
    if (proc.wrapperType != juce::AudioProcessor::wrapperType_Standalone) return;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);

    auto* top = getTopLevelComponent();
    if (top == nullptr || top == this) return;

    if (auto* dw = dynamic_cast<juce::DocumentWindow*>(top))
        dw->setBackgroundColour(vesper::theme::palette().night); // repaints the bar

    /* D-051: the standalone's own "Options" button in the top-left corner is
       now redundant — everything it offered lives in the ··· menu, and it is
       the one piece of chrome that has no equivalent when hosted. Hide it so
       there is exactly one place to look for settings. */
    {
        std::function<void(juce::Component*)> hideRedundant = [&] (juce::Component* c)
        {
            if (auto* b = dynamic_cast<juce::Button*>(c))
            {
                // "Options" (top-left) and the muted-notice "Settings..." both
                // now duplicate the ··· menu — one place to look, not three.
                const auto t = b->getButtonText();
                if (t == "Options" || t == "Settings...") { b->setVisible(false); return; }
            }
            for (int i = 0; i < c->getNumChildComponents(); ++i)
                hideRedundant(c->getChildComponent(i));
        };
        hideRedundant(top);
    }

    if (auto* label = findMutedNoticeLabel(top))
    {
        label->setColour(juce::Label::textColourId, vesper::theme::palette().textDim);
        if (auto* area = label->getParentComponent())
        {
            if (mutedBackplate == nullptr)
            {
                mutedBackplate = std::make_unique<ChromeBackplate>();
                area->addAndMakeVisible(*mutedBackplate);
                mutedBackplate->toBack(); // behind the label + Settings button
            }
            mutedBackplate->setBounds(area->getLocalBounds());
        }
    }
}

//==============================================================================
void VesperEditor::selectModule(int module)
{
    // The bloom (04: context selected) — on the one clock, vesperEase.
    for (int m = 0; m < numModules; ++m)
    {
        auto* panel = panels[(size_t) m].get();
        if (m == module)
        {
            if (! panel->isVisible())
            {
                panel->setVisible(true);
                if (! panelArea.isEmpty())
                    bloomTween.start(*panel, panelArea.translated(0, theme::unit),
                                     panelArea, 0.0f, 1.0f, motion::move);
                else
                    panel->setAlpha(1.0f);
            }
        }
        else
        {
            panel->setVisible(false);
            panel->setAlpha(1.0f);
            if (! panelArea.isEmpty()) panel->setBounds(panelArea);
        }
    }
    header.randomFocus = module;

    // EQ curve is always drawn from the live band values; accent when the
    // EQ module is selected, ghost otherwise (09 §2). When EQ is active the
    // analyzer also becomes an on-curve node editor (M4).
    if (eqPanel != nullptr)
        analyzer.overlayResponse = [this] (double hz) { return eqPanel->responseDbAt(hz); };
    // Match correction curve: shown while the EQ is the active module (D-032).
    analyzer.matchResponse = (module == mEQ)
        ? std::function<float(double)>([this] (double hz) { return proc.matchEQ.displayDbAtHz(hz); })
        : nullptr;
    analyzer.overlayEmphasized = (module == mEQ);
    analyzer.setEQEditable(&proc.apvts, module == mEQ);
    if (module == mEQ && eqPanel != nullptr)
        analyzer.setSelectedBand(eqPanel->selectedBand());
    repaint();
}

void VesperEditor::setUIScale(float scale, bool store)
{
    uiScale = scale;
    if (store)
        proc.apvts.state.setProperty("uiScale", (double) scale, nullptr);
    setSize(juce::roundToInt(baseWidth * scale), juce::roundToInt(baseHeight * scale));
}

//==============================================================================
void VesperEditor::paint(juce::Graphics& g)
{
    // Flat field + UI v2 containment (09/D-022): header rule, then the
    // workspace and performance Surfaces with group separators.
    auto& pal = theme::palette();
    g.fillAll(pal.night);
    // Same fit-and-centre mapping the child content uses (see resized), so
    // the painted bands stay registered with the components on top of them.
    const float s    = contentScale();
    const auto  off  = contentOrigin();
    const float w    = baseWidth * s;
    auto S = [s, off] (juce::Rectangle<int> r)
    {
        return juce::Rectangle<float>(r.getX() * s + off.x, r.getY() * s + off.y,
                                      r.getWidth() * s, r.getHeight() * s);
    };
    g.setColour(pal.ink(0.07f));
    g.fillRect(off.x, theme::headerH * s + off.y, w, 1.0f);

    theme::drawSurface(g, S(wsRect), theme::radiusMd * s);   // ladder (D-055)
    theme::drawSurface(g, S(perfRect), theme::radiusMd * s);
    g.setColour(pal.ink(0.07f));
    for (int x : { sepX1, sepX2 })
        g.fillRect(x * s + off.x, (perfRect.getY() + 12) * s + off.y, 1.0f,
                   (perfRect.getHeight() - 24) * s);

    /* MORPH value line — painted, not a Label, so the numeral and unit can
       speak in Knob's exact two-tone voice (D-056). */
    {
        auto vr = S(morphValueRect);
        auto vf = theme::valueFont(12.0f * s);
        auto uf = theme::microFont(8.5f * s);
        const float vw = vf.getStringWidthFloat(morphNumeral);
        const float uw = uf.getStringWidthFloat("%");
        const float x0 = vr.getCentreX() - (vw + uw + 3.0f * s) * 0.5f;
        g.setColour(pal.text.withAlpha(0.88f));
        g.setFont(vf);
        g.drawText(morphNumeral,
                   juce::Rectangle<float>(x0, vr.getY(), vw + 2.0f, vr.getHeight())
                       .toNearestInt(),
                   juce::Justification::centredLeft);
        g.setFont(uf);
        g.setColour(pal.textFaint);
        g.drawText("%",
                   juce::Rectangle<float>(x0 + vw + 3.0f * s, vr.getY(), uw + 2.0f,
                                          vr.getHeight() - 1.0f)
                       .toNearestInt(),
                   juce::Justification::bottomLeft);
    }
}

void VesperEditor::resized()
{
    /* Fit-and-centre, not width-only: an editor must render correctly at
       whatever size it is handed. Scaling by width alone truncates the
       content whenever the host (or the standalone, once its muted-input
       notice changes the height) gives us an aspect other than the
       reference 1080x680. Letterboxing is the honest fallback. */
    const float scale = contentScale();
    const auto  off   = contentOrigin();
    uiScale = scale;
    content.setTransform(juce::AffineTransform::scale(scale)
                             .translated(off.x, off.y));
    content.setBounds(0, 0, baseWidth, baseHeight);

    // UI v2 bands (09): header / signal / icon strip / workspace / performance
    header.setBounds(juce::Rectangle<int>(0, 0, baseWidth, theme::headerH)
                         .reduced(theme::pad - 10, 0));

    const int rackTop = theme::horizonY - ModuleCard::lineY; // strip top (316)
    analyzer.setBounds(0, theme::headerH, baseWidth, rackTop - theme::headerH);
    rack.setBounds(0, rackTop, baseWidth, theme::stationBandH + 8); // +8: notch overhang

    const int panelTop = rackTop + theme::stationBandH;
    const int utilTop  = baseHeight - theme::utilityH;
    wsRect = juce::Rectangle<int>(theme::containInset, panelTop + 4,
                                  baseWidth - 2 * theme::containInset,
                                  utilTop - panelTop - 10);
    panelArea = wsRect.reduced(6, 4);
    for (auto& panel : panels)
        if (panel->isVisible()) panel->setBounds(panelArea);

    // Performance: one contained band — IN OUT MIX | macros+morph | telemetry
    perfRect = juce::Rectangle<int>(theme::containInset, utilTop + 2,
                                    baseWidth - 2 * theme::containInset,
                                    theme::utilityH - 10);
    auto util = perfRect.reduced(12, 4);
    auto io = util.removeFromLeft(186);
    sepX1 = io.getRight() + 6;
    const int kw = io.getWidth() / 3;
    inKnob->setBounds(io.removeFromLeft(kw));
    outKnob->setBounds(io.removeFromLeft(kw));
    mixKnob->setBounds(io);

    auto meterArea = util.removeFromRight(216);
    sepX2 = meterArea.getX() - 6;
    // D-051: was inset by 9 top and bottom, which spent 18 px of a ~40 px
    // band on nothing and left the three telemetry rows cramped.
    meters.setBounds(meterArea.reduced(0, 3));

    const int cellW = juce::jmin(62, util.getWidth() / 5);
    auto mid = juce::Rectangle<int>(util.getCentreX() - cellW * 5 / 2, util.getY(),
                                    cellW * 5, util.getHeight());
    for (int i = 0; i < 4; ++i)
        macroKnobs[(size_t) i]->setBounds(mid.removeFromLeft(cellW));
    // D-056: mirror the compact Knob — label above, value below, no swap
    morphLabel.setBounds(mid.removeFromTop(11));
    morphValueRect = mid.removeFromBottom(14);
    morphSlider.setBounds(mid.reduced(2));

    browser.setBounds(0, 0, baseWidth, baseHeight);
}

//==============================================================================
bool VesperEditor::keyPressed(const juce::KeyPress& k)
{
    const bool ctrl = k.getModifiers().isCommandDown();

    if (ctrl && k.getKeyCode() == 'Z' && k.getModifiers().isShiftDown())
        { proc.undoManager.redo(); return true; }
    if (ctrl && k.getKeyCode() == 'Z') { proc.undoManager.undo(); return true; }
    if (ctrl && k.getKeyCode() == 'Y') { proc.undoManager.redo(); return true; }

    if (k.getKeyCode() == 'P') { browser.open(); return true; }
    if (k.getKeyCode() == 'X')
    {
        proc.toggleAB();
        rack.refreshOrder();
        repaint();
        return true;
    }
    if (k.getKeyCode() == 'B')
    {
        if (auto* bp = proc.apvts.getParameter(ids::bypass))
        {
            bp->beginChangeGesture();
            bp->setValueNotifyingHost(bp->getValue() > 0.5f ? 0.0f : 1.0f);
            bp->endChangeGesture();
        }
        return true;
    }
    if (k.getKeyCode() == 'R')
    {
        Randomizer::randomize(proc.apvts, rack.getSelected(), 0.5f);
        return true;
    }
    if (k.getTextCharacter() == '[') { proc.presets.loadNext(false); return true; }
    if (k.getTextCharacter() == ']') { proc.presets.loadNext(true); return true; }

    /* D-060: digits address what the eye sees — the Nth tile in the rack —
       not internal module ids (they diverged when Gate/MBComp/Pitch were
       appended with stable ids; pressing 1 used to light the SECOND tile). */
    const auto ch = k.getTextCharacter();
    if (ch >= '1' && ch <= '9')
    { rack.select(proc.getChainOrder()[(size_t) ((int) ch - '1')]); return true; }
    if (ch == '0') { rack.select(proc.getChainOrder()[9]); return true; }

    return false;
}
