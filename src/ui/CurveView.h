#pragma once
/*  CurveView — a module's transfer curve, drawn inside its card (D-066,
    Workstream 5 / Subculture card language part 2).

    This is information, not ornament: a saturator's character is its
    shape, and reading it beats reading the word "Tube". The unity
    diagonal is the reference — wherever the curve leaves that line is
    exactly where the module is changing the signal, and by how much.

    Deliberately generic: it plots any f(x) over [-1, 1] -> [-1, 1], so
    the dynamics modules (compressor knee, gate, limiter ceiling) can
    adopt it without a second component. */

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace vesper {

class CurveView : public juce::Component
{
public:
    CurveView() { setInterceptsMouseClicks(false, false); }

    /* Input -> output over [-1, 1]. Must be cheap and pure: it is called
       ~120x per repaint on the message thread. */
    std::function<float(float)> shape;

    /* Optional caption drawn in the tile's top-left, micro-caps. */
    juce::String caption;

    void paint(juce::Graphics& g) override
    {
        auto& p = theme::palette();
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        theme::drawSurface(g, r, theme::radiusMd);

        auto plot = r.reduced(10.0f);
        if (! caption.isEmpty())
        {
            g.setColour(p.textFaint);
            g.setFont(theme::microFont(9.0f));
            g.drawText(caption, r.reduced(8.0f, 6.0f).removeFromTop(11.0f).toNearestInt(),
                       juce::Justification::centredLeft);
            plot.removeFromTop(9.0f);
        }

        // Square the plot so a 45-degree unity line really is 45 degrees —
        // otherwise "no change" reads as a slope that is not 1.
        const float side = juce::jmin(plot.getWidth(), plot.getHeight());
        plot = plot.withSizeKeepingCentre(side, side);

        auto toXY = [&plot] (float x, float y)
        {
            return juce::Point<float>(plot.getCentreX() + x * plot.getWidth() * 0.5f,
                                      plot.getCentreY() - y * plot.getHeight() * 0.5f);
        };

        // axes through zero, then the unity reference
        g.setColour(p.ink(0.07f));
        g.fillRect(plot.getX(), plot.getCentreY(), plot.getWidth(), 1.0f);
        g.fillRect(plot.getCentreX(), plot.getY(), 1.0f, plot.getHeight());
        g.setColour(p.ink(0.12f));
        g.drawLine({ toXY(-1.0f, -1.0f), toXY(1.0f, 1.0f) }, 1.0f);

        if (! shape) return;

        juce::Path curve;
        constexpr int steps = 120;
        for (int i = 0; i <= steps; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / (float) steps;
            const float y = juce::jlimit(-1.0f, 1.0f, shape(x));
            const auto pt = toXY(x, y);
            if (i == 0) curve.startNewSubPath(pt); else curve.lineTo(pt);
        }
        g.setColour(p.accent);
        g.strokePath(curve, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CurveView)
};

} // namespace vesper
