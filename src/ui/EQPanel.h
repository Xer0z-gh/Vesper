#pragma once
/*  EQPanel — split from Panels.h (10-AUDIT Workstream 4): at 500+ lines
    the band-focus EQ deserved its own file. Behaviour unchanged. */

#include "../VesperProcessor.h"
#include "Knob.h"
#include "Theme.h"

namespace vesper {

// ---------------------------------------------------------------- EQPanel --
/* Band-focus layout (D-025): a selector strip of 6 bands on top; the
   selected band's FREQ/GAIN/Q/DYN show below as full-size arcs — every EQ
   dial gets workspace room and character, matching the other modules. */
class EQPanel : public juce::Component, private juce::Timer
{
public:
    explicit EQPanel(VesperProcessor& procIn) : proc(procIn)
    {
        startTimerHz(30);
        for (int b = 0; b < 6; ++b)
        {
            auto& s = bands[(size_t) b];
            s.type = std::make_unique<juce::ComboBox>();
            s.type->setTitle("Band " + juce::String(b + 1) + " type"); // a11y name
            s.type->addItemList(eqBandTypeNames(), 1);
            s.typeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                proc.apvts, eqBandID(b, "type"), *s.type);
            addChildComponent(*s.type);

            s.freq = std::make_unique<Knob>(proc.apvts, eqBandID(b, "freq"), "FREQ");
            s.gain = std::make_unique<Knob>(proc.apvts, eqBandID(b, "gain"), "GAIN");
            s.q    = std::make_unique<Knob>(proc.apvts, eqBandID(b, "q"),    "Q");
            s.dyn  = std::make_unique<Knob>(proc.apvts, eqBandID(b, "dyn"),  "DYN");
            for (auto* k : { s.freq.get(), s.gain.get(), s.q.get(), s.dyn.get() })
            {
                k->macroEngine = &proc.macros;
                addChildComponent(*k);
            }
            s.onAtt = std::make_unique<juce::ParameterAttachment>(
                *proc.apvts.getParameter(eqBandID(b, "on")), [this] (float) { repaint(); });

            /* D-064: HP / LP / NOTCH ignore gain in the DSP, so GAIN must
               stop advertising itself as live for those types — same rule
               the DIVISION field follows under SYNC (D-063). */
            s.typeGate = std::make_unique<juce::ParameterAttachment>(
                *proc.apvts.getParameter(eqBandID(b, "type")),
                [this, &s] (float v)
                {
                    const bool hasGain = eqTypeHasGain((int) v);
                    s.gain->setEnabled(hasGain);
                    s.gain->setAlpha(hasGain ? 1.0f : 0.4f);
                });
            s.typeGate->sendInitialUpdate();
        }
        // Linear-phase mode (D-036): whole-EQ switch, lives beside the band strip
        lpBtn.setClickingTogglesState(true);
        lpBtn.setTooltip("Linear phase: static curve with zero phase shift "
                         "(adds latency; DYN is inactive in this mode)");
        lpAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            proc.apvts, "eq_lp", lpBtn);
        addAndMakeVisible(lpBtn);

        setupMatch();
        applyBand(selected);
    }

    int selectedBand() const { return selected; }

    /* dB response at hz for the Field overlay. */
    float responseDbAt(double hz) const
    {
        std::array<dsp::ParametricEQ::Band, 6> b {};
        for (int i = 0; i < 6; ++i)
        {
            b[(size_t) i].on   = proc.apvts.getRawParameterValue(eqBandID(i, "on"))->load() > 0.5f;
            b[(size_t) i].type = (int) proc.apvts.getRawParameterValue(eqBandID(i, "type"))->load();
            b[(size_t) i].freq = proc.apvts.getRawParameterValue(eqBandID(i, "freq"))->load();
            b[(size_t) i].gain = proc.apvts.getRawParameterValue(eqBandID(i, "gain"))->load();
            b[(size_t) i].q    = proc.apvts.getRawParameterValue(eqBandID(i, "q"))->load();
        }
        return dsp::ParametricEQ::responseDbAt(b.data(), 48000.0, hz);
    }

    void paint(juce::Graphics& g) override
    {
        auto& p = theme::palette();
        // Band selectors are the module tiles' twin: a compact centred tile
        // per band (so the enable dot hugs the content, perfectly aligned),
        // number-over-type, state fill, accent underline + notch when
        // selected. Fixed tile width keeps all six identical and aligned.
        for (int b = 0; b < 6; ++b)
        {
            const auto tile = bandTile(b);
            auto cell = tile.toFloat();
            const bool sel = (b == selected);
            const bool on  = bandOn(b);
            const bool hov = (b == hovered);

            g.setColour(p.ink(sel ? 0.06f : hov ? 0.035f : 0.02f));
            g.fillRoundedRectangle(cell, theme::radiusSm);
            g.setColour(sel ? p.accent.withAlpha(0.5f) : p.ink(hov ? 0.16f : 0.09f));
            g.drawRoundedRectangle(cell.reduced(0.5f), theme::radiusSm, 1.0f);

            g.setColour(sel ? p.accent : on ? p.text.withAlpha(0.9f) : p.textDim);
            g.setFont(theme::valueFont(17.0f));
            g.drawText(juce::String(b + 1),
                       cell.withTrimmedBottom(cell.getHeight() * 0.44f).toNearestInt(),
                       juce::Justification::centred);
            g.setColour(sel ? p.accent.withAlpha(0.9f) : hov ? p.textDim : p.textFaint);
            g.setFont(theme::microFont(8.5f));
            g.drawText(typeAbbrev(bandType(b)),
                       cell.withTrimmedTop(cell.getHeight() * 0.54f).toNearestInt(),
                       juce::Justification::centred);

            // enable dot, top-right corner of the compact tile
            const juce::Point<float> dot { cell.getRight() - 9.0f, cell.getY() + 9.0f };
            if (on) theme::drawGlowDot(g, dot, 2.5f, p.accent, sel ? 2.0f : 0.0f);
            else { g.setColour(hov ? p.textDim : p.ink(0.16f));
                   g.drawEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre(dot), 1.2f); }

            if (sel) // accent bar filling the whole bottom edge + downward notch
            {
                const float y = cell.getBottom();
                {
                    // clip to the tile so the bar hugs its rounded corners exactly
                    juce::Graphics::ScopedSaveState save(g);
                    juce::Path tilePath;
                    tilePath.addRoundedRectangle(cell, theme::radiusSm);
                    g.reduceClipRegion(tilePath);
                    g.setColour(p.accent);
                    g.fillRect(cell.getX(), y - 3.0f, cell.getWidth(), 3.0f);
                }
                juce::Path notch;
                const float nx = cell.getCentreX();
                notch.addTriangle(nx - 4.0f, y, nx + 4.0f, y, nx, y + 5.0f);
                g.setColour(p.accent.withAlpha(0.9f));
                g.fillPath(notch);
            }
        }

        // Section labels + the listening glow under DYN when it's working
        g.setColour(p.textFaint);
        g.setFont(theme::microFont(9.0f));
        g.drawText("BAND " + juce::String(selected + 1),
                   ctrlArea.withHeight(14), juce::Justification::topLeft);

        const float v = std::abs(dynMeter[(size_t) selected]);
        if (v > 0.25f && bands[(size_t) selected].dyn != nullptr)
            theme::drawGlowDot(g, { (float) bands[(size_t) selected].dyn->getBounds().getCentreX(),
                                    (float) bands[(size_t) selected].dyn->getBounds().getBottom() + 3.0f },
                               2.5f, p.accent.withAlpha(juce::jlimit(0.35f, 1.0f, v / 6.0f)),
                               v > 3.0f ? 2.5f : 0.0f);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const int h = tabHit(e.getPosition());
        if (h != hovered)
        {
            hovered = h;
            setMouseCursor(h >= 0 ? juce::MouseCursor::PointingHandCursor
                                  : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        if (hovered >= 0) { hovered = -1; setMouseCursor(juce::MouseCursor::NormalCursor); repaint(); }
    }

    std::function<void(int)> onBandSelected; // panel -> analyzer node highlight

    /* Called by the analyzer when a curve node is clicked (no callback back). */
    void selectBandExternal(int b) { applyBand(b); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const int b = tabHit(e.getPosition());
        if (b < 0) return;
        // clicking the top-right enable dot toggles; elsewhere selects (and
        // enables so you always hear the band you opened)
        const auto tile = bandTile(b);
        const juce::Point<float> dot { (float) tile.getRight() - 9.0f, (float) tile.getY() + 9.0f };
        if (e.position.getDistanceFrom(dot) < 11.0f)
        {
            if (auto* on = proc.apvts.getParameter(eqBandID(b, "on")))
            {
                on->beginChangeGesture();
                on->setValueNotifyingHost(bandOn(b) ? 0.0f : 1.0f);
                on->endChangeGesture();
            }
        }
        else if (! bandOn(b))
        {
            if (auto* on = proc.apvts.getParameter(eqBandID(b, "on")))
            {
                on->beginChangeGesture(); on->setValueNotifyingHost(1.0f); on->endChangeGesture();
            }
        }
        applyBand(b);
        if (onBandSelected) onBandSelected(b);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(theme::pad, 6);
        auto strip = r.removeFromTop(38);
        lpBtn.setBounds(strip.removeFromRight(96).reduced(2, 7)); // D-036 switch
        strip.removeFromRight(theme::gap / 2);
        tabStrip = strip;
        r.removeFromTop(theme::gap);

        // reserve the Match EQ strip along the bottom
        matchArea = r.removeFromBottom(72);
        layoutMatch();
        r.removeFromBottom(theme::gap);
        ctrlArea = r;

        auto& s = bands[(size_t) selected];
        // D-065: the "BAND n" caption sat 14px above the knob row, which its
        // own cap height nearly filled — caption and knob labels read as one
        // stacked block. +4px separates them; more than that starts eating
        // the arcs, which is not a trade worth making.
        auto c = ctrlArea.withTrimmedTop(18);
        // FREQ hero (left), then GAIN · Q · DYN, then TYPE selector (right)
        const int heroSide = juce::jmin(c.getHeight(), 128);
        s.freq->setBounds(c.removeFromLeft(heroSide + 30)
                              .withSizeKeepingCentre(heroSide, heroSide));
        c.removeFromLeft(theme::gap);

        auto typeCol = c.removeFromRight(150);
        s.type->setBounds(typeCol.withSizeKeepingCentre(140, 30)
                              .translated(0, -6));
        typeLabelArea = typeCol.withSizeKeepingCentre(140, 12).translated(0, -26);

        const int mSide = juce::jmin(96, c.getHeight());
        const int cellW = c.getWidth() / 3;
        auto place = [&] (Knob& k, int i)
        {
            k.setBounds(juce::Rectangle<int>(c.getX() + i * cellW, c.getY(), cellW, c.getHeight())
                            .withSizeKeepingCentre(mSide, mSide));
        };
        place(*s.gain, 0);
        place(*s.q, 1);
        place(*s.dyn, 2);
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        auto& p = theme::palette();
        g.setColour(p.textFaint);
        g.setFont(theme::microFont(9.0f));
        g.drawText("TYPE", typeLabelArea, juce::Justification::centredLeft);

        // ---- Match EQ strip header + status (D-032) ----
        auto ms = matchArea;
        g.setColour(p.ink(0.07f));
        g.fillRect((float) ms.getX(), (float) ms.getY(), (float) ms.getWidth(), 1.0f);
        ms.removeFromTop(6); // D-065: air under the rule, not text against it
        auto lab = ms.removeFromTop(15);
        g.setColour(p.textFaint);
        g.setFont(theme::microFont(9.0f));
        g.drawText("MATCH EQ", lab.removeFromLeft(90), juce::Justification::centredLeft);

        // learn-readiness dots: reference then source
        const auto ls = proc.matchEQ.learnState();
        auto dotState = [&] (bool ready, bool learning) {
            return learning ? p.accent : ready ? p.text.withAlpha(0.8f) : p.ink(0.16f);
        };
        const float dy = lab.getCentreY();
        float dx = (float) lab.getX() + 6.0f;
        auto legend = [&] (const char* t, juce::Colour c)
        {
            theme::drawGlowDot(g, { dx, dy }, 2.4f, c,
                               c == p.accent ? 2.0f : 0.0f);
            g.setColour(p.textFaint);
            g.setFont(theme::microFont(8.5f));
            g.drawText(t, (int) dx + 7, lab.getY(), 42, lab.getHeight(),
                       juce::Justification::centredLeft);
            dx += 60.0f;
        };
        const int act = proc.matchEQ.activeReference();
        legend("REF A", dotState(proc.matchEQ.referenceReady(0),
                                 act == 0 && ls == dsp::MatchEQ::Learn::reference));
        legend("REF B", dotState(proc.matchEQ.referenceReady(1),
                                 act == 1 && ls == dsp::MatchEQ::Learn::reference));
        legend("SRC",   dotState(proc.matchEQ.sourceReady(), ls == dsp::MatchEQ::Learn::source));
        if (proc.matchEQ.hasMatch())
        {
            g.setColour(p.ink(0.55f));
            g.setFont(theme::microFont(8.5f));
            g.drawText("MATCHED", (int) dx, lab.getY(), 70, lab.getHeight(),
                       juce::Justification::centredLeft);
        }

        // group separator ticks in the button row's gaps (D-056)
        g.setColour(p.ink(0.07f));
        for (float x : matchSepX)
            if (x > 0.0f)
                g.fillRect(x - 0.5f, (float) ms.getCentreY() - 7.0f, 1.0f, 14.0f);
    }

private:
    void setupMatch()
    {
        auto styleBtn = [this] (juce::TextButton& b, std::function<void()> fn)
        {
            b.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            b.onClick = std::move(fn);
            addAndMakeVisible(b);
        };
        // learn toggles — arming one disarms the other
        m.learnRef.setClickingTogglesState(true);
        m.learnSrc.setClickingTogglesState(true);
        styleBtn(m.learnRef, [this]
        {
            if (m.learnRef.getToggleState())
            { m.learnSrc.setToggleState(false, juce::dontSendNotification);
              proc.matchEQ.startLearn(true); }
            else proc.matchEQ.stopLearn();
        });
        styleBtn(m.learnSrc, [this]
        {
            if (m.learnSrc.getToggleState())
            { m.learnRef.setToggleState(false, juce::dontSendNotification);
              proc.matchEQ.startLearn(false); }
            else proc.matchEQ.stopLearn();
        });
        styleBtn(m.match, [this]
        {
            if (! (proc.matchEQ.referenceReady() && proc.matchEQ.sourceReady())) return;
            m.learnRef.setToggleState(false, juce::dontSendNotification);
            m.learnSrc.setToggleState(false, juce::dontSendNotification);
            proc.matchEQ.computeMatch();
            if (auto* on = proc.apvts.getParameter("mch_on"))
            { on->beginChangeGesture(); on->setValueNotifyingHost(1.0f); on->endChangeGesture(); }
            repaint();
        });
        styleBtn(m.clear, [this]
        {
            if (auto* on = proc.apvts.getParameter("mch_on"))
            { on->beginChangeGesture(); on->setValueNotifyingHost(0.0f); on->endChangeGesture(); }
            proc.matchEQ.clearMatch();
            repaint();
        });
        m.onBtn.setClickingTogglesState(true);
        m.onBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        m.onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            proc.apvts, "mch_on", m.onBtn);
        addAndMakeVisible(m.onBtn);

        // D-039: A/B reference slot + where the learn tap listens. Both are
        // choice params flipped by a single button that reads its own state.
        auto cycler = [this] (juce::TextButton& b, const char* id,
                              juce::StringArray names)
        {
            auto refresh = [&b, this, id, names]
            {
                const int v = (int) proc.apvts.getRawParameterValue(id)->load();
                b.setButtonText(names[juce::jlimit(0, names.size() - 1, v)]);
            };
            b.onClick = [this, id, names, refresh]
            {
                if (auto* p = proc.apvts.getParameter(id))
                {
                    const int v = (int) proc.apvts.getRawParameterValue(id)->load();
                    const int nv = (v + 1) % names.size();
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(p->convertTo0to1((float) nv));
                    p->endChangeGesture();
                }
                refresh();
                repaint();
            };
            b.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            addAndMakeVisible(b);
            refresh();
            return std::make_unique<juce::ParameterAttachment>(
                *proc.apvts.getParameter(id),
                [refresh] (float) { refresh(); });
        };
        m.refAtt = cycler(m.refBtn, "mch_ref", { "REF A", "REF B" });
        m.refBtn.setTooltip("Which reference slot LEARN REF fills and MATCH uses");
        m.tapAtt = cycler(m.tapBtn, "mch_tap", { "TAP IN", "TAP OUT" });
        m.tapBtn.setTooltip("Learn from the plugin input, or from the processed output");

        /* D-056 — hierarchy. The config cyclers and CLEAR are secondary
           voice; MATCH is only clickable once both spectra exist (it used
           to swallow the click silently). */
        for (auto* q : { &m.refBtn, &m.tapBtn, &m.clear })
            q->getProperties().set("quiet", true);
        m.match.setTooltip("Compute the correction from the learned reference and source");
        m.match.setEnabled(proc.matchEQ.referenceReady() && proc.matchEQ.sourceReady());
        m.clear.setEnabled(proc.matchEQ.hasMatch());

        m.amount = std::make_unique<Knob>(proc.apvts, "mch_amount", "AMOUNT");
        m.smooth = std::make_unique<Knob>(proc.apvts, "mch_smooth", "SMOOTH");
        m.maxdb  = std::make_unique<Knob>(proc.apvts, "mch_maxdb",  "MAX");
        for (auto* k : { m.amount.get(), m.smooth.get(), m.maxdb.get() })
        { k->macroEngine = &proc.macros; addAndMakeVisible(*k); }
    }

    void layoutMatch()
    {
        auto a = matchArea;
        a.removeFromTop(22);   // rule + air + status line (drawn in paint, D-065)

        auto knobArea = a.removeFromRight(3 * 54);
        const int kw = knobArea.getWidth() / 3;
        auto placeK = [&] (Knob& k) { k.setBounds(knobArea.removeFromLeft(kw).reduced(4, 1)); };
        placeK(*m.amount); placeK(*m.smooth); placeK(*m.maxdb);
        a.removeFromRight(theme::gap);

        /* D-056 — learn | apply | manage. The row reads as three thoughts,
           not seven equal words: the cyclers sit with the learn verbs they
           configure, MATCH+ON apply the result, CLEAR stands alone. The
           gaps take faint separator ticks (header precedent, D-052). */
        auto row = a.withSizeKeepingCentre(a.getWidth(), 26);
        const int gap = 14;
        struct Cell { juce::TextButton* b; float w; };
        const Cell learn[]  = { { &m.refBtn, 0.8f }, { &m.tapBtn, 0.8f },
                                { &m.learnRef, 1.25f }, { &m.learnSrc, 1.25f } };
        const Cell apply[]  = { { &m.match, 1.0f }, { &m.onBtn, 0.8f } };
        const Cell manage[] = { { &m.clear, 0.8f } };
        const float unit = juce::jmin(96.0f,
            (float) (row.getWidth() - 2 * gap) / (0.8f * 4 + 1.25f * 2 + 1.0f + 0.8f));
        size_t sep = 0;
        auto placeGroup = [&] (const Cell* cells, int n, bool last)
        {
            for (int i = 0; i < n; ++i)
                cells[i].b->setBounds(
                    row.removeFromLeft(juce::roundToInt(unit * cells[i].w)).reduced(3, 0));
            if (! last)
            {
                matchSepX[sep++] = (float) row.getX() + gap * 0.5f;
                row.removeFromLeft(gap);
            }
        };
        placeGroup(learn, 4, false);
        placeGroup(apply, 2, false);
        placeGroup(manage, 1, true);
    }


    void applyBand(int b)
    {
        selected = b;
        for (int i = 0; i < 6; ++i)
        {
            const bool v = (i == b);
            auto& s = bands[(size_t) i];
            s.type->setVisible(v);
            s.freq->setVisible(v); s.gain->setVisible(v);
            s.q->setVisible(v);    s.dyn->setVisible(v);
        }
        resized();
        repaint();
    }

    void timerCallback() override
    {
        // Match EQ: accumulate a learn frame (no-op unless a Learn is armed),
        // and keep the strip's status dots live.
        const auto ls = proc.matchEQ.learnState();
        if (ls != dsp::MatchEQ::Learn::idle) { proc.matchEQ.pumpLearn(); repaint(); }
        else if (ls != lastLearn) repaint();
        lastLearn = ls;

        // readiness is state, not a silent click-swallow (D-056)
        const bool ready = proc.matchEQ.referenceReady() && proc.matchEQ.sourceReady();
        if (m.match.isEnabled() != ready) m.match.setEnabled(ready);
        const bool clearable = proc.matchEQ.hasMatch();
        if (m.clear.isEnabled() != clearable) m.clear.setEnabled(clearable);

        const float v = proc.eq.dynMeterDb[(size_t) selected].load(std::memory_order_relaxed);
        if (std::abs(v - dynMeter[(size_t) selected]) > 0.1f)
        {
            dynMeter[(size_t) selected] = v;
            repaint();
        }
    }

    juce::Rectangle<int> tabRect(int b) const
    {
        const int w = tabStrip.getWidth() / 6;
        return { tabStrip.getX() + b * w, tabStrip.getY(), w, tabStrip.getHeight() };
    }
    /* A compact, identical-width tile centred in each cell — keeps the six
       buttons perfectly aligned with the enable dot hugging the content. */
    juce::Rectangle<int> bandTile(int b) const
    {
        auto cell = tabRect(b);
        return cell.withSizeKeepingCentre(juce::jmin(cell.getWidth() - 8, 150),
                                          cell.getHeight() - 2);
    }
    int tabHit(juce::Point<int> pt) const
    {
        for (int b = 0; b < 6; ++b) if (tabRect(b).contains(pt)) return b;
        return -1;
    }
    bool bandOn(int b) const
    { return proc.apvts.getRawParameterValue(eqBandID(b, "on"))->load() > 0.5f; }
    int bandType(int b) const
    { return (int) proc.apvts.getRawParameterValue(eqBandID(b, "type"))->load(); }
    static juce::String typeAbbrev(int t)
    {
        static const char* a[] = { "BELL", "LO SHELF", "HI SHELF", "HPF", "LPF", "NOTCH" };
        return a[juce::jlimit(0, 5, t)];
    }

    struct BandStrip
    {
        std::unique_ptr<juce::ComboBox> type;
        std::unique_ptr<Knob> freq, gain, q, dyn;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAtt;
        std::unique_ptr<juce::ParameterAttachment> typeGate; // GAIN live? (D-064)
        std::unique_ptr<juce::ParameterAttachment> onAtt;
    };
    struct MatchUI
    {
        juce::TextButton learnRef { "LEARN REF" }, learnSrc { "LEARN SRC" },
                         match { "MATCH" }, onBtn { "ON" }, clear { "CLEAR" },
                         refBtn { "REF A" }, tapBtn { "TAP IN" };
        std::unique_ptr<Knob> amount, smooth, maxdb;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
        std::unique_ptr<juce::ParameterAttachment> refAtt, tapAtt;
    };
    VesperProcessor& proc;
    std::array<BandStrip, 6> bands;
    std::array<float, 6> dynMeter {};
    juce::TextButton lpBtn { "LIN PHASE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lpAtt;
    MatchUI m;
    juce::Rectangle<int> tabStrip, ctrlArea, typeLabelArea, matchArea;
    std::array<float, 2> matchSepX {}; // group separator ticks (D-056)
    dsp::MatchEQ::Learn lastLearn = dsp::MatchEQ::Learn::idle;
    int selected = 0, hovered = -1;
};


} // namespace vesper
