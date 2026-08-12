#pragma once
/*  Knob — a floating arc of light with the value set inside it.
      drag           coarse        |  shift+drag    fine (velocity mode)
      mouse wheel    step          |  shift+wheel   fine step
      double-click   reset default |  right-click   context menu (type a value)
    Values are always visible — no hover hunting. Fully keyboard- and
    screen-reader-accessible via the underlying Slider. */

#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/Creative.h"
#include "../core/Params.h"
#include "Theme.h"

namespace vesper {

class Knob : public juce::Component
{
public:
    /* Set by the owning panel to enable the macro assignment editor. */
    MacroEngine* macroEngine = nullptr;

    Knob(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramIDIn,
         const juce::String& labelText)
        : label(labelText), paramID(paramIDIn)
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setMouseDragSensitivity(160);
        slider.setVelocityModeParameters(0.6, 1, 0.06, true,
                                         juce::ModifierKeys::shiftModifier);
        slider.setScrollWheelEnabled(true);
        slider.setPopupDisplayEnabled(false, false, nullptr);
        slider.setWantsKeyboardFocus(true);
        slider.setTitle(labelText);
        slider.setTooltip(labelText);

        if (auto* p = apvts.getParameter(paramID))
        {
            unit = p->getLabel();
            slider.setDoubleClickReturnValue(
                true, p->convertFrom0to1(p->getDefaultValue()));
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(p))
                choiceNames = choice->choices;
        }
        percent01 = paramID.startsWith("macro"); // 0..1 macros read as percent
        if (percent01) macroIndex = paramID.getTrailingIntValue(); // 1..4, else 0
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, paramID, slider);

        /* Accessibility (D-046): a screen reader must announce the value a
           sighted user sees. The arc draws its own numeral (there is no
           text box), so without this the announced value is JUCE's raw
           default — no unit, and none of the sync / percent / choice
           overrides. Route both through the same formatter. */
        slider.textFromValueFunction = [this] (double) { return valueText(); };
        slider.updateText();

        addAndMakeVisible(slider);
        slider.onValueChange = [this] { slider.updateText(); repaint(); };
        slider.addMouseListener(this, false);
    }

    /* The reading as drawn: numeral + unit, honouring every override. */
    juce::String valueText() const
    {
        if (! choiceNames.isEmpty())
            return choiceNames[juce::jlimit(0, choiceNames.size() - 1,
                                            (int) std::round(slider.getValue()))];
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        auto vt = theme::formatValue(slider.getValue(), unit, bipolar);
        overrideReadout(vt);
        return vt.unit.isEmpty() ? vt.numeral : vt.numeral + " " + vt.unit;
    }

    ~Knob() override { slider.removeMouseListener(this); }

    /* M6: while `syncParam` is on, the readout shows the musical division
       ("1/8" + SYNC) instead of the numeric value — the knob still edits the
       free value underneath, which returns the moment sync goes off. */
    void followSync(juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& syncParam, const juce::String& divParam)
    {
        syncOn = apvts.getRawParameterValue(syncParam);
        divIdx = apvts.getRawParameterValue(divParam);
        const auto onChange = [this] (float) { slider.updateText(); repaint(); };
        syncAtt = std::make_unique<juce::ParameterAttachment>(
            *apvts.getParameter(syncParam), onChange);
        divAtt = std::make_unique<juce::ParameterAttachment>(
            *apvts.getParameter(divParam), onChange);
    }

    /* Compact mode (09 reference, small dials): label ABOVE, arc, numeral
       BELOW at readable size — values never squeeze inside tiny arcs. */
    bool compact() const { return getHeight() < 86 || getWidth() < 62; }

    void resized() override
    {
        /* D-065: the caption and the numeral each get a hair of air between
           them and the arc — they used to abut it exactly, so a knob read
           as one dense block of text-ring-text. The air is CONDITIONAL:
           in a cell too short to afford it the arc would collapse and the
           numeral would clip, and legibility always outranks breathing
           room. */
        auto r = getLocalBounds();
        const int air = r.getHeight() >= 70 ? 2 : 0;
        if (compact())
        {
            labelArea = r.removeFromTop(11);
            r.removeFromTop(air);
            valueArea = r.removeFromBottom(14);
            r.removeFromBottom(air);
        }
        else
        {
            labelArea = r.removeFromBottom(15);
            r.removeFromBottom(air);
            valueArea = {};
        }
        slider.setBounds(r);
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        // Numeral + unit as one instrument reading (docs/design/03).
        auto& p = theme::palette();
        const bool active = slider.isMouseOverOrDragging() || slider.hasKeyboardFocus(true);

        // Macro knobs carry their assignment count, so a macro that drives
        // nothing reads differently from one driving six things (D-041).
        // Drawn before the compact branch returns — the M knobs are compact.
        if (macroIndex > 0 && macroEngine != nullptr)
        {
            const int n = macroEngine->targetCount(macroIndex);
            if (n > 0)
            {
                // hug the visible arc, not the slider's bounds: the LNF insets
                // by 6 and centres a square, so the box is wider than the arc
                const auto sb = slider.getBounds().toFloat().reduced(6.0f);
                const float side = juce::jmin(sb.getWidth(), sb.getHeight());
                const auto arc = sb.withSizeKeepingCentre(side, side);
                const auto badge = juce::Rectangle<float>(13.0f, 11.0f)
                                       .withCentre({ arc.getRight() - 1.0f, arc.getY() + 3.0f });
                g.setColour(p.accent.withAlpha(0.9f));
                g.fillRoundedRectangle(badge, theme::radiusXs);
                g.setColour(p.night);
                g.setFont(theme::microFont(8.0f));
                g.drawText(juce::String(n), badge.toNearestInt(), juce::Justification::centred);
            }
        }

        if (compact())
        {
            const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
            auto vt = theme::formatValue(slider.getValue(), unit, bipolar);
            overrideReadout(vt);
            g.setFont(theme::microFont(8.5f));
            g.setColour(active ? p.textDim : p.textFaint);
            g.drawText(label.toUpperCase(), labelArea, juce::Justification::centred);

            auto vf = theme::valueFont(12.0f);
            auto uf = theme::microFont(8.5f);
            const float vw = vf.getStringWidthFloat(vt.numeral);
            const bool showUnit = vt.unit.isNotEmpty()
                                  && vw + uf.getStringWidthFloat(vt.unit) + 3.0f
                                         < (float) getWidth() - 2.0f;
            const float uw = showUnit ? uf.getStringWidthFloat(vt.unit) + 3.0f : 0.0f;
            const float x0 = (float) valueArea.getCentreX() - (vw + uw) * 0.5f;
            g.setColour(active ? p.accent : p.text.withAlpha(0.88f));
            g.setFont(vf);
            g.drawText(vt.numeral,
                       juce::Rectangle<float>(x0, (float) valueArea.getY(),
                                              vw + 2.0f, (float) valueArea.getHeight())
                           .toNearestInt(),
                       juce::Justification::centredLeft);
            if (showUnit)
            {
                g.setFont(uf);
                g.setColour(p.textFaint);
                g.drawText(vt.unit,
                           juce::Rectangle<float>(x0 + vw + 3.0f, (float) valueArea.getY(),
                                                  uw, (float) valueArea.getHeight() - 1.0f)
                               .toNearestInt(),
                           juce::Justification::bottomLeft);
            }
            return;
        }

        const auto sliderArea = slider.getBounds().toFloat();
        const float size = juce::jmin(sliderArea.getWidth(), sliderArea.getHeight());
        const float vh = juce::jlimit(11.0f, 28.0f, size * 0.19f);

        g.setColour(active ? p.accent : p.text.withAlpha(size < 60.0f ? 0.72f : 0.92f));

        if (! choiceNames.isEmpty()) // choices are words, no unit
        {
            g.setFont(theme::valueFont(juce::jmin(vh, 15.0f)));
            g.drawText(displayText(), sliderArea.toNearestInt(),
                       juce::Justification::centred);
        }
        else
        {
            const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
            auto vt = theme::formatValue(slider.getValue(), unit, bipolar);
            overrideReadout(vt);
            auto vf = theme::valueFont(vh);
            auto uf = theme::microFont(juce::jmax(8.5f, vh * 0.38f));
            const bool showUnit = vt.unit.isNotEmpty() && size >= 66.0f;
            const float vw = vf.getStringWidthFloat(vt.numeral);
            const float uw = showUnit ? uf.getStringWidthFloat(vt.unit) + 3.0f : 0.0f;
            const float x0 = sliderArea.getCentreX() - (vw + uw) * 0.5f;
            const float cy = sliderArea.getCentreY();

            g.setFont(vf);
            g.drawText(vt.numeral,
                       juce::Rectangle<float>(x0, cy - vh * 0.5f, vw + 2.0f, vh)
                           .toNearestInt(),
                       juce::Justification::centredLeft);
            if (showUnit)
            {
                g.setFont(uf);
                g.setColour(p.textFaint);
                g.drawText(vt.unit,
                           juce::Rectangle<float>(x0 + vw + 3.0f, cy - vh * 0.5f,
                                                  uw, vh * 0.94f).toNearestInt(),
                           juce::Justification::bottomLeft);
            }
        }

        // Label: one emphasis step up while engaged
        g.setFont(theme::labelFont(size < 66.0f ? 9.5f : 11.0f));
        g.setColour(active ? p.textDim : p.textFaint);
        g.drawText(label.toUpperCase(), labelArea, juce::Justification::centred);

    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        juce::PopupMenu m;
        m.addItem("Enter value…", [this] { beginTextEntry(); });
        m.addItem("Reset to default", [this]
                  { slider.setValue(slider.getDoubleClickReturnValue(),
                                    juce::sendNotificationSync); });

        // Macro assignment editor (D-020)
        if (macroEngine != nullptr && MacroEngine::assignable(paramID))
        {
            m.addSeparator();
            const int cur = macroEngine->macroFor(paramID);
            juce::PopupMenu follow;
            for (int i = 1; i <= 4; ++i)
                follow.addItem("Macro " + juce::String(i), true, cur == i,
                               [this, i] { macroEngine->assign(paramID, i); });
            m.addSubMenu("Follow macro", follow);
            if (cur > 0)
            {
                m.addItem("Macro travel: MIN is here", [this]
                          { macroEngine->setEndToCurrent(paramID, false); });
                m.addItem("Macro travel: MAX is here", [this]
                          { macroEngine->setEndToCurrent(paramID, true); });
                m.addItem("Stop following Macro " + juce::String(cur), [this]
                          { macroEngine->remove(paramID); });
            }
        }
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
    }

    juce::Slider& getSlider() { return slider; }

private:
    /* Sync + percent readouts replace the numeric text (M6). */
    void overrideReadout(theme::ValueText& vt) const
    {
        if (syncOn != nullptr && syncOn->load() > 0.5f)
        {
            const auto& names = delayDivNames();
            vt.numeral = names[juce::jlimit(0, names.size() - 1,
                                            (int) std::round(divIdx->load()))];
            vt.unit = "SYNC";
        }
        else if (percent01)
        {
            vt.numeral = juce::String((int) std::round(slider.getValue() * 100.0));
            vt.unit = "%";
        }
    }

    juce::String displayText() const
    {
        const double v = slider.getValue();
        if (! choiceNames.isEmpty())
            return choiceNames[juce::jlimit(0, choiceNames.size() - 1, (int) std::round(v))];
        const double a = std::abs(v);
        if (a >= 10000.0) return juce::String(v / 1000.0, 1) + "k";
        if (a >= 1000.0)  return juce::String(v / 1000.0, 2) + "k";
        if (a >= 100.0)   return juce::String(v, 0);
        if (a >= 10.0)    return juce::String(v, 1);
        return juce::String(v, 2);
    }

    void beginTextEntry()
    {
        auto* ed = new juce::TextEditor();
        ed->setJustification(juce::Justification::centred);
        ed->setFont(theme::font(13.0f));
        ed->setText(juce::String(slider.getValue(), 2), juce::dontSendNotification);
        ed->setSize(84, 24);
        ed->selectAll();

        auto& box = juce::CallOutBox::launchAsynchronously(
            std::unique_ptr<juce::Component>(ed), getScreenBounds(), nullptr);
        ed->grabKeyboardFocus();
        ed->onReturnKey = [this, ed, &box]
        {
            slider.setValue(ed->getText().getDoubleValue(), juce::sendNotificationSync);
            box.dismiss();
        };
        ed->onEscapeKey = [&box] { box.dismiss(); };
    }

    juce::Slider slider;
    juce::String label, paramID, unit;
    juce::StringArray choiceNames;
    juce::Rectangle<int> labelArea, valueArea;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::atomic<float>* syncOn = nullptr;   // M6 sync readout
    std::atomic<float>* divIdx = nullptr;
    std::unique_ptr<juce::ParameterAttachment> syncAtt, divAtt;
    bool percent01 = false;                 // macros: 0..1 shown as %
    int  macroIndex = 0;                    // 1..4 for macro knobs, else 0

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Knob)
};

} // namespace vesper
