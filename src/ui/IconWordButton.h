#pragma once
/*  IconWordButton — a quiet word with a small drawn glyph before it (D-057).

    The header's Dice button faked this with leading spaces in the button
    text while a paint-over routine drew the die at a hand-tuned offset —
    two pieces of code that had to agree about geometry and never quite
    did. Here the glyph and the word are measured and centred as ONE unit,
    and the glyph paints in the same ink the word wears, so they can never
    disagree on position, hover state, or colour.

    Glyphs are Paths and primitives only — icon fonts stay banned (D-011). */

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace vesper {

class IconWordButton : public juce::Button
{
public:
    /* Draws the glyph inside `area` using `ink` — the word's current
       colour, so the pair always reads as one control. */
    using GlyphFn = std::function<void (juce::Graphics&,
                                        juce::Rectangle<float> area,
                                        juce::Colour ink)>;

    IconWordButton(const juce::String& word, GlyphFn glyphIn)
        : juce::Button(word), glyph(std::move(glyphIn)) {}

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        auto& p = theme::palette();

        // hover wash — identical to the LNF's momentary-button treatment
        if (over || down)
        {
            g.setColour(p.ink(down ? 0.09f : 0.045f));
            g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f),
                                   theme::radiusSm);
        }

        const auto ink = ! isEnabled() ? p.textFaint.withAlpha(0.5f)
                         : over        ? p.text
                                       : p.textDim;

        const auto  f     = theme::font(12.5f);
        const float ww    = f.getStringWidthFloat(getButtonText());
        const float total = glyphSide + wordGap + ww;
        const float x0    = ((float) getWidth() - total) * 0.5f;

        if (glyph)
            glyph(g, { x0, ((float) getHeight() - glyphSide) * 0.5f,
                       glyphSide, glyphSide }, ink);

        g.setColour(ink);
        g.setFont(f);
        g.drawText(getButtonText(),
                   juce::Rectangle<float>(x0 + glyphSide + wordGap, 0.0f,
                                          ww + 2.0f, (float) getHeight())
                       .toNearestInt(),
                   juce::Justification::centredLeft);
    }

private:
    static constexpr float glyphSide = 11.0f, wordGap = 6.0f;
    GlyphFn glyph;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IconWordButton)
};

} // namespace vesper
