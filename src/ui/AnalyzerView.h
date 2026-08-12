#pragma once
/*  AnalyzerView — the sky. Full-bleed spectrum rising off the horizon like
    airglow: a soft wide bloom under a crisp luminous crest. No box, no
    chrome; frequency marks are quiet ticks resting on the horizon.
    60 fps UI-side FFT with per-bin attack/decay ballistics; the audio
    thread only ever fills a lock-free FIFO. */

#include "../core/Params.h"
#include "../dsp/Analysis.h"
#include "Theme.h"

namespace vesper {

class AnalyzerView : public juce::Component, private juce::Timer
{
public:
    explicit AnalyzerView(dsp::FFTCapture& captureIn)
        : capture(captureIn), fft(dsp::FFTCapture::fftOrder),
          window((size_t) dsp::FFTCapture::fftSize,
                 juce::dsp::WindowingFunction<float>::hann)
    {
        smoothed.fill(-100.0f);
        setInterceptsMouseClicks(false, false);
        startTimerHz(60);
    }

    void setSampleRate(double fs) noexcept { sampleRate = fs; }

    /* Optional overlay: returns dB at a given Hz (e.g. EQ response). */
    std::function<float(double hz)> overlayResponse;

    /* Optional Match EQ correction overlay: dB at Hz, or nullptr when no
       match exists. Drawn as a distinct dashed curve (D-032). */
    std::function<float(double hz)> matchResponse;

    /* Accent when the EQ is the active module; ghost otherwise (09 §2). */
    bool overlayEmphasized = true;

    // ---- on-curve EQ editing (M4) ---------------------------------------
    /* Enabled when the EQ module is selected. The analyzer becomes an EQ
       editor: each band is a draggable node on the curve. */
    void setEQEditable(juce::AudioProcessorValueTreeState* apvts, bool on)
    {
        eqApvts = apvts;
        eqEditable = on;
        setInterceptsMouseClicks(on, on);
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
    void setSelectedBand(int b) { if (b != selBand) { selBand = b; repaint(); } }
    std::function<void(int)> onBandSelected; // node clicked -> tell the panel

    void paint(juce::Graphics& g) override
    {
        auto& p = theme::palette();
        const auto r = graphBounds();          // the plotted field (D-064)
        const auto full = getLocalBounds().toFloat(); // gutters live out here

        /* Calibration (09 §2): ONE dB scale, in the left gutter — never
           mirrored (D-063), never on top of the curve (D-064). It measures
           whichever layer is prominent: the EQ's own gain axis while you
           are editing bands, the spectrum's level otherwise. Two axes were
           always in play; only one was ever labelled. */
        g.setFont(theme::microFont(9.0f));
        const bool eqAxis = overlayEmphasized && overlayResponse != nullptr;
        const float eqTicks[]   = { 12.0f, 0.0f, -12.0f };
        const float specTicks[] = { 0.0f, -12.0f, -24.0f, -48.0f, -72.0f };
        const int   tickCount   = eqAxis ? 3 : 5;
        for (int i = 0; i < tickCount; ++i)
        {
            const float db = eqAxis ? eqTicks[i] : specTicks[i];
            const float y  = eqAxis ? yForEqDb(db, r) : yForDb(db, r);
            const bool  zeroLine = eqAxis && std::abs(db) < 0.5f;

            // the spectrum's 0 dB rule is implied by the top edge; the EQ's
            // 0 dB line is the reference the whole curve is read against.
            if (eqAxis || db < -0.5f)
            {
                g.setColour(p.ink(zeroLine ? 0.13f : 0.07f));
                g.fillRect(r.getX(), y, r.getWidth(), 1.0f);
            }
            g.setColour(p.ink(0.38f).withAlpha(zeroLine ? 0.95f : 0.7f));
            g.drawText(eqAxis && db > 0.5f ? "+" + juce::String((int) db)
                                           : juce::String((int) db),
                       (int) full.getX(), (int) y - 5, (int) gutterL - 6, 10,
                       juce::Justification::centredRight);
        }

        // Frequency ruler along the bottom edge: 20 ... 20k (09 §2)
        static const double freqs[]  = { 20, 50, 100, 200, 500, 1000, 2000,
                                         5000, 10000, 20000 };
        static const char*  fLabels[] = { "20", "50", "100", "200", "500", "1k",
                                          "2k", "5k", "10k", "20k" };
        for (int i = 0; i < 10; ++i)
        {
            const float x = xForFreq(freqs[i], r);
            if (juce::exactlyEqual(freqs[i], 100.0) || juce::exactlyEqual(freqs[i], 1000.0)
                || juce::exactlyEqual(freqs[i], 10000.0))
            {
                g.setColour(p.ink(0.07f));
                g.fillRect(x, r.getY() + 4.0f, 1.0f, r.getHeight() - 8.0f);
            }
            // D-064: tick sits ON the graph's baseline, numeral below it in
            // the bottom gutter — neither one crowds the plotted field.
            g.setColour(p.ink(0.16f));
            g.fillRect(juce::jlimit(r.getX(), r.getRight() - 1.0f, x),
                       r.getBottom() - 4.0f, 1.0f, 4.0f);
            g.setColour(p.ink(0.38f).withAlpha(0.75f));
            g.drawText(fLabels[i],
                       (int) juce::jlimit(full.getX(), full.getRight() - 30.0f, x - 15.0f),
                       (int) r.getBottom() + 4, 30, 11, juce::Justification::centred);
        }

        // Spectrum is NEUTRAL context (accent belongs to what the user acts
        // on). Silence draws nothing — calibrated stillness.
        if (std::any_of(smoothed.begin(), smoothed.end(),
                        [] (float db) { return db > -92.0f; }))
        {
            juce::Path crest = buildSpectrumPath(r);
            juce::Path fill(crest);
            fill.lineTo(r.getRight(), r.getBottom());
            fill.lineTo(r.getX(), r.getBottom());
            fill.closeSubPath();
            /* D-052: the body of the spectrum washes the accent, fading to
               nothing at the floor, with a bright crest on top — the airglow
               the mockup shows. Still context, not a control: the wash is
               low-alpha so it never competes with the EQ curve above it. */
            g.setGradientFill(juce::ColourGradient(
                p.accent.withAlpha(0.30f), r.getCentreX(), r.getY(),
                p.accent.withAlpha(0.02f), r.getCentreX(), r.getBottom(), false));
            g.fillPath(fill);
            g.setColour(p.accent.withAlpha(0.75f));
            g.strokePath(crest, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved));
        }

        // EQ response — THE accent layer when the EQ is active; ghost when
        // another module is selected and bands are shaping.
        if (overlayResponse)
        {
            juce::Path oc;
            float span = 0.0f;
            const int steps = juce::jmax(2, getWidth() / 3);
            for (int i = 0; i <= steps; ++i)
            {
                const float x = r.getX() + r.getWidth() * i / (float) steps;
                const double hz = freqForX(x, r);
                const float db = overlayResponse(hz);
                span = juce::jmax(span, std::abs(db));
                // 0 dB rides the middle of the Field; +/-28 dB spans it
                const float y = yForEqDb(db, r);
                if (i == 0) oc.startNewSubPath(x, y);
                else oc.lineTo(x, y);
            }
            if (overlayEmphasized || span > 0.2f)
            {
                g.setColour(overlayEmphasized ? p.light : p.ink(0.38f));
                g.strokePath(oc, juce::PathStrokeType(overlayEmphasized ? 1.6f : 1.3f,
                                                      juce::PathStrokeType::curved));
            }
        }

        // Match EQ correction — a distinct dashed curve showing the linear-phase
        // correction being applied (D-032). Drawn only when a match exists.
        if (matchResponse)
        {
            juce::Path mc;
            float span = 0.0f;
            const int steps = juce::jmax(2, getWidth() / 3);
            for (int i = 0; i <= steps; ++i)
            {
                const float x = r.getX() + r.getWidth() * i / (float) steps;
                const float db = matchResponse(freqForX(x, r));
                span = juce::jmax(span, std::abs(db));
                const float y = yForEqDb(db, r);
                if (i == 0) mc.startNewSubPath(x, y); else mc.lineTo(x, y);
            }
            if (span > 0.05f)
            {
                juce::Path dashed;
                const float dashes[] = { 5.0f, 4.0f };
                juce::PathStrokeType(1.4f, juce::PathStrokeType::curved)
                    .createDashedStroke(dashed, mc, dashes, 2);
                g.setColour(p.ink(0.55f));
                g.fillPath(dashed);
            }
        }

        // EQ node handles (M4): one per enabled band, draggable on the curve
        if (eqEditable && eqApvts != nullptr)
        {
            for (int b = 0; b < 6; ++b)
            {
                if (bandVal(b, "on") <= 0.5f) continue;
                const auto c = nodePos(b, r);
                const bool sel = (b == selBand);
                const bool hot = (b == hoverBand);
                const float rad = sel ? 10.0f : hot ? 9.0f : 8.0f;

                // a vertical guide line from the node down to the axis (sel/hot)
                if (sel || hot)
                {
                    g.setColour(p.ink(0.10f));
                    g.fillRect(c.x - 0.5f, r.getY() + 6.0f, 1.0f, r.getHeight() - 12.0f);
                }
                // filled disc + ring + centred number — the FabFilter-class node
                g.setColour((sel ? p.accent : p.ink(hot ? 0.34f : 0.22f)));
                g.fillEllipse(juce::Rectangle<float>(rad * 2, rad * 2).withCentre(c));
                g.setColour(sel ? p.accent : hot ? p.text : p.ink(0.72f));
                g.drawEllipse(juce::Rectangle<float>(rad * 2, rad * 2).withCentre(c), 1.4f);
                g.setColour(sel ? p.night : hot ? p.night.withAlpha(0.9f) : p.text.withAlpha(0.9f));
                g.setFont(theme::valueFont(10.0f));
                g.drawText(juce::String(b + 1),
                           juce::Rectangle<float>(rad * 2, rad * 2).withCentre(c).toNearestInt(),
                           juce::Justification::centred);
                // No numeric readout on the node — the curve IS the value
                // (2026-07-19); the band-focus panel shows precise
                // numbers.
            }
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (! eqEditable) return;
        const int b = bandAt(e.position);
        if (b != hoverBand)
        {
            hoverBand = b;
            setMouseCursor(b >= 0 ? juce::MouseCursor::DraggingHandCursor
                                  : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        if (hoverBand >= 0) { hoverBand = -1; repaint(); }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (! eqEditable || eqApvts == nullptr) return;
        if (e.mods.isPopupMenu()) { showNodeMenu(bandAt(e.position)); return; }
        int b = bandAt(e.position);
        if (b < 0) // click on empty curve area near a frequency -> enable nearest off band there
        {
            b = nearestBand((double) freqForX(e.position.x, graphBounds()));
            if (b >= 0) gestureSet(b, "on", 1.0f);
        }
        if (b < 0) return;
        dragBand = b;
        selBand = b;
        if (onBandSelected) onBandSelected(b);
        beginGesture(b, "freq"); beginGesture(b, "gain");
        applyDrag(e.position);
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragBand >= 0) applyDrag(e.position);
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragBand >= 0) { endGesture(dragBand, "freq"); endGesture(dragBand, "gain"); }
        dragBand = -1;
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (! eqEditable || eqApvts == nullptr) return;
        const int b = bandAt(e.position);
        if (b >= 0) gestureSet(b, "on", bandVal(b, "on") > 0.5f ? 0.0f : 1.0f);
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (! eqEditable || eqApvts == nullptr) return;
        const int b = bandAt(e.position);
        if (b < 0) return;
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(eqApvts->getParameter(eqBandID(b, "q"))))
        {
            const float nv = juce::jlimit(0.0f, 1.0f, p->getValue() + w.deltaY * 0.6f);
            p->setValueNotifyingHost(nv);
            repaint();
        }
    }

private:
    static constexpr float dbRange = 96.0f; // -96..0 dB display (09 §2)

    void timerCallback() override
    {
        if (! capture.read(timeData.data())) { repaint(); return; }
        window.multiplyWithWindowingTable(timeData.data(), (size_t) dsp::FFTCapture::fftSize);
        std::copy(timeData.begin(), timeData.end(), freqData.begin());
        std::fill(freqData.begin() + dsp::FFTCapture::fftSize, freqData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform(freqData.data());

        const int bins = dsp::FFTCapture::fftSize / 2;
        for (int i = 0; i < bins; ++i)
        {
            const float mag = freqData[(size_t) i] / (float) (dsp::FFTCapture::fftSize / 4);
            const float db  = juce::Decibels::gainToDecibels(mag, -100.0f);
            float& s = smoothed[(size_t) i];
            s = db > s ? db * 0.55f + s * 0.45f    // fast attack
                       : db * 0.06f + s * 0.94f;   // slow decay
        }
        repaint();
    }

    juce::Path buildSpectrumPath(juce::Rectangle<float> r) const
    {
        juce::Path path;
        const int bins = dsp::FFTCapture::fftSize / 2;
        bool started = false;
        for (int i = 1; i < bins; ++i)
        {
            const double hz = sampleRate * i / dsp::FFTCapture::fftSize;
            if (hz < 18.0 || hz > 22000.0) continue;
            const float x = xForFreq(hz, r);
            const float y = yForDb(smoothed[(size_t) i], r);
            if (! started) { path.startNewSubPath(x, y); started = true; }
            else path.lineTo(x, y);
        }
        if (! started) path.startNewSubPath(r.getX(), r.getBottom());
        return path;
    }

    // ---- EQ node helpers (M4) ----
    /* Right-click a node: change its filter type, set DYN, or remove it
       (D-041) — the graph becomes a complete band editor, no round trip to
       the panel for the things you reach for mid-gesture. */
    void showNodeMenu(int b)
    {
        if (b < 0 || eqApvts == nullptr) return;
        selBand = b;
        if (onBandSelected) onBandSelected(b);
        repaint();

        juce::PopupMenu m;
        m.addSectionHeader("Band " + juce::String(b + 1));

        juce::PopupMenu types;
        const auto names = eqBandTypeNames();
        const int curType = (int) bandVal(b, "type");
        for (int t = 0; t < names.size(); ++t)
            types.addItem(names[t], true, t == curType,
                          [this, b, t] { gestureSet(b, "type", (float) t); });
        m.addSubMenu("Type", types);

        juce::PopupMenu dyn; // the DYN amounts worth reaching for on a node
        static const std::pair<const char*, float> steps[] = {
            { "Off",  0.0f },  { "-6 dB (tame)", -6.0f }, { "-12 dB (strong)", -12.0f },
            { "+6 dB (open)", 6.0f }, { "+12 dB (lift)", 12.0f } };
        const float curDyn = bandVal(b, "dyn");
        for (const auto& s : steps)
            dyn.addItem(s.first, true, std::abs(curDyn - s.second) < 0.05f,
                        [this, b, v = s.second] { gestureSet(b, "dyn", v); });
        m.addSubMenu("Dynamic", dyn);

        m.addSeparator();
        m.addItem("Flatten gain", std::abs(bandVal(b, "gain")) > 0.01f,
                  false, [this, b] { gestureSet(b, "gain", 0.0f); });
        m.addItem("Remove band", true, false, [this, b]
        {
            gestureSet(b, "on", 0.0f);
            gestureSet(b, "gain", 0.0f);
            gestureSet(b, "dyn", 0.0f);
        });
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
    }

    float bandVal(int b, const char* leaf) const
    {
        return eqApvts->getRawParameterValue(eqBandID(b, leaf))->load();
    }
    /* D-064: HP / LP / NOTCH have no gain in the DSP (see eqTypeHasGain),
       so their node has no gain axis. It used to be pinned to the 0 dB
       centre line, leaving the dot floating in space while the curve dived
       past it somewhere else — "the points don't make the wave". Those
       nodes now RIDE the response curve at their own frequency, so a node
       always sits on the shape it makes. */
    juce::Point<float> nodePos(int b, juce::Rectangle<float> r) const
    {
        const float x = xForFreq(juce::jlimit(20.0f, 20000.0f, bandVal(b, "freq")), r);
        const int type = (int) bandVal(b, "type");
        const float db = eqTypeHasGain(type)
                             ? bandVal(b, "gain")
                             : (overlayResponse ? overlayResponse(freqForX(x, r)) : 0.0f);
        const float y = yForEqDb(db, r);
        return { x, juce::jlimit(r.getY() + 8.0f, r.getBottom() - 8.0f, y) };
    }
    int bandAt(juce::Point<float> pt) const
    {
        if (! eqEditable || eqApvts == nullptr) return -1;
        const auto r = graphBounds();
        int best = -1; float bestD = 20.0f; // generous grab radius
        for (int b = 0; b < 6; ++b)
        {
            if (bandVal(b, "on") <= 0.5f) continue;
            // prefer the already-selected band when nodes overlap
            const float bias = (b == selBand) ? 4.0f : 0.0f;
            const float d = pt.getDistanceFrom(nodePos(b, r)) - bias;
            if (d < bestD) { bestD = d; best = b; }
        }
        return best;
    }
    int nearestBand(double hz) const
    {
        int best = -1; double bestD = 1.0e9;
        for (int b = 0; b < 6; ++b)
        {
            if (bandVal(b, "on") > 0.5f) continue; // only offer disabled bands
            const double d = std::abs(std::log(bandVal(b, "freq") / hz));
            if (d < bestD) { bestD = d; best = b; }
        }
        return best;
    }
    void applyDrag(juce::Point<float> pt)
    {
        const auto r = graphBounds();
        const double hz = freqForX(pt.x, r);
        setRanged(dragBand, "freq", (float) hz);
        const int type = (int) bandVal(dragBand, "type");
        /* D-064: NOTCH was missing from this guard — dragging a notch node
           vertically wrote a `gain` the notch design ignores, so the dot
           moved and a parameter changed while the curve never budged. */
        if (eqTypeHasGain(type))
        {
            const float gain = juce::jlimit(-18.0f, 18.0f, // matches the band range
                                            eqDbForY(pt.y, r));
            setRanged(dragBand, "gain", gain);
        }
        repaint();
    }
    void setRanged(int b, const char* leaf, float plainValue)
    {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(
                eqApvts->getParameter(eqBandID(b, leaf))))
            p->setValueNotifyingHost(p->convertTo0to1(plainValue));
    }
    void gestureSet(int b, const char* leaf, float plainValue)
    {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(
                eqApvts->getParameter(eqBandID(b, leaf))))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(plainValue));
            p->endChangeGesture();
        }
    }
    void beginGesture(int b, const char* leaf)
    { if (auto* p = eqApvts->getParameter(eqBandID(b, leaf))) p->beginChangeGesture(); }
    void endGesture(int b, const char* leaf)
    { if (auto* p = eqApvts->getParameter(eqBandID(b, leaf))) p->endChangeGesture(); }

    /* The plotted field, inset from the component (D-064). The graph used
       to bleed to all four edges, so the dB numerals sat ON the curve, the
       ruler labels touched the bottom, and nodes at 20 Hz / 20 kHz were
       half outside the window — that crowding is what read as "clustered".
       The scale numerals now live in gutters outside this rect.
       EVERYTHING — painting, hit-testing, dragging — goes through here, or
       the nodes drift away from the cursor. */
    juce::Rectangle<float> graphBounds() const
    {
        auto b = getLocalBounds().toFloat();
        b.removeFromLeft(gutterL);
        b.removeFromRight(gutterR);
        b.removeFromTop(gutterT);
        b.removeFromBottom(gutterB);
        return b;
    }

    float xForFreq(double hz, juce::Rectangle<float> r) const
    {
        const double t = std::log(hz / 20.0) / std::log(20000.0 / 20.0);
        return r.getX() + (float) juce::jlimit(0.0, 1.0, t) * r.getWidth();
    }
    double freqForX(float x, juce::Rectangle<float> r) const
    {
        const double t = (x - r.getX()) / r.getWidth();
        return 20.0 * std::pow(20000.0 / 20.0, juce::jlimit(0.0, 1.0, t));
    }
    float yForDb(float db, juce::Rectangle<float> r) const
    {
        return r.getY() + (juce::jlimit(-dbRange, 0.0f, db) / -dbRange) * r.getHeight();
    }

    /* The EQ curve rides a DIFFERENT axis from the spectrum: symmetric
       about the centre line, +/-eqSpan/2 across the field. This used to be
       an inline `centreY - db * h/56` copied into four places while the
       only LABELLED scale was the spectrum's 0..-96 — so a +6 dB bell drew
       itself level with the "-48" gridline and the numbers described
       nothing the user was editing (D-064). One mapping now, and the
       labels switch to this axis whenever the EQ curve is the prominent
       layer. */
    static constexpr float eqSpan = 56.0f;   // +/-28 dB across the field
    float yForEqDb(float db, juce::Rectangle<float> r) const
    {
        return r.getCentreY() - db * (r.getHeight() / eqSpan);
    }
    float eqDbForY(float y, juce::Rectangle<float> r) const
    {
        return (r.getCentreY() - y) * (eqSpan / r.getHeight());
    }

    // gutters for the scale numerals — the graph never draws into these
    static constexpr float gutterL = 34.0f, gutterR = 14.0f,
                           gutterT = 10.0f, gutterB = 20.0f;

    dsp::FFTCapture& capture;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, dsp::FFTCapture::fftSize> timeData {};
    std::array<float, dsp::FFTCapture::fftSize * 2> freqData {};
    std::array<float, dsp::FFTCapture::fftSize / 2> smoothed {};
    double sampleRate = 48000.0;

    juce::AudioProcessorValueTreeState* eqApvts = nullptr;
    bool eqEditable = false;
    int  selBand = 0, hoverBand = -1, dragBand = -1;
};

} // namespace vesper
