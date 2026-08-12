#pragma once
/*  Theme.h — mirror of docs/design/07-TOKENS.md (the machine contract).
    Language tokens first (valid for every Vesper product), then this
    plugin's layout tokens (docs/design/08). A change here without the
    docs — or vice versa — is a defect.

    Monochromatic. Two modes (dark default, light). Live = the brightest /
    strongest ink; there is no colour accent (D-028). One flat field. */

#include <juce_gui_basics/juce_gui_basics.h>

namespace vesper::theme {

// ============================================================ LANGUAGE ====

enum class Mode { dark, light };

// ------------------------------------------------------------ accent ------
/* D-052: the field stays monochrome — that is settled — but the accent is
   the user's choice. Accent only ever marks what is live, so swapping its
   hue changes the instrument's character without touching the grey
   structure underneath. "Mono" keeps the original neutral reading for
   strictly black-and-white. Each option carries a dark-mode and a
   light-mode value, because a hue that sings on near-black washes out on
   paper. */
struct AccentOption { const char* name; juce::uint32 dark, light; };

inline const std::vector<AccentOption>& accentOptions()
{
    static const std::vector<AccentOption> a {
        /* D-063: lighter dark-mode blue (9.5:1 on the sable field, up from
           7.3:1) — brighter reads as more "live" without adding chroma.
           The light-mode cut goes the OTHER way on purpose: against a
           near-white field a lighter accent loses contrast, so it deepens
           to clear AA (4.9:1, was 4.4:1). Same hue family, both legible. */
        { "Blue",    0xff8ab4ff, 0xff2a5fd6 },   // Sable family blue
        { "Mono",    0xfff6f6f6, 0xff0d0f12 },
        { "Aqua",    0xff45e0c8, 0xff0f9b85 },
        { "Green",   0xff5ed17b, 0xff1f9448 },
        { "Amber",   0xffe8a33d, 0xffb5741a },
        { "Violet",  0xff9d7bff, 0xff6338d6 },
        { "Magenta", 0xffe86ab0, 0xffbc3d80 },
        { "Red",     0xffe86a6a, 0xffc23b3b },
    };
    return a;
}

inline int& accentIndex()
{
    static int i = 0;   // Blue, following the mockup
    return i;
}

// ------------------------------------------------------------- color ------
/* Monochrome: one field (night), one ink, and a "live" ink that is the
   strongest reading of that same neutral (near-white in dark, near-black in
   light). error is the single functional exception, kept desaturated and
   reserved for clipping. Everything else is ink at an emphasis. */
struct Palette
{
    juce::Colour night { 0xff0e1013 };  // the field
    juce::Colour inkC  { 0xffe6e8ea };  // information base
    juce::Colour light { 0xfff6f8fb };  // "live" — strongest ink (== accent)
    juce::Colour error { 0xffd98c8c };  // clipping only, muted

    // Emphasis ladder (07): 7 · 16 · 38 · 62 · 100 %
    juce::Colour ink(float emphasis) const { return inkC.withAlpha(emphasis); }

    /* The chosen accent for this mode (D-052). */
    static juce::Colour accentColour(Mode m)
    {
        const auto& opts = accentOptions();
        const auto& o = opts[(size_t) juce::jlimit(0, (int) opts.size() - 1, accentIndex())];
        return juce::Colour(m == Mode::dark ? o.dark : o.light);
    }

    // ---- legacy aliases (component code reads these) ----
    juce::Colour bg, bgTop, bgDeep, panel, panelHover, stroke, ghost,
                 text, textDim, textFaint, accent, accentSoft, warm,
                 danger, shadow;

    Palette() { build(Mode::dark, false); }

    void build(Mode mode, bool highContrast)
    {
        if (mode == Mode::dark)
        {
            // Pure neutral greys (equal R=G=B) — truly monochromatic, deep.
            // (D-049 briefly took this duotone; reverted — the Baby Audio
            //  reference was about form and interaction, not colour.)
            night = juce::Colour(highContrast ? 0xff000000 : 0xff0a0a0a); // near-black charcoal
            inkC  = juce::Colour(highContrast ? 0xffffffff : 0xffe8e8e8); // neutral grey
            light = juce::Colour(highContrast ? 0xffffffff : 0xfff6f6f6); // neutral "live"
            error = juce::Colour(0xffd98a8a);
            shadow = juce::Colours::black.withAlpha(0.55f);
        }
        else // light — soft off-white field, near-black ink (easy on the eyes)
        {
            night = juce::Colour(highContrast ? 0xfffcfcfd : 0xffeceef1);
            inkC  = juce::Colour(highContrast ? 0xff000000 : 0xff1a1d21);
            light = juce::Colour(highContrast ? 0xff000000 : 0xff0d0f12);
            error = juce::Colour(0xffb04d4d);
            shadow = juce::Colours::black.withAlpha(0.14f);
        }

        const float s38 = highContrast ? 0.58f : 0.40f;
        const float s16 = highContrast ? 0.30f : 0.17f;
        const float s07 = highContrast ? 0.14f : 0.08f;

        bg = bgTop = bgDeep = night;          // the field is flat (D-014)
        text       = inkC;
        textDim    = inkC.withAlpha(0.64f);
        textFaint  = inkC.withAlpha(s38);
        stroke     = inkC.withAlpha(s16);
        ghost      = inkC.withAlpha(s07);
        panel      = inkC.withAlpha(mode == Mode::light ? 0.06f : 0.05f);
        panelHover = inkC.withAlpha(mode == Mode::light ? 0.11f : 0.10f);
        // Accent is the one user-chosen hue (D-052); high contrast forces it
        // back to the neutral "live" ink so contrast guarantees still hold.
        accent     = highContrast ? light : accentColour(mode);
        accentSoft = accent.withAlpha(s16);
        warm       = inkC.withAlpha(0.86f);
        danger     = error;
    }
};

inline Palette& palette()
{
    static Palette p;
    return p;
}

inline Mode& mode()
{
    static Mode m = Mode::dark;
    return m;
}

inline bool& highContrast()
{
    static bool hc = false;
    return hc;
}

inline void rebuild() { palette().build(mode(), highContrast()); }
inline void setMode(Mode m) { mode() = m; rebuild(); }
inline void setHighContrast(bool on) { highContrast() = on; rebuild(); }
inline void setAccent(int index)
{
    accentIndex() = juce::jlimit(0, (int) accentOptions().size() - 1, index);
    rebuild();
}

// ---------------------------------------------------------- proportion ----
constexpr int   unit     = 8;                       // u
/* Corner-radius ladder (D-055, audit fix #1). Four steps, nothing between:
     radius    18 — overlay sheets (browser, pads)
     radiusMd  12 — surfaces (workspace, tiles, panes, popups)
     radiusSm   8 — fields (steppers, editors, buttons, preset field)
     radiusXs   5 — chips (badges, row highlights, hover pills)
   The audit found seven ad-hoc values (14/12/10/9/8/7/5) against a declared
   two-step ladder; every rounded corner now draws from these four. */
constexpr float radius   = 18.0f;
constexpr float radiusMd = 12.0f;
constexpr float radiusSm = 8.0f;
constexpr float radiusXs = 5.0f;
constexpr int   gap      = 2 * unit;                // default sibling gap (2u)
constexpr int   pad      = 3 * unit;                // margin (24)
// size ladder (x1.6): 24 · 38 · 60 · 96 · 152
constexpr int sizeS = 60, sizeM = 96, sizeL = 152;

// ------------------------------------------------------------- type -------
/* Two real faces, resolved once against what the machine actually has
   (D-050). Previously every style came from JUCE's default sans with
   `boldened()` — a synthetic, smeared weight — and generous letterspacing
   on already-tiny caps. That combination is the classic generated-UI tell.
   Now: a technical DIN-like face carries labels and numerals (the
   instrument register), a proper UI face carries prose, and weight comes
   from a real semibold cut rather than being faked. */
namespace detail
{
    inline juce::String pickFace(std::initializer_list<const char*> preferences)
    {
        static const auto available = juce::Font::findAllTypefaceNames();
        for (auto* p : preferences)
            if (available.contains(p)) return juce::String(p);
        return juce::Font::getDefaultSansSerifFontName();
    }
}

/* Labels, units and numerals — condensed, technical, even-width figures. */
inline const juce::String& displayFace()
{
    static const juce::String n = detail::pickFace({
        "Bahnschrift", "DIN Alternate", "Roboto Condensed",
        "Avenir Next Condensed", "Segoe UI Semibold", "Helvetica Neue" });
    return n;
}
/* Prose: preset names, menus, editable fields. */
inline const juce::String& bodyFace()
{
    static const juce::String n = detail::pickFace({
        "Segoe UI Variable Display", "Segoe UI", "Inter",
        "Helvetica Neue", "Arial" });
    return n;
}
/* A real semibold cut where one exists; never synthetic. */
inline const juce::String& bodyFaceMedium()
{
    static const juce::String n = detail::pickFace({
        "Segoe UI Semibold", "Segoe UI Variable Display", "Segoe UI",
        "Inter", "Helvetica Neue", "Arial" });
    return n;
}

inline juce::Font font(float height, bool medium = false)
{
    return juce::Font(medium ? bodyFaceMedium() : bodyFace(), height,
                      juce::Font::plain);
}
inline juce::Font labelFont(float h = 11.0f)  // Label: caps, modest tracking
{
    return juce::Font(displayFace(), h, juce::Font::plain)
               .withExtraKerningFactor(0.035f);
}
inline juce::Font microFont(float h = 9.5f)   // Micro: caps, tighter still
{
    return juce::Font(displayFace(), h, juce::Font::plain)
               .withExtraKerningFactor(0.025f);
}
inline juce::Font valueFont(float h)          // numerals: even width
{
    return juce::Font(displayFace(), h, juce::Font::plain);
}

// Value formatting per docs/design/03-TYPOGRAPHY. The unit may transform
// the numeral (Hz -> kHz), so both parts come from one call.
struct ValueText { juce::String numeral, unit; };

inline ValueText formatValue(double v, const juce::String& unitName, bool bipolar = false)
{
    auto num = [&] (juce::String s)
    {
        return bipolar && v > 0.00001 ? "+" + s : s;
    };
    if (unitName == "dB")
        return { num(juce::String(v, 1)), "dB" };
    if (unitName == "Hz")
        return std::abs(v) >= 1000.0
                   ? ValueText { juce::String(v / 1000.0, 2), "kHz" }
                   : ValueText { juce::String(juce::roundToInt(v)), "Hz" };
    if (unitName == "ms")
        return { std::abs(v) < 10.0 ? juce::String(v, 1)
                                    : juce::String(juce::roundToInt(v)), "ms" };
    if (unitName == "s")
        return { juce::String(v, 1), "s" };
    if (unitName == "%")
        return { juce::String(juce::roundToInt(v)), "%" };
    return { num(std::abs(v) < 10.0 ? juce::String(v, 2) : juce::String(v, 1)),
             unitName.trim() };
}

// =============================================================== LAYOUT ===
// docs/design/09-TARGET-UI-V2 bands: header 56 · signal 260 · strip 78 ·
// workspace 210 · performance 76. The meridian is the strip's bottom edge.
constexpr int headerH      = 56;
constexpr int horizonY     = 393;   // meridian = strip bottom (module band 316..394)
constexpr int stationBandH = 78;    // icon-tile strip height
constexpr int utilityH     = 76;    // performance band 604..680
constexpr int containInset = 16;    // Surface inset from canvas edges

// ---------------------------------------------------------- signatures ----
/* The mark: a 4-point star (the evening star) — the system's only ornament. */
inline juce::Path starPath(float cx, float cy, float r)
{
    juce::Path s;
    const float w = r * 0.34f;
    s.startNewSubPath(cx, cy - r);
    s.quadraticTo(cx + w * 0.2f, cy - w, cx + r, cy);
    s.quadraticTo(cx + w * 0.2f, cy + w, cx, cy + r);
    s.quadraticTo(cx - w * 0.2f, cy + w, cx - r, cy);
    s.quadraticTo(cx - w * 0.2f, cy - w, cx, cy - r);
    s.closeSubPath();
    return s;
}

/* The meridian rule: light-16 field under a light-62 core. Quiet, exact. */
inline void drawHorizon(juce::Graphics& g, float y, float x1, float x2)
{
    auto& p = palette();
    g.setColour(p.light.withAlpha(0.16f));
    g.fillRect(x1, y - 1.5f, x2 - x1, 3.0f);
    g.setColour(p.light.withAlpha(0.62f));
    g.fillRect(x1, y - 0.5f, x2 - x1, 1.0f);
}

/* A point of light. Glow is earned: glowScale > 0 only for engaged/active. */
inline void drawGlowDot(juce::Graphics& g, juce::Point<float> c, float r,
                        juce::Colour col, float glowScale = 0.0f)
{
    if (glowScale > 0.01f)
    {
        g.setColour(col.withAlpha(0.10f));
        g.fillEllipse(juce::Rectangle<float>(r * glowScale * 2, r * glowScale * 2)
                          .withCentre(c));
    }
    g.setColour(col);
    g.fillEllipse(juce::Rectangle<float>(r * 2, r * 2).withCentre(c));
}

/* Surface: the contained panel of UI v2 (D-022) — soft fill, hairline
   border, rounded. Workspace/performance/preset field all share it. */
inline void drawSurface(juce::Graphics& g, juce::Rectangle<float> r,
                        float rad = radiusMd, bool emphasized = false)
{
    auto& p = palette();
    g.setColour(p.ink(0.045f));
    g.fillRoundedRectangle(r, rad);
    g.setColour(p.ink(emphasized ? 0.16f : 0.10f));
    g.drawRoundedRectangle(r.reduced(0.5f), rad, 1.0f);
}

/* Overlay sheet: flat night surface bounded by one rule. */
inline void drawGlassPanel(juce::Graphics& g, juce::Rectangle<float> r,
                           bool hovered = false, float rad = radius)
{
    auto& p = palette();
    g.setColour(p.night);
    g.fillRoundedRectangle(r, rad);
    g.setColour(p.ink(hovered ? 0.09f : 0.045f));
    g.fillRoundedRectangle(r, rad);
    g.setColour(p.ink(hovered ? 0.16f : 0.07f));
    g.drawRoundedRectangle(r.reduced(0.5f), rad, 1.0f);
}

} // namespace vesper::theme
