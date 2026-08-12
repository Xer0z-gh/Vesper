#pragma once
/*  HeaderBar (UI v2 M2, docs/design/09 §1) — mark + wordmark · a centered,
    contained preset navigator (‹ name › over a category subline) with a
    save field · quiet word actions · a drawn power pill for bypass. */

#include "../VesperProcessor.h"
#include "../core/Creative.h"
#include "IconWordButton.h"
#include "Motion.h"
#include "Theme.h"

namespace vesper {

// ------------------------------------------------------------ PowerButton --
/* A power symbol in an outlined pill: accent when the plugin is active,
   dim when bypassed (09 §1 — replaces the redundant "BYP" word + icon). */
class PowerButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PowerButton(juce::RangedAudioParameter& param)
    {
        att = std::make_unique<juce::ParameterAttachment>(
            param, [this] (float v) { bypassed = v > 0.5f; repaint(); });
        att->sendInitialUpdate();
        setTooltip("Bypass  (B)");
    }

    void paint(juce::Graphics& g) override
    {
        auto& p = theme::palette();
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        const bool hot = isMouseOver();
        const auto col = bypassed ? p.ink(0.38f) : p.accent;

        g.setColour(p.ink(hot ? 0.06f : 0.03f));
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(bypassed ? p.ink(0.18f) : p.accent.withAlpha(0.5f));
        g.drawRoundedRectangle(r.reduced(0.5f), r.getHeight() * 0.5f, 1.0f);

        const float cx = r.getCentreX(), cy = r.getCentreY() + 0.5f, rad = 5.5f;
        juce::Path glyph;
        glyph.addCentredArc(cx, cy, rad, rad, 0.0f, 0.6f,
                            juce::MathConstants<float>::twoPi - 0.6f, true);
        g.setColour(col);
        g.strokePath(glyph, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        g.fillRect(cx - 0.8f, cy - rad - 2.0f, 1.6f, rad + 1.0f); // stem
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent&) override
    {
        att->setValueAsCompleteGesture(bypassed ? 0.0f : 1.0f);
    }

private:
    std::unique_ptr<juce::ParameterAttachment> att;
    bool bypassed = false;
};

// -------------------------------------------------------------- HeaderBar --
class HeaderBar : public juce::Component, private juce::Timer
{
public:
    explicit HeaderBar(VesperProcessor& procIn) : proc(procIn)
    {
        auto initTextButton = [this] (juce::TextButton& b, const juce::String& text,
                                      const juce::String& tip)
        {
            b.setButtonText(text);
            b.setTooltip(tip);
            addAndMakeVisible(b);
        };

        initTextButton(prevBtn, juce::CharPointer_UTF8("\xe2\x80\xb9"), "Previous preset");
        initTextButton(nextBtn, juce::CharPointer_UTF8("\xe2\x80\xba"), "Next preset");
        initTextButton(presetBtn, {}, "Open preset browser  (P)"); // transparent hit area
        initTextButton(saveBtn, "+", "Save as user preset");
        initTextButton(undoBtn, "Undo", "Undo  (Ctrl+Z)");
        initTextButton(redoBtn, "Redo", "Redo  (Ctrl+Shift+Z)");
        initTextButton(abBtn, "A", "Toggle A/B state  (X)");
        initTextButton(copyBtn, "Copy", "Copy active to other slot");
        initTextButton(movesBtn, "Moves", "One-gesture transformations");
        diceBtn.setTooltip("Musical randomize  (R)");
        addAndMakeVisible(diceBtn);
        /* D-051: the three dots are DRAWN (Points — a language primitive),
           not typed. As button text they rendered as a faint dash at small
           sizes; drawn they stay crisp at any UI scale. */
        initTextButton(gearBtn, {}, "Settings");

        prevBtn.onClick = [this] { proc.presets.loadNext(false); refreshPresetName(); };
        nextBtn.onClick = [this] { proc.presets.loadNext(true); refreshPresetName(); };
        presetBtn.onClick = [this] { if (onOpenBrowser) onOpenBrowser(); };
        saveBtn.onClick = [this] { if (onSavePreset) onSavePreset();
                                   else if (onOpenBrowser) onOpenBrowser(); };
        undoBtn.onClick = [this] { proc.undoManager.undo(); };
        redoBtn.onClick = [this] { proc.undoManager.redo(); };
        abBtn.onClick = [this]
        {
            proc.toggleAB();
            abBtn.setButtonText(proc.isStateB() ? "B" : "A");
            if (onStateSwapped) onStateSwapped();
        };
        copyBtn.onClick = [this] { proc.copyActiveToOther(); };
        movesBtn.onClick = [this] { showMovesMenu(); };
        diceBtn.onClick = [this]
        {
            Randomizer::randomize(proc.apvts, randomFocus, 0.5f);
            refreshPresetName();
        };
        gearBtn.onClick = [this] { showSettingsMenu(); };

        osBox.addItemList(oversampleNames(), 1);
        osBox.setTooltip("Oversampling (saturation stage)");
        osAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            proc.apvts, ids::oversamp, osBox);
        addAndMakeVisible(osBox);

        power = std::make_unique<PowerButton>(*proc.apvts.getParameter(ids::bypass));
        addAndMakeVisible(*power);

        startTimerHz(4);
    }

    std::function<void()> onOpenBrowser;
    std::function<void()> onSavePreset;  // "+": browser opened onto the save field (D-055)
    std::function<void()> onStateSwapped;
    std::function<void()> onChainChanged;
    std::function<void()> onThemeChanged;
    std::function<void(float)> onScaleRequest;
    int randomFocus = -1;

    void refreshPresetName() { repaint(); }

    void paint(juce::Graphics& g) override
    {
        auto& p = theme::palette();
        const float cy = getHeight() * 0.5f;

        // Mark + wordmark
        const float sx = (float) theme::pad + 2.0f;
        g.setColour(p.accent.withAlpha(0.10f));
        g.fillEllipse(juce::Rectangle<float>(26.0f, 26.0f).withCentre({ sx, cy }));
        g.setColour(p.accent.withAlpha(0.92f));
        g.fillPath(theme::starPath(sx, cy, 6.5f));
        g.setFont(theme::font(14.5f, true).withExtraKerningFactor(0.24f));
        g.setColour(p.text);
        g.drawText("VESPER", (int) sx + 16, 0, 130, getHeight(),
                   juce::Justification::centredLeft);

        // Preset navigator: a contained field with name over a category subline
        auto f = presetField.toFloat();
        theme::drawSurface(g, f, theme::radiusSm, presetBtn.isMouseOver());
        auto nameArea = f.reduced(30.0f, 0.0f); // clear of the ‹ › buttons
        const auto name = proc.presets.getCurrentName();
        g.setColour(p.text);
        g.setFont(theme::font(14.0f));
        g.drawText(name, nameArea.withTrimmedBottom(nameArea.getHeight() * 0.42f).toNearestInt(),
                   juce::Justification::centred);
        g.setColour(p.textFaint);
        g.setFont(theme::microFont(8.5f));
        g.drawText(subtitle(name),
                   nameArea.withTrimmedTop(nameArea.getHeight() * 0.55f).toNearestInt(),
                   juce::Justification::centred);

        // Save field: a small square container for the +
        theme::drawSurface(g, saveField.toFloat(), theme::radiusSm, saveBtn.isMouseOver());

        // Group separators (D-052)
        g.setColour(p.ink(0.10f));
        for (const auto& s : separators) g.fillRect(s);

        // Settings affordance: three Points, drawn (D-051)
        {
            const auto gb = gearBtn.getBounds().toFloat();
            const float cy2 = gb.getCentreY();
            const float step = 4.5f;
            g.setColour(gearBtn.isMouseOver() ? p.text : p.textDim);
            for (int i = -1; i <= 1; ++i)
                g.fillEllipse(juce::Rectangle<float>(2.6f, 2.6f)
                                  .withCentre({ gb.getCentreX() + i * step, cy2 }));
        }
    }

    void resized() override
    {
        auto full = getLocalBounds().reduced(theme::pad, 11);
        auto r = full;

        // Right cluster (from the edge): power · ··· · OS · dice · moves ·
        // copy · A/B · redo · undo
        /* D-052: hairline separators between the action groups, as the
           mockup does — the cluster reads as three related groups rather
           than one undifferentiated row of words. */
        separators.clear();
        auto groupGap = [&] (juce::Rectangle<int>& rr)
        {
            rr.removeFromRight(6);
            separators.add(rr.removeFromRight(1).reduced(0, 4));
            rr.removeFromRight(6);
        };

        power->setBounds(r.removeFromRight(46).reduced(0, 3));
        r.removeFromRight(8);
        gearBtn.setBounds(r.removeFromRight(30));
        groupGap(r);
        osBox.setBounds(r.removeFromRight(60).reduced(0, 2));
        groupGap(r);
        diceBtn.setBounds(r.removeFromRight(58));
        movesBtn.setBounds(r.removeFromRight(54));
        groupGap(r);
        copyBtn.setBounds(r.removeFromRight(44));
        abBtn.setBounds(r.removeFromRight(42));
        groupGap(r);
        redoBtn.setBounds(r.removeFromRight(42));
        undoBtn.setBounds(r.removeFromRight(42));

        // Preset navigator group centered in the space BETWEEN the wordmark
        // and the action cluster (not the whole width — avoids collisions).
        const int leftLimit = full.getX() + 190;
        const int rightLimit = undoBtn.getX() - 12;
        const int fieldW = juce::jlimit(220, 300, rightLimit - leftLimit - 48);
        const int saveW = 32, groupW = fieldW + 8 + saveW;
        const int gx = leftLimit + juce::jmax(0, (rightLimit - leftLimit - groupW) / 2);

        presetField = { gx, full.getY(), fieldW, full.getHeight() };
        saveField   = { gx + fieldW + 8, full.getY(), saveW, full.getHeight() };

        prevBtn.setBounds(presetField.getX() + 2, presetField.getY(), 26, presetField.getHeight());
        nextBtn.setBounds(presetField.getRight() - 28, presetField.getY(), 26, presetField.getHeight());
        presetBtn.setBounds(presetField.reduced(28, 0)); // transparent hit area over the name
        saveBtn.setBounds(saveField);
    }

private:
    juce::String subtitle(const juce::String& name) const
    {
        if (name.isEmpty() || name == "Init") return "NO PRESET LOADED";
        const juce::String sep (juce::CharPointer_UTF8 ("  \xc2\xb7  ")); // middle dot
        for (const auto& info : proc.presets.getAll())
            if (info.name == name)
                return (info.factory ? "FACTORY" : "USER")
                     + (info.category.isEmpty() ? juce::String()
                                                : sep + info.category.toUpperCase());
        return "USER PRESET";
    }

    void timerCallback() override
    {
        undoBtn.setEnabled(proc.undoManager.canUndo());
        redoBtn.setEnabled(proc.undoManager.canRedo());
        abBtn.setButtonText(proc.isStateB() ? "B" : "A");
        repaint();
    }

    void showMovesMenu()
    {
        juce::PopupMenu m;
        int id = 1;
        for (const auto& move : macroMoves())
            m.addItem(id++, move.name);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&movesBtn),
                        [this] (int result)
                        {
                            if (result > 0)
                                applyMacroMove(proc.apvts, macroMoves()[(size_t) result - 1]);
                        });
    }

    void showSettingsMenu()
    {
        juce::PopupMenu scale;
        for (auto s : { 75, 100, 125, 150, 175, 200 })
            scale.addItem(1000 + s, juce::String(s) + "%");

        const bool isLight = theme::mode() == theme::Mode::light;
        juce::PopupMenu m;
        m.addSectionHeader("Interface");
        m.addItem(4, "Dark", true, ! isLight);
        m.addItem(5, "Light", true, isLight);
        juce::PopupMenu accents;   // D-052: the accent is the user's choice
        for (int i = 0; i < (int) theme::accentOptions().size(); ++i)
            accents.addItem(2000 + i, theme::accentOptions()[(size_t) i].name,
                            true, i == theme::accentIndex());
        m.addSubMenu("Accent", accents);
        m.addSubMenu("UI scale", scale);
        m.addItem(2, "High contrast", true, theme::highContrast());
        m.addItem(3, "Reduced motion", true, motion::reducedMotion());
        m.addSeparator();
        m.addSectionHeader("Signal chain");
        m.addSubMenu("Chain order", buildChainOrderMenu());

        /* Everything the standalone window's own "Options" button offers is
           mirrored here (D-050) — that button does not exist when the plugin
           is hosted, so in Ableton these were unreachable. "Reset to Init"
           is useful in any host; the device dialog only makes sense in the
           standalone, where we own the device. */
        m.addSeparator();
        m.addSectionHeader("Session");
        m.addItem(7, "Reset to Init");
        if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
            m.addItem(6, "Audio / MIDI settings...");

        m.addSeparator();
        m.addSectionHeader("Vesper " + juce::String(JucePlugin_VersionString));
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&gearBtn),
                        [this] (int result)
                        {
                            if (result >= 2000)
                            { theme::setAccent(result - 2000);
                              if (onThemeChanged) onThemeChanged(); }
                            else if (result > 1000 && onScaleRequest)
                                onScaleRequest((result - 1000) / 100.0f);
                            else if (result == 2)
                            { theme::setHighContrast(! theme::highContrast());
                              if (onThemeChanged) onThemeChanged(); }
                            else if (result == 3)
                                motion::reducedMotion() = ! motion::reducedMotion();
                            else if (result == 4)
                            { theme::setMode(theme::Mode::dark);
                              if (onThemeChanged) onThemeChanged(); }
                            else if (result == 5)
                            { theme::setMode(theme::Mode::light);
                              if (onThemeChanged) onThemeChanged(); }
                            else if (result == 6)
                                showAudioSettings();
                            else if (result == 7)
                            {
                                proc.presets.loadInit();
                                refreshPresetName();
                                if (onChainChanged) onChainChanged();
                            }
                        });
    }

    /* Open the standalone's audio device dialog from our own menu (D-050).
       Reached through the JUCE Options button's own handler so we do not
       duplicate its dialog setup; standalone-only by construction. */
    void showAudioSettings()
    {
        auto* top = getTopLevelComponent();
        if (top == nullptr) return;
        // The Options button is a child of the standalone window; trigger it.
        std::function<juce::Button*(juce::Component*)> findOptions =
            [&] (juce::Component* c) -> juce::Button*
            {
                if (auto* b = dynamic_cast<juce::Button*>(c))
                    if (b->getButtonText() == "Options") return b;
                for (int i = 0; i < c->getNumChildComponents(); ++i)
                    if (auto* f = findOptions(c->getChildComponent(i))) return f;
                return nullptr;
            };
        if (auto* opts = findOptions(top))
            opts->triggerClick();
    }

    // Deliberate chain reordering (D-024): tiles are stationary; the order
    // is edited here, applied through the lock-free setChainOrder path.
    juce::PopupMenu buildChainOrderMenu()
    {
        const auto order = proc.getChainOrder();
        juce::PopupMenu m;
        for (int i = 0; i < numModules; ++i)
        {
            const int mod = order[(size_t) i];
            juce::PopupMenu sub;
            sub.addItem("Move earlier", i > 0, false, [this, mod] { moveModule(mod, -1); });
            sub.addItem("Move later",   i < numModules - 1, false, [this, mod] { moveModule(mod, +1); });
            sub.addItem("Move to start", i > 0, false, [this, mod] { moveModuleTo(mod, 0); });
            sub.addItem("Move to end",   i < numModules - 1, false,
                        [this, mod] { moveModuleTo(mod, numModules - 1); });
            m.addSubMenu(juce::String(i + 1).paddedLeft('0', 2) + "   "
                             + juce::String(moduleName(mod)).toUpperCase(), sub);
        }
        m.addSeparator();
        m.addItem("Reset to default order", [this]
                  { proc.setChainOrder(unpackOrder(defaultOrder()));
                    if (onChainChanged) onChainChanged(); });
        return m;
    }
    void moveModuleTo(int mod, int target)
    {
        auto order = proc.getChainOrder();
        int from = 0;
        for (int i = 0; i < numModules; ++i) if (order[(size_t) i] == mod) { from = i; break; }
        target = juce::jlimit(0, numModules - 1, target);
        const int v = order[(size_t) from];
        if (from < target) for (int i = from; i < target; ++i) order[(size_t) i] = order[(size_t) i + 1];
        else               for (int i = from; i > target; --i) order[(size_t) i] = order[(size_t) i - 1];
        order[(size_t) target] = v;
        proc.setChainOrder(order);
        if (onChainChanged) onChainChanged();
    }
    void moveModule(int mod, int delta)
    {
        auto order = proc.getChainOrder();
        int from = 0;
        for (int i = 0; i < numModules; ++i) if (order[(size_t) i] == mod) { from = i; break; }
        moveModuleTo(mod, from + delta);
    }

    VesperProcessor& proc;
    juce::TextButton prevBtn, nextBtn, presetBtn, saveBtn, undoBtn, redoBtn,
                     abBtn, copyBtn, movesBtn, gearBtn;
    /* Dice: die face + word as one measured unit (D-057). Paths only —
       icon fonts stay banned (D-011). */
    IconWordButton diceBtn { "Dice",
        [] (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour ink)
        {
            auto die = a.reduced(0.5f);
            g.setColour(ink);
            g.drawRoundedRectangle(die, 2.5f, 1.1f);
            for (auto pip : { juce::Point<float>(-2.4f, -2.4f),
                              juce::Point<float>( 2.4f,  2.4f),
                              juce::Point<float>( 0.0f,  0.0f) })
                g.fillEllipse(juce::Rectangle<float>(1.7f, 1.7f)
                                  .withCentre(die.getCentre() + pip));
        } };
    juce::Array<juce::Rectangle<int>> separators;   // D-052 group rules
    juce::ComboBox osBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> osAtt;
    std::unique_ptr<PowerButton> power;
    juce::Rectangle<int> presetField, saveField;
};

} // namespace vesper
