#pragma once
/*  Stepper — ‹ value › for a small choice parameter (D-051).

    The Subculture reference reads its discrete choices as a centred value
    between two chevrons, with a dot per option underneath. That is a better
    fit than a dropdown for short lists: the whole range is visible at a
    glance, changing it is one click rather than click-then-pick, and it
    scrubs with the wheel.

    Long lists keep the dropdown — stepping through seventeen saturation
    algorithms one chevron at a time would be worse, not better, so the
    factory chooses per parameter. */

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace vesper {

class Stepper : public juce::Component,
                public juce::SettableTooltipClient
{
public:
    /* Show dots only when they stay legible — beyond this the row is noise. */
    static constexpr int maxDots = 8;

    Stepper(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
            const juce::String& captionIn)
        : caption(captionIn)
    {
        param = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramID));
        jassert(param != nullptr); // steppers are for choice parameters only
        if (param != nullptr)
        {
            choices = param->choices;
            att = std::make_unique<juce::ParameterAttachment>(
                *param, [this] (float) { repaint(); });
            att->sendInitialUpdate();
        }
        setTitle(captionIn);
        setWantsKeyboardFocus(true);
        setTooltip(captionIn);
    }

    void paint(juce::Graphics& g) override
    {
        auto& p = theme::palette();
        const bool hot = isMouseOver() || hasKeyboardFocus(false);

        auto r = getLocalBounds();
        auto capArea = r.removeFromTop(12);
        r.removeFromTop(3); // D-065: caption clear of the pill — keep rowArea() in step
        g.setFont(theme::microFont(9.0f));
        g.setColour(p.textFaint);
        g.drawText(caption, capArea, juce::Justification::centred);

        auto dots = showDots() ? r.removeFromBottom(8) : juce::Rectangle<int>();
        auto row  = r;

        theme::drawSurface(g, row.toFloat().reduced(0.5f), theme::radiusSm, hot);

        // chevrons — dimmed at the ends so the range is legible
        const int idx = currentIndex();
        g.setFont(theme::font(13.0f));
        g.setColour(idx > 0 ? (hoverLeft ? p.text : p.textDim) : p.ink(0.14f));
        g.drawText(juce::CharPointer_UTF8("\xe2\x80\xb9"), leftArrow(),
                   juce::Justification::centred);
        g.setColour(idx < choices.size() - 1 ? (hoverRight ? p.text : p.textDim)
                                             : p.ink(0.14f));
        g.drawText(juce::CharPointer_UTF8("\xe2\x80\xba"), rightArrow(),
                   juce::Justification::centred);

        g.setColour(p.text);
        g.setFont(theme::valueFont(12.5f));
        g.drawText(choices.isEmpty() ? juce::String() : choices[idx],
                   row.reduced(20, 0), juce::Justification::centred, false);

        if (showDots())
        {
            const int n = choices.size();
            const float step = 9.0f;
            const float x0 = dots.getCentreX() - (n - 1) * step * 0.5f;
            for (int i = 0; i < n; ++i)
            {
                const auto c = juce::Point<float>(x0 + i * step, (float) dots.getCentreY());
                if (i == idx) theme::drawGlowDot(g, c, 2.0f, p.accent, 0.0f);
                else { g.setColour(p.ink(0.16f));
                       g.fillEllipse(juce::Rectangle<float>(3.0f, 3.0f).withCentre(c)); }
            }
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const bool l = leftArrow().contains(e.getPosition());
        const bool r = rightArrow().contains(e.getPosition());
        if (l != hoverLeft || r != hoverRight)
        {
            hoverLeft = l; hoverRight = r;
            setMouseCursor(l || r ? juce::MouseCursor::PointingHandCursor
                                  : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        hoverLeft = hoverRight = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (leftArrow().contains(e.getPosition()))       nudge(-1);
        else if (rightArrow().contains(e.getPosition())) nudge(+1);
        else if (choices.size() > maxDots)               showList(); // long lists
        else                                             nudge(+1);  // click cycles
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (std::abs(w.deltaY) > 0.01f) nudge(w.deltaY > 0.0f ? +1 : -1);
    }

    bool keyPressed(const juce::KeyPress& k) override
    {
        if (k.isKeyCode(juce::KeyPress::leftKey) || k.isKeyCode(juce::KeyPress::downKey))
        { nudge(-1); return true; }
        if (k.isKeyCode(juce::KeyPress::rightKey) || k.isKeyCode(juce::KeyPress::upKey))
        { nudge(+1); return true; }
        return false;
    }

private:
    bool showDots() const { return choices.size() > 1 && choices.size() <= maxDots; }
    int  currentIndex() const
    {
        return param == nullptr ? 0
                                : juce::jlimit(0, juce::jmax(0, choices.size() - 1),
                                               param->getIndex());
    }
    juce::Rectangle<int> rowArea() const
    {
        auto r = getLocalBounds().withTrimmedTop(15); // 12 caption + 3 air (D-065)
        if (showDots()) r.removeFromBottom(8);
        return r;
    }
    juce::Rectangle<int> leftArrow()  const { return rowArea().removeFromLeft(22); }
    juce::Rectangle<int> rightArrow() const { return rowArea().removeFromRight(22); }

    void nudge(int delta)
    {
        if (param == nullptr || choices.isEmpty()) return;
        const int next = juce::jlimit(0, choices.size() - 1, currentIndex() + delta);
        if (next == currentIndex()) return;
        att->setValueAsCompleteGesture(
            param->convertTo0to1((float) next));
    }

    /* Long lists still get a pick-list, reached by clicking the value. */
    void showList()
    {
        if (param == nullptr) return;
        juce::PopupMenu m;
        for (int i = 0; i < choices.size(); ++i)
            m.addItem(i + 1, choices[i], true, i == currentIndex());
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                        [this] (int r)
                        {
                            if (r > 0 && param != nullptr)
                                att->setValueAsCompleteGesture(
                                    param->convertTo0to1((float) (r - 1)));
                        });
    }

    juce::String caption;
    juce::AudioParameterChoice* param = nullptr;
    juce::StringArray choices;
    std::unique_ptr<juce::ParameterAttachment> att;
    bool hoverLeft = false, hoverRight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Stepper)
};

} // namespace vesper
