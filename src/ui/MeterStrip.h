#pragma once
/*  MeterStrip — telemetry as three labeled rows (09 §6): each row is a Micro
    label left, a tabular Value right, and a thin meter bar beneath. LUFS =
    output loudness, CORR = stereo correlation (bipolar, centre-zero), GR =
    gain reduction (fills from the right). Clipping is the only loud voice. */

#include "../VesperProcessor.h"
#include "Theme.h"

namespace vesper {

class MeterStrip : public juce::Component, private juce::Timer
{
public:
    explicit MeterStrip(VesperProcessor& procIn) : proc(procIn)
    {
        startTimerHz(60);
        setInterceptsMouseClicks(false, false);
    }

    void paint(juce::Graphics& g) override
    {
        auto& p = theme::palette();
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        const float rowH = r.getHeight() / 3.0f;

        const auto lufsStr = lufs <= -69.0f
            ? juce::String(juce::CharPointer_UTF8("\xe2\x80\x93"))
            : juce::String(lufs, 1);

        drawRow(g, r.removeFromTop(rowH), "LUFS", lufsStr,
                juce::jlimit(0.0f, 1.0f, (lufs + 36.0f) / 36.0f), // -36..0 LUFS
                Fill::left, false);
        drawRow(g, r.removeFromTop(rowH), "CORR", juce::String(corr, 2),
                (corr + 1.0f) * 0.5f, Fill::centre, false);
        drawRow(g, r.removeFromTop(rowH), "GR", juce::String(gr, 1),
                juce::jlimit(0.0f, 1.0f, -gr / 18.0f),            // 0..-18 dB
                Fill::right, false);

        // Clip flag: the out level touching 0 dBFS turns the LUFS row red
        if (juce::jmax(outL, outR) > -0.2f)
        {
            g.setColour(p.danger.withAlpha(0.9f));
            g.fillRect(r.getX(), 0.0f, 2.0f, (float) getHeight());
        }
    }

private:
    enum class Fill { left, right, centre };

    void drawRow(juce::Graphics& g, juce::Rectangle<float> row, const char* label,
                 const juce::String& value, float t, Fill fill, bool)
    {
        auto& p = theme::palette();
        /* D-052 — the mockup's telemetry row: label, then the bar occupying
           the middle, then the value hard right, all on ONE line. Previously
           the text sat above its own bar in a stack, which at this height
           left both cramped (D-051 relieved the collision; this removes the
           stack entirely). */
        row.removeFromBottom(2.0f);                        // row-to-row air
        auto line = row;

        auto labelCol = line.removeFromLeft(34.0f);
        auto valueCol = line.removeFromRight(46.0f);
        line.removeFromLeft(6.0f);
        line.removeFromRight(8.0f);
        auto bar = line.withSizeKeepingCentre(line.getWidth(), 3.0f);

        g.setColour(p.textFaint);
        g.setFont(theme::microFont(9.0f));
        g.drawText(label, labelCol.toNearestInt(), juce::Justification::centredLeft, false);
        g.setColour(p.textDim);
        g.setFont(theme::valueFont(11.5f));
        g.drawText(value, valueCol.toNearestInt(), juce::Justification::centredRight, false);
        const float rad = bar.getHeight() * 0.5f;
        g.setColour(p.ink(0.14f));
        g.fillRoundedRectangle(bar, rad);
        g.setColour(p.accent.withAlpha(0.9f));
        t = juce::jlimit(0.0f, 1.0f, t);

        auto tick = [&] (float x) // bright head at the reading (mockup)
        {
            g.setColour(p.text.withAlpha(0.92f));
            g.fillRoundedRectangle(juce::Rectangle<float>(x - 1.0f, bar.getY() - 1.0f,
                                                          2.0f, bar.getHeight() + 2.0f),
                                   1.0f);
        };

        switch (fill)
        {
            case Fill::left:
            {
                const float w = bar.getWidth() * t;
                g.fillRoundedRectangle(bar.withWidth(juce::jmax(rad * 2.0f, w)), rad);
                tick(bar.getX() + w);
                break;
            }
            case Fill::right:
            {
                const float w = bar.getWidth() * t;
                g.fillRoundedRectangle(bar.withTrimmedLeft(bar.getWidth() - w), rad);
                tick(bar.getRight() - w);
                break;
            }
            case Fill::centre:
            {
                const float mid = bar.getCentreX();
                const float w = (t - 0.5f) * bar.getWidth();
                if (w >= 0.0f)
                    g.fillRoundedRectangle({ mid, bar.getY(), w, bar.getHeight() }, rad);
                else
                    g.fillRoundedRectangle({ mid + w, bar.getY(), -w, bar.getHeight() }, rad);
                g.setColour(p.ink(0.30f)); // centre reference
                g.fillRect(mid - 0.5f, bar.getY() - 1.0f, 1.0f, bar.getHeight() + 2.0f);
                tick(mid + w);
                break;
            }
        }
    }

    void timerCallback() override
    {
        auto decay = [] (float& v, float target)
        {
            v = target > v ? target : v * 0.86f + target * 0.14f;
        };
        decay(inL,  dsp::gainToDb(proc.meterIn.consumePeak(0)));
        decay(inR,  dsp::gainToDb(proc.meterIn.consumePeak(1)));
        decay(outL, dsp::gainToDb(proc.meterOut.consumePeak(0)));
        decay(outR, dsp::gainToDb(proc.meterOut.consumePeak(1)));
        lufs = proc.meterOut.lufs.load(std::memory_order_relaxed);
        corr = proc.meterOut.correlation.load(std::memory_order_relaxed);
        gr   = juce::jmin(proc.comp.grDb.load(std::memory_order_relaxed),
                          proc.limiter.grDb.load(std::memory_order_relaxed));
        repaint();
    }

    VesperProcessor& proc;
    float inL = -70.0f, inR = -70.0f, outL = -70.0f, outR = -70.0f;
    float lufs = -70.0f, corr = 1.0f, gr = 0.0f;
};

} // namespace vesper
