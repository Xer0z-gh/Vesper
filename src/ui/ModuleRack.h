#pragma once
/*  ModuleRack — the module strip of UI v2 (docs/design/09, D-022):
    13 icon tiles, each a primitive-drawn glyph over a caps label, riding
    the meridian — the full-width accent rule at the strip's bottom edge,
    with an underline notch beneath the selected tile.
    Click = select · click the top-right corner point = enable/bypass ·
    drag sideways = reorder (lock-free, stateful — audio never clicks). */

#include "../VesperProcessor.h"
#include "Theme.h"

namespace vesper {

// ------------------------------------------------------- module glyphs ----
/* All 13 icons drawn from Rule/Point/Arc primitives on a 22x22 box,
   1.5 px stroke, round caps. Never icon fonts (D-011). */
inline void paintModuleGlyph(juce::Graphics& g, int module,
                             juce::Rectangle<float> box, juce::Colour col)
{
    const float s = box.getWidth() / 22.0f;
    auto X = [&] (float v) { return box.getX() + v * s; };
    auto Y = [&] (float v) { return box.getY() + v * s; };
    juce::Path p;
    const juce::PathStrokeType stroke(1.5f * s, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded);
    g.setColour(col);

    switch (module)
    {
        case mGate: // three bars, centre tall
            p.startNewSubPath(X(5), Y(7));  p.lineTo(X(5), Y(15));
            p.startNewSubPath(X(11), Y(3)); p.lineTo(X(11), Y(19));
            p.startNewSubPath(X(17), Y(7)); p.lineTo(X(17), Y(15));
            break;
        case mEQ: // bell
            p.startNewSubPath(X(1), Y(17));
            p.cubicTo(X(7), Y(17), X(7), Y(4), X(11), Y(4));
            p.cubicTo(X(15), Y(4), X(15), Y(17), X(21), Y(17));
            break;
        case mFilter: // lowpass slope with knee
            p.startNewSubPath(X(1), Y(8));
            p.lineTo(X(11), Y(8));
            p.cubicTo(X(15), Y(8), X(16), Y(19), X(20), Y(19));
            break;
        case mComp: // waves pinched by converging rules
            p.startNewSubPath(X(2), Y(4));  p.lineTo(X(20), Y(8));
            p.startNewSubPath(X(2), Y(18)); p.lineTo(X(20), Y(14));
            p.startNewSubPath(X(5), Y(11));
            p.cubicTo(X(8), Y(8), X(11), Y(14), X(14), Y(11));
            break;
        case mMBComp: // five bars
            for (int i = 0; i < 5; ++i)
            {
                const float h[] = { 6, 10, 14, 10, 6 };
                p.startNewSubPath(X(3 + i * 4.0f), Y(19));
                p.lineTo(X(3 + i * 4.0f), Y(19 - h[i]));
            }
            break;
        case mSat: // sigmoid transfer curve
            p.startNewSubPath(X(2), Y(19));
            p.cubicTo(X(12), Y(19), X(10), Y(3), X(20), Y(3));
            break;
        case mTransient: // spike then decay
            p.startNewSubPath(X(1), Y(16));
            p.lineTo(X(6), Y(16));
            p.lineTo(X(8), Y(3));
            p.cubicTo(X(10), Y(14), X(15), Y(16), X(21), Y(16));
            break;
        case mModFX: // two interlocked circles + centre point
            p.addEllipse(X(2), Y(5), 12 * s, 12 * s);
            p.addEllipse(X(8), Y(5), 12 * s, 12 * s);
            break;
        case mPitch: // sine period
            p.startNewSubPath(X(1), Y(11));
            p.cubicTo(X(4), Y(3), X(7), Y(3), X(11), Y(11));
            p.cubicTo(X(15), Y(19), X(18), Y(19), X(21), Y(11));
            break;
        case mDelay: // fading echo bars
            p.startNewSubPath(X(4), Y(4));  p.lineTo(X(4), Y(18));
            p.startNewSubPath(X(11), Y(6)); p.lineTo(X(11), Y(16));
            p.startNewSubPath(X(18), Y(8)); p.lineTo(X(18), Y(14));
            break;
        case mReverb: // concentric rings + centre point
            p.addEllipse(X(7), Y(7), 8 * s, 8 * s);
            p.addEllipse(X(2.5f), Y(2.5f), 17 * s, 17 * s);
            g.fillEllipse(X(10), Y(10), 2 * s, 2 * s);
            break;
        case mStereo: // venn with an M/S axis
            p.addEllipse(X(2), Y(5.5f), 11 * s, 11 * s);
            p.addEllipse(X(9), Y(5.5f), 11 * s, 11 * s);
            p.startNewSubPath(X(11), Y(2)); p.lineTo(X(11), Y(20));
            break;
        case mLimiter: // waveform clipped by the ceiling
            p.startNewSubPath(X(2), Y(5)); p.lineTo(X(20), Y(5));
            p.startNewSubPath(X(6), Y(19));  p.lineTo(X(6), Y(9));
            p.startNewSubPath(X(11), Y(19)); p.lineTo(X(11), Y(5.8f));
            p.startNewSubPath(X(16), Y(19)); p.lineTo(X(16), Y(9));
            break;
        default: break;
    }
    g.strokePath(p, stroke);
}

// ---------------------------------------------------------------- tile ----
class ModuleCard : public juce::Component
{
public:
    static constexpr int lineY = 77; // meridian = strip bottom (band 316..394)

    ModuleCard(VesperProcessor& procIn, int moduleIdx)
        : proc(procIn), module(moduleIdx)
    {
        onAttachment = std::make_unique<juce::ParameterAttachment>(
            *proc.apvts.getParameter(moduleOnID(module)),
            [this] (float) { repaint(); });
        setTitle(moduleName(module));
        setWantsKeyboardFocus(true);
    }

    std::function<void(int)> onSelect;

    void setSelected(bool s) { selected = s; repaint(); }

    void paint(juce::Graphics& g) override
    {
        auto& p = theme::palette();
        const bool on = isOn();
        const bool hover = isMouseOver();
        auto tile = getLocalBounds().toFloat().withTrimmedBottom((float) getHeight() - lineY)
                        .reduced(1.0f, 6.0f);

        if (selected)
        {
            /* D-052: the selected tile is a contained, accent-bordered box —
               the mockup's treatment. A tinted fill alone read as "slightly
               lighter"; the border states selection outright. */
            g.setColour(p.accent.withAlpha(0.10f));
            g.fillRoundedRectangle(tile, theme::radiusMd);
            g.setColour(p.accent.withAlpha(0.70f));
            g.drawRoundedRectangle(tile.reduced(0.5f), theme::radiusMd, 1.2f);
        }
        else if (hover)
        {
            g.setColour(p.ink(0.030f));
            g.fillRoundedRectangle(tile, theme::radiusMd);
        }

        // Glyph + label: rest ink-62/38, hover +1 step, selected accent
        const auto glyphCol = selected ? p.accent
                            : hover    ? p.text.withAlpha(0.85f)
                                       : p.textDim.withAlpha(0.85f);
        const auto labelCol = selected ? p.accent.withAlpha(0.95f)
                            : hover    ? p.textDim
                                       : p.textFaint;
        auto inner = tile.reduced(0.0f, 5.0f);
        paintModuleGlyph(g, module,
                         juce::Rectangle<float>(22.0f, 22.0f)
                             .withCentre({ inner.getCentreX(),
                                           inner.getY() + 14.0f }),
                         glyphCol);
        g.setFont(theme::labelFont(9.5f));
        g.setColour(labelCol);
        g.drawText(juce::String(moduleName(module)).toUpperCase(),
                   inner.toNearestInt().withTrimmedTop(30),
                   juce::Justification::centredTop);

        // Enable point, top-right corner: accent = processing, ring = bypassed
        const juce::Point<float> c { tile.getRight() - 8.0f, tile.getY() + 8.0f };
        if (on)
            theme::drawGlowDot(g, c, 2.5f, p.accent, 0.0f);
        else
        {
            g.setColour(hover ? p.textDim : p.ink(0.16f));
            g.drawEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre(c), 1.2f);
        }

        // Selected: accent underline segment + notch on the meridian
        if (selected)
        {
            const float y = (float) lineY;
            const float w = tile.getWidth() * 0.55f;
            const float cx = tile.getCentreX();
            g.setColour(p.accent);
            g.fillRect(cx - w * 0.5f, y - 1.0f, w, 2.0f);
            juce::Path notch;
            notch.addTriangle(cx - 4.0f, y + 1.0f, cx + 4.0f, y + 1.0f, cx, y + 6.0f);
            g.setColour(p.accent.withAlpha(0.9f));
            g.fillPath(notch);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Tiles are stationary (D-024). Corner point toggles; else selects.
        if (e.getPosition().getDistanceFrom({ getWidth() - 9, 14 }) < 12)
        {
            if (auto* param = proc.apvts.getParameter(moduleOnID(module)))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost(isOn() ? 0.0f : 1.0f);
                param->endChangeGesture();
            }
            if (onSelect) onSelect(module); // hearing what you just enabled
            return;
        }
        if (onSelect) onSelect(module);
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

    int getModule() const { return module; }

private:
    bool isOn() const
    {
        return proc.apvts.getRawParameterValue(moduleOnID(module))->load() > 0.5f;
    }

    VesperProcessor& proc;
    int module;
    bool selected = false;
    std::unique_ptr<juce::ParameterAttachment> onAttachment;
};

// -----------------------------------------------------------------------------
class ModuleRack : public juce::Component
{
public:
    explicit ModuleRack(VesperProcessor& procIn) : proc(procIn)
    {
        for (int m = 0; m < numModules; ++m)
        {
            auto card = std::make_unique<ModuleCard>(proc, m);
            card->onSelect = [this] (int module) { select(module); };
            addAndMakeVisible(*card);
            cards.push_back(std::move(card));
        }
        select(mSat);
    }

    std::function<void(int)> onModuleSelected;

    void select(int module)
    {
        selected = module;
        for (auto& c : cards) c->setSelected(c->getModule() == module);
        if (onModuleSelected) onModuleSelected(module);
    }

    int getSelected() const { return selected; }

    void refreshOrder() { resized(); repaint(); }

    void paint(juce::Graphics& g) override
    {
        // Hairline separators between tiles; the meridian rule at the
        // strip's bottom edge — the strip IS the meridian, worn as a toolbar.
        auto& p = theme::palette();
        const float slotW = getWidth() / (float) numModules;
        g.setColour(p.ink(0.07f));
        for (int i = 1; i < numModules; ++i)
            g.fillRect(slotW * i, 12.0f, 1.0f, (float) ModuleCard::lineY - 24.0f);
        theme::drawHorizon(g, (float) ModuleCard::lineY, 0.0f, (float) getWidth());
    }

    void resized() override
    {
        // Tiles sit in the chain order; reordering will move to a settings
        // menu (D-024) — the lock-free setChainOrder path stays ready for it.
        const auto order = proc.getChainOrder();
        const int slotW = getWidth() / numModules;
        for (int i = 0; i < numModules; ++i)
            for (auto& c : cards)
                if (c->getModule() == order[(size_t) i])
                    c->setBounds(i * slotW, 0, slotW, getHeight());
    }

private:
    VesperProcessor& proc;
    std::vector<std::unique_ptr<ModuleCard>> cards;
    int selected = mSat;
};

} // namespace vesper
