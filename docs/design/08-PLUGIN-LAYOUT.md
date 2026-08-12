# 08 · The plugin layout — one implementation of the language

Everything in this file is **layout**, free to be redesigned without
touching the language (00–07). The meridian is *this product's* defining
composition — iconic here, but explicitly **not the brand** (D-017): a
future Vesper may have no meridian and must still be recognizable.

## Reference scale

Canvas 1080 × 680 · u = 8 px · window scale 0.75–2.0.

## Architecture — five sections, three structural rules

| Section | Band (y) | Contents |
|---|---|---|
| **HEADER** | 0–52 | mark · preset Selector · history · state · quality |
| — Rule 52 — | | |
| **SIGNAL** | 52–384 | the Field: spectrum, overlay curve, dB calibration (−12/−24/−48), frequency ticks 100/1k/10k |
| **MERIDIAN** | 384–432 | full-width Rule at y 408 · 13 graduations · Markers (stations) with slot numerals 01–13 · labels |
| **WORKSPACE** | 432–612 | selected module's controls, tiered |
| — Rule 612 — | | |
| **PERFORMANCE** | 612–680 | I/O arcs · M1–M4 · Morph · Telemetry right-aligned |

Margins 3u (24). SIGNAL and the meridian bleed full width (data touches
edges; chrome does not). The **pause** (Space primitive) of the default
view is the upper SIGNAL field — calibrated silence until signal arrives.

## Workspace composition

| Zone | Placement | Contents |
|---|---|---|
| Hero | left, 280 wide | one Arc L (ladder 152) — the module's musical identity |
| Supporting | upper band right of hero | 1–4 Arc M (96), left-aligned |
| Advanced | lower band right of hero, **right-aligned** | Arc S (60), Selectors, Switches |

(Amended from a right-hand column after a fit review: Delay/Multiband
overflow a 200 px column; a quiet lower band preserves the same hierarchy —
upper strong, lower subordinate — and right-alignment keeps it out of the
reading path.) Unfilled workspace remains composed Space bounded by the
section rules. No watermark (D-013).

## Per-module tiers (curated here, nowhere else)

| Module | Hero | Supporting | Advanced |
|---|---|---|---|
| Gate | Threshold | Range, Release | Attack, Hold, Mode |
| EQ | bespoke: 6 band columns registered under the Field | | band type Selectors |
| Filter | Cutoff | Resonance, Drive | Model |
| Compress | Threshold | Ratio, Attack, Release | Knee, Makeup, Upward, Mix, Detect, Style |
| Multiband | X-Low/X-High | band Thresholds ×3 | ratios, gains, Attack, Release |
| Saturate | Drive | Mix, Tone | Bias, Trim, Algorithm |
| Transient | Attack | Sustain | Speed |
| Motion | Depth | Rate, Mix | Feedback, Spread, Mode |
| Pitch | Amount | Mix | Mode |
| Delay | Feedback | Time/Division, Mix | Wow, cuts, Diffuse, Width, Color, Sync, Ping-pong |
| Reverb | Decay | Size, Mix | Pre-delay, Damp, Lo-cut, Shimmer, Width, Space |
| Stereo | Width | Mid, Side | Haas, Pan, Mono-below |
| Limit | Gain | Ceiling | Release, Soft clip |

## Motion mapping (04 catalog → this layout)

context selected → workspace morph with the station's stem brightening ·
order changed → stations slide along the meridian, others parting ·
element enabled → station Point fills · overlay → browser sheet over the
full face.

## Key map

`Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y` undo/redo · `P` browser · `X` A/B ·
`B` bypass · `R` dice (focused module) · `[` `]` presets · `1–0` stations.

## Layout tokens (mirrored in Theme.h alongside language tokens)

sections y (above) · meridian y 408 · graduation count 13 · hero zone 280 ·
advanced zone 200 · calibration set {−12, −24, −48 dB · 100/1k/10k Hz}.
