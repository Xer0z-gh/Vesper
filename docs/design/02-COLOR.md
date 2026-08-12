# 02 · Color

> **Superseded by D-028 (2026-07-18): Vesper is now MONOCHROME with two
> modes (dark default + light).** The mint `light` accent is retired. The
> palette is one neutral: a flat field (`night`), an ink base, and a "live"
> ink = the strongest reading of that neutral (near-white in dark,
> near-black in light) — used everywhere this doc says "accent". `error`
> remains the single functional exception (desaturated, clipping only).
> "Light means live" still holds: live is the brightest/strongest ink, not
> a hue. Everything below still applies as *structure* (emphasis ladder,
> two-data-layers, glow-is-earned); only the hue is gone. See Theme.h
> `Palette::build(mode, highContrast)`.

---


Four semantic colors. Everything else is a derived opacity. There is no
fifth color anywhere in the application.

| Name | Value | Meaning |
|---|---|---|
| **night** | `#0A0C0F` | the instrument's face — one flat field |
| **ink** | `#F2F4F6` | information at rest |
| **light** | `#86EFD5` | *the* accent: live values, active states, signal |
| **error** | `#FF6B6B` | clipping and destructive confirmation only |

## Derivation, not invention

All greys are **ink at fixed opacities** — never separate hex greys:

| Step | Opacity | Use |
|---|---|---|
| ink-100 | 100 % | primary numerals, selected labels |
| ink-62 | 62 % | secondary text, labels, units at focus |
| ink-38 | 38 % | resting labels, units, inactive stations |
| ink-16 | 16 % | arc tracks, graduations, switch rails |
| ink-7 | 7 % | structural rules, calibration rules |

`light` has exactly three emphases: **light-100** (value arcs, points,
station dots), **light-62** (secondary data layer, e.g. spectrum crest),
**light-16** (data fills, e.g. spectrum area). No other alphas.

## Laws

1. **The field is flat.** One background value; no gradients anywhere
   (D-014). Depth comes from structure and light, not shading.
2. **Light = live.** Accent appears only on: a value arc, a value point, an
   enabled station, an active/hover state, the signal itself (spectrum),
   and focused text. Chrome, labels, structure are never accent.
3. **Two data layers max** in the SIGNAL field, distinguished by emphasis
   (spectrum = light-62/16, overlay curve = light-100), never by hue.
4. **Glow is earned.** A soft halo (layered strokes) exists only on the
   *engaged* element — the arc being dragged, the selected station. At rest
   everything is crisp, flat, exact.
5. **Error is speech, not decor.** error appears when signal exceeds 0 dBFS
   or an action destroys data. If error is visible, something is wrong.

## High contrast (accessibility)

High-contrast mode raises the derivation table (38→56, 16→28, 7→14) and
ink to pure white; the four semantic colors are unchanged. Because the
system is derived, every component inherits the mode with zero component
code (see 05-INTERACTION for contrast targets).

## Exception: gradients that encode data (D-055)

The gradient ban is a ban on decoration. A gradient whose alpha or hue
carries information — the spectrum wash fading with energy toward the
floor (D-052) — is a data encoding, not chrome, and is permitted. The
test: delete it and ask what information was lost. If the answer is
"none", it was decoration and stays banned.
