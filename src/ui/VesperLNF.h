#pragma once
/*  VesperLNF — HORIZON look & feel.
    Controls are light, not hardware: a rotary is a floating arc with a
    star of light at its value point — no body, no pointer, no chrome.
    Vector-only; crisp at any scale and DPI. */

#include "Theme.h"

namespace vesper {

class VesperLNF : public juce::LookAndFeel_V4
{
public:
    VesperLNF() { applyColours(); }

    /* Re-read the palette (call after a theme mode / contrast change). */
    void applyColours()
    {
        auto& p = theme::palette();
        const auto sheet = p.night.withAlpha(0.98f);

        // The V4 scheme drives whatever we don't draw ourselves — the
        // standalone window chrome, dialogs, alert windows (D-033). Locked to
        // the palette so no stock JUCE colour ever reaches the screen. Must
        // come first: it resets component colours, our overrides follow.
        setColourScheme({ p.night,        // windowBackground
                          p.night,        // widgetBackground
                          p.night,        // menuBackground
                          p.ink(0.09f),   // outline
                          p.text,         // defaultText
                          p.ink(0.05f),   // defaultFill
                          p.text,         // highlightedText
                          p.ink(0.10f),   // highlightedFill
                          p.text });      // menuText

        setColour(juce::ResizableWindow::backgroundColourId, p.bg);
        setColour(juce::DocumentWindow::textColourId, p.textDim);
        setColour(juce::Label::textColourId, p.text);
        setColour(juce::Slider::textBoxTextColourId, p.text);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::backgroundColourId, p.bgDeep);
        setColour(juce::TextEditor::textColourId, p.text);
        setColour(juce::TextEditor::outlineColourId, p.stroke);
        setColour(juce::TextEditor::focusedOutlineColourId, p.accent.withAlpha(0.6f));
        setColour(juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::ComboBox::textColourId, p.textDim);
        setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::ComboBox::arrowColourId, p.textFaint);
        setColour(juce::PopupMenu::backgroundColourId, sheet);
        setColour(juce::PopupMenu::textColourId, p.text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, p.ink(0.10f));
        setColour(juce::PopupMenu::highlightedTextColourId, p.text);
        setColour(juce::PopupMenu::headerTextColourId, p.textFaint);
        setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        setColour(juce::TextButton::textColourOffId, p.textDim);
        setColour(juce::TextButton::textColourOnId, p.accent);
        setColour(juce::ToggleButton::textColourId, p.textDim);
        setColour(juce::TooltipWindow::backgroundColourId, sheet);
        setColour(juce::TooltipWindow::textColourId, p.text);
        setColour(juce::TooltipWindow::outlineColourId, p.stroke);
        setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::ScrollBar::thumbColourId, p.textFaint);
        setColour(juce::CaretComponent::caretColourId, p.accent);
    }

    // ------------------------------------------------------ the arc rotary --
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider& slider) override
    {
        auto& p = theme::palette();
        const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(6.0f);
        const float size  = juce::jmin(bounds.getWidth(), bounds.getHeight());
        const auto  area  = bounds.withSizeKeepingCentre(size, size);
        const auto  c     = area.getCentre();
        const float r     = size * 0.5f - 3.0f;
        // D-049: chunky arcs — the control should read as a solid object,
        // not a hairline. Roughly 1.8x the previous weight.
        const float track = juce::jlimit(3.5f, 9.0f, size * 0.080f);
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        const bool  hover = slider.isMouseOverOrDragging() || slider.hasKeyboardFocus(true);
        const bool  bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;

        // Ghost track — full-weight now (D-049): the unfilled part of the
        // ring reads as the body of the control, so the knob looks solid.
        juce::Path bg;
        bg.addCentredArc(c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
        g.setColour(p.ink(hover ? 0.15f : 0.10f));
        g.strokePath(bg, juce::PathStrokeType(track, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

        // Value arc: layered strokes = light, not paint
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        if (std::abs(angle - from) > 0.015f)
        {
            juce::Path val;
            val.addCentredArc(c.x, c.y, r, r, 0.0f,
                              juce::jmin(from, angle), juce::jmax(from, angle), true);
            const auto col = slider.isEnabled() ? p.accent : p.textFaint;
            if (hover) // glow is earned: only while engaged (02-COLOR law 4)
            {
                g.setColour(col.withAlpha(0.16f));
                g.strokePath(val, juce::PathStrokeType(track * 3.0f,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            }
            g.setColour(col.withAlpha(hover ? 1.0f : 0.85f));
            g.strokePath(val, juce::PathStrokeType(track, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }

        // The value point — crisp at rest, lit when engaged
        const auto tip = c.getPointOnCircumference(r, angle);
        theme::drawGlowDot(g, tip, track * (hover ? 1.05f : 0.85f),
                           slider.isEnabled() ? p.accent : p.textFaint,
                           hover ? 3.5f : 0.0f);
    }

    // ----------------------------------------------------------- linear ----
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                          float minPos, float maxPos,
                          juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        juce::ignoreUnused(minPos, maxPos, style);
        auto& p = theme::palette();
        const auto r = juce::Rectangle<int>(x, y, w, h).toFloat();
        const float cy = r.getCentreY(), track = 2.0f;
        g.setColour(p.stroke.withAlpha(0.5f));
        g.fillRoundedRectangle(r.getX(), cy - track * 0.5f, r.getWidth(), track, track * 0.5f);
        g.setColour(p.accent.withAlpha(0.8f));
        g.fillRoundedRectangle(r.getX(), cy - track * 0.5f, pos - r.getX(), track, track * 0.5f);
        theme::drawGlowDot(g, { pos, cy }, 3.0f, p.accent,
                           s.isMouseOverOrDragging() ? 3.5f : 0.0f);
    }

    // ---------------------------------------------------------- buttons ----
    /* Toggle-capable buttons are Switches: a point on a rail (06). Narrow
       toggles (EQ band numbers) compact to text + state point. Momentary
       buttons stay quiet words. No pills, no boxes anywhere. */
    void drawButtonBackground(juce::Graphics& g, juce::Button& b,
                              const juce::Colour&, bool highlighted, bool down) override
    {
        if (! b.getClickingTogglesState())
        {
            if (highlighted || down)
            {
                auto& p = theme::palette();
                g.setColour(p.ink(down ? 0.09f : 0.045f));
                g.fillRoundedRectangle(b.getLocalBounds().toFloat().reduced(1.0f),
                                       theme::radiusSm);
            }
            return;
        }

        auto& p = theme::palette();
        const bool on = b.getToggleState();
        const float cy = (float) b.getHeight() * 0.5f;

        if (b.getWidth() >= 56) // switch: rail + travelling point
        {
            const float x0 = 4.0f, x1 = 26.0f;
            g.setColour(p.ink(0.16f));
            g.fillRect(x0, cy - 0.75f, x1 - x0, 1.5f);
            theme::drawGlowDot(g, { on ? x1 : x0, cy }, 3.5f,
                               on ? p.accent : p.ink(0.38f),
                               on && (highlighted || down) ? 3.0f : 0.0f);
        }
        else // compact: state point beneath the text
        {
            theme::drawGlowDot(g, { b.getWidth() * 0.5f, (float) b.getHeight() - 4.0f },
                               2.5f, on ? p.accent : p.ink(0.16f), 0.0f);
        }
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool over, bool) override
    {
        auto& p = theme::palette();
        const bool toggles = b.getClickingTogglesState();
        auto r = b.getLocalBounds();

        if (toggles && b.getWidth() >= 56) // switch label, right of the rail
        {
            g.setFont(theme::labelFont(9.5f));
            g.setColour(! b.isEnabled() ? p.textFaint.withAlpha(0.5f)
                        : b.getToggleState() ? p.text
                        : over ? p.textDim : p.textFaint);
            g.drawText(b.getButtonText().toUpperCase(), r.withTrimmedLeft(34),
                       juce::Justification::centredLeft);
            return;
        }
        if (toggles) // compact toggle: text above its state point
            r.removeFromBottom(7);

        /* Secondary buttons (the "quiet" property) speak one step down the
           ink ladder — configuration and housekeeping must not compete with
           the verbs that do the work (D-056, MATCH strip hierarchy). */
        const bool quiet = (bool) b.getProperties().getWithDefault("quiet", false);
        g.setFont(theme::font(quiet ? 11.0f : 12.5f, b.getToggleState()));
        g.setColour(! b.isEnabled() ? p.textFaint.withAlpha(0.5f)
                    : b.getToggleState() ? p.accent
                    : quiet ? (over ? p.textDim : p.textFaint)
                    : over ? p.text : p.textDim);
        g.drawText(b.getButtonText(), r, juce::Justification::centred);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b,
                          bool highlighted, bool down) override
    {
        drawButtonBackground(g, b, {}, highlighted, down);
        auto& p = theme::palette();
        g.setFont(theme::labelFont(10.5f));
        g.setColour(b.getToggleState() ? p.accent : p.textDim);
        g.drawText(b.getButtonText().toUpperCase(), b.getLocalBounds(),
                   juce::Justification::centred);
    }

    // --------------------------------------------------------- combobox ----
    /* A selector is a clearly-clickable field (09/D-022): rounded container,
       hairline border (brighter on hover), value + a down-chevron caret in a
       divided cell — reads unmistakably as a dropdown. */
    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox& box) override
    {
        /* D-056 — one field family. The combo wears the stepper's casing
           (drawSurface at radiusSm, value centred in the value voice) so a
           long list and a short list read as the same control, differing
           only in how they open. The boxed caret cell is gone — the
           chevron speaks alone, like the stepper's arrows. */
        auto& p = theme::palette();
        const bool over = box.isMouseOver() || box.hasKeyboardFocus(true);
        auto r = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height).reduced(0.5f);

        theme::drawSurface(g, r, theme::radiusSm, over);

        juce::Path chevron;
        const float ax = (float) width - 13.0f, ay = (float) height * 0.5f;
        chevron.startNewSubPath(ax - 3.5f, ay - 1.5f);
        chevron.lineTo(ax, ay + 2.5f);
        chevron.lineTo(ax + 3.5f, ay - 1.5f);
        g.setColour(over ? p.text : p.textDim);
        g.strokePath(chevron, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(20, 0, box.getWidth() - 40, box.getHeight());
        label.setFont(getComboBoxFont(box));
        label.setJustificationType(juce::Justification::centred);
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override { return theme::valueFont(12.5f); }
    juce::Font getPopupMenuFont() override { return theme::font(13.5f); }
    juce::Font getLabelFont(juce::Label& l) override
    {
        juce::ignoreUnused(l);
        return theme::font(12.0f);
    }

    // ---------------------------------------------- standalone chrome (D-033) --
    /* The standalone window's title bar is JUCE-drawn: night field, hairline
       rule, the name as a quiet label — the shell reads as the instrument. */
    void drawDocumentWindowTitleBar(juce::DocumentWindow& w, juce::Graphics& g,
                                    int width, int height, int titleSpaceX,
                                    int titleSpaceW, const juce::Image*, bool) override
    {
        auto& p = theme::palette();
        g.fillAll(p.night);
        g.setColour(p.ink(0.07f));
        g.fillRect(0, height - 1, width, 1);
        g.setColour(w.isActiveWindow() ? p.textDim : p.textFaint);
        g.setFont(theme::labelFont(11.0f));
        g.drawText(w.getName().toUpperCase(), titleSpaceX, 0, titleSpaceW, height,
                   juce::Justification::centred);
    }

    /* Monochrome glyph buttons — no stock traffic-light colours. */
    juce::Button* createDocumentWindowButton(int buttonType) override
    {
        struct ChromeButton : juce::Button
        {
            explicit ChromeButton(int t) : juce::Button({}), kind(t) {}
            void paintButton(juce::Graphics& g, bool over, bool down) override
            {
                auto& p = theme::palette();
                const auto b = getLocalBounds().toFloat();
                if (over || down)
                {
                    g.setColour(p.ink(down ? 0.12f : 0.06f));
                    g.fillRoundedRectangle(b.reduced(3.0f), theme::radiusXs);
                }
                const auto c = b.getCentre();
                const float r = 4.0f;
                juce::Path glyph;
                if (kind == juce::DocumentWindow::closeButton)
                {
                    glyph.startNewSubPath(c.x - r, c.y - r); glyph.lineTo(c.x + r, c.y + r);
                    glyph.startNewSubPath(c.x + r, c.y - r); glyph.lineTo(c.x - r, c.y + r);
                }
                else if (kind == juce::DocumentWindow::minimiseButton)
                {
                    glyph.startNewSubPath(c.x - r, c.y); glyph.lineTo(c.x + r, c.y);
                }
                else glyph.addRectangle(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
                g.setColour(over || down ? p.text : p.textDim);
                g.strokePath(glyph, juce::PathStrokeType(1.3f));
            }
            int kind;
        };
        return new ChromeButton(buttonType);
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override
    {
        auto& p = theme::palette();
        g.setColour(p.night.withAlpha(0.98f));
        g.fillRoundedRectangle(0.0f, 0.0f, (float) width, (float) height, theme::radiusMd);
        g.setColour(p.ink(0.045f));
        g.fillRoundedRectangle(0.0f, 0.0f, (float) width, (float) height, theme::radiusMd);
        g.setColour(p.stroke);
        g.drawRoundedRectangle(0.5f, 0.5f, width - 1.0f, height - 1.0f, theme::radiusMd, 1.0f);
    }

    int getPopupMenuBorderSize() override { return 8; }

    // --------------------------------------------------------- text edit ---
    void fillTextEditorBackground(juce::Graphics& g, int w, int h, juce::TextEditor&) override
    {
        g.setColour(theme::palette().bgDeep.withAlpha(0.85f));
        g.fillRoundedRectangle(0.0f, 0.0f, (float) w, (float) h, theme::radiusSm);
    }
    void drawTextEditorOutline(juce::Graphics& g, int w, int h, juce::TextEditor& te) override
    {
        auto& p = theme::palette();
        g.setColour(te.hasKeyboardFocus(true) ? p.accent.withAlpha(0.45f) : p.stroke);
        g.drawRoundedRectangle(0.5f, 0.5f, w - 1.0f, h - 1.0f, theme::radiusSm, 1.0f);
    }
};

} // namespace vesper
