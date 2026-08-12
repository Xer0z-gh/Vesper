# SABLE AUDIO — the brand system

Adopted 2026-07-28 (D-058). Replaces the Tannin Audio provisional brand —
the original BRANDING.md itself flagged the company name for revisit, and
its mint/amber palette died with D-028. This document is canonical for the
**whole product line**, not just Vesper. Every plugin ships on this system.

======================================================================
THE NAME
======================================================================

**SABLE AUDIO.**

Why it is the right name and not just a nicer one:

- **Sable is the heraldic word for black.** The design language is a
  monochrome field with derived greys — the company is literally named
  for the colour system it builds on. (The light theme has a heraldic
  name too: argent. The palette *is* sable and argent, plus one live
  colour.)
- **A sable is the finest brush an ink artist can hold** — an instrument,
  not a decoration. "Instrument-grade tools for people who work in
  sound" is the entire pitch, and the name carries it without a tagline.
- The type hierarchy already speaks in an **ink ladder**; sable is ink.
- Two syllables, spells itself, reads premium (FabFilter / Teenage
  Engineering register, not edgy-startup register).
- **Diligence (2026-07-28):** no plugin company, DSP shop, or music-tech
  brand named "Sable Audio" found; collisions are out-of-category (a
  Berlin recording artist "Sable // Music", a Bon Iver EP, a video game).
  ⚠ Before spending money on the brand: proper trademark search (Nice
  class 9 software) and register the domain — sableaudio.com appeared
  unclaimed by any audio company at adoption time.

Voice (kept from the original doc — it was right): understated
confidence. Say what it does; never "revolutionary". Sound-first copy.
Docs speak to producers as peers.

======================================================================
THE FAMILY — how products are named
======================================================================

Two tiers, two naming registers:

- **Flagship instruments** — evocative single words from the
  evening-sky register: **VESPER** (the evening star — shipping),
  later **AUBADE** (the morning counterpart — mastering chain),
  **NOCTURNE** (creative reverb). These are destinations; their names
  are allowed to be poetry.
- **Utility line** — brand + function: **Sable Saturate**, **Sable
  Match**, **Sable Motion**. People searching a marketplace type "match
  eq", not a poem. Utility names are for being found; the descriptor
  goes in the name, the poetry stays in the flagship tier.

Never: version-number names, "Pro/Ultimate" suffixes, initialisms.

======================================================================
THE NICHE MAP — every product aims at one buyer
======================================================================

A plugin that appeals to everyone is bought by no one. Each product
names its buyer before a line of code moves:

| Product | Niche (the one buyer) | Price | Role |
|---|---|---|---|
| **Vesper** | electronic producers / sound designers who want one deep creative environment instead of a folder of single-trick FX | TBD (flagship) | the reputation |
| **Sable Saturate** | bedroom & lo-fi producers hunting free character | FREE | the funnel — Gumroad $0+ captures emails |
| **Sable Match** | budget mixing engineers; match EQ is gated behind $219 suites (Pro-Q, Ozone) and nothing serves it at $29 | $29 (intro $19) | the wedge |
| **Sable Motion** | groove-driven electronic producers (rhythmic modulation) | ~$29 | month 2–3 |
| Aubade | mastering engineers | later | flagship 2 |
| Nocturne | ambient / cinematic composers | later | flagship 3 |

Positioning rule: marketing copy names the niche in the first sentence
("Match EQ for engineers who don't own Ozone"), never "for everyone who
loves great sound".

======================================================================
THE FIELD — one colour system for every product
======================================================================

The Vesper theme system (src/ui/Theme.h) IS the brand palette. Every
Sable plugin ships it verbatim:

- **Field**: sable `#0a0a0a` (dark) · argent `#eceef1` (light). No
  second material. Depth from hierarchy, never shadows/gradients.
- **Ink ladder**: all greys are opacities of one ink — 7 / 16 / 38 /
  62 / 100 %. Hierarchy is weight and opacity before size, size before
  colour.
- **Accent means live** — selection, live data, the thing under your
  hand. Never decoration. Default accent: **#8ab4ff dark / #2a5fd6
  light** (the Sable family blue, D-063). The two modes move in opposite
  directions on purpose: on the sable field a *lighter* blue reads as
  more alive (9.5:1); on the argent field it must *deepen* to stay
  legible (4.9:1, clears AA). User may choose from
  the named accent set (D-052: Blue · Mono · Aqua · Green · Amber ·
  Violet · Magenta · Red); high contrast forces neutral.
- Gradients only when they encode data (02-COLOR exception, D-055).

======================================================================
THE TYPE, THE GEOMETRY, THE MOTION
======================================================================

- **Display face** (labels, numerals, units): Bahnschrift → DIN
  Alternate → Roboto Condensed. **Body face**: Segoe UI Variable.
  Real weights only — synthetic bold is banned (D-050). Tracking
  3.5 % / 2.5 %. Tabular numerals; unit rendered small and faint
  beside the numeral ("6.0 dB" as two voices).
- **Radius ladder**: 18 overlay · 12 surface · 8 field · 5 chip
  (D-055). Nothing off-ladder.
- **Spacing**: 4px grid.
- **Motion**: ≤240 ms, explains change, respects reduced-motion.
- **Icons**: drawn Paths only — icon fonts banned (D-011). Glyph+word
  pairs use IconWordButton (D-057) so icon and word share one ink.

======================================================================
THE SHELL — what makes a Sable plugin recognisable
======================================================================

Same anatomy in every product, so owning two Sable plugins feels like
owning one instrument line:

- **Header**: mark + wordmark left · centred preset navigator
  (‹ name › over category, save field) · quiet-word actions grouped by
  faint separators · power pill right. ··· menu holds Interface
  (theme, accent, scale) and Session sections.
- **Wordmark**: product name in the display face, wide tracking;
  "SABLE AUDIO" in 9px caps beneath it only on the About surface —
  the product speaks, the company whispers.
- **Controls**: one knob language (arc + centred value), steppers for
  short choice lists, the stepper-cased combo for long ones (D-056),
  two-weight button hierarchy with gap-grouping.
- **Browser**: the Waves-style two-pane library (D-049).

Implementation vehicle: when plugin #2 starts, `src/ui/` (Theme, LNF,
Knob, Stepper, IconWordButton, HeaderBar skeleton, PresetBrowser)
extracts into a shared **sable-ui** module (Flagship Workstream 3).
Until then Vesper is the reference implementation and this file is the
contract.

======================================================================
STANDING ORDER
======================================================================

- New product? It enters the niche map FIRST — name the one buyer.
- Any UI divergence between products is a bug in one of them.
- The identity constants (company name, bundle id prefix
  com.sableaudio.*, manufacturer code `Sabl`) live in each product's
  CMakeLists and must match this document.
- Trademark + domain must be settled before the first paid launch.
