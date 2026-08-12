# 09 · TARGET UI v2 — the founder's reference design

**Authority: this is the visual target set on 2026-07-18 from two
reference mockups ("this is what the UI should look like, strip the generic look").
It supersedes conflicting clauses in 01/02/06 via D-022. Build exactly
this; deviations require an explicit revision of this spec.** The mockup PNGs should live
in `docs/design/reference/` once added; this spec is
written to be sufficient without them.

Canvas 1080 × 680. Bands: **header 56 · signal 260 · module strip 78 ·
workspace 210 · performance 76** (= 680).

## 1 · Header (56)

- Left: star mark (accent, ~14 px) + `VESPER` wordmark (tracked caps).
- Center: **contained preset navigator** — a rounded field (radius 10,
  ink-4.5 % fill, ink-16 hairline border, ~360 × 40): `‹` and `›` at the
  ends, centered preset name (Body 14, ink-100) over a Micro subline
  (ink-38): current category, or `No preset loaded`. A separate `+`
  square field (40 × 40) to its right = save.
- Right, quiet word buttons (Label caps, ink-62 → 100 hover): `UNDO`
  `REDO` (disabled = ink-38) · `A / B` · `COPY` · `MOVES` (with a tiny
  4-point-wave glyph left of the word) · `DICE` · OS `2x ▾` · `···` ·
  gear-free. Far right: **bypass power glyph in an outlined pill**
  (44 × 36, hairline border): a primitive-drawn power symbol (arc + stem),
  accent when the plugin is active, ink-38 when bypassed. (Discipline: the
  mockup's redundant `BYP` word + power icon collapses to the one pill.)

## 2 · Signal field (260) — the calibrated analyzer

- **dB scale on BOTH left and right edges**: Micro numerals `0 −12 −24
  −48 −96` (log-spaced like the mockup: −12/−24 wide apart, floor
  compressed), ink-38. Faint horizontal rules (ink-7) at each.
- **Frequency ruler along the bottom edge**: graduations at
  20 50 100 200 500 1k 2k 5k 10k 20k — Micro, ink-38, lowercase `k`
  (discipline: never `1K`). 1 px ticks (ink-16) rise from the bottom rule;
  faint vertical rules (ink-7) at 100/1k/10k only.
- **Spectrum is NEUTRAL**: ink-16 area fill under an ink-38 crest line
  (1.3 px). The spectrum is context, not signal — accent is reserved for
  what the user is *doing*. (Supersedes "spectrum = light" in 02; D-022.)
- **EQ response curve is the accent layer**: 1.6 px curve. When the EQ
  module is selected the curve is `light`; node handles ride it (§3).
  When another module is selected the curve stays visible at ink-38
  (ghost) if any band is active.

## 3 · On-curve EQ editing (the FabFilter-class surface)

- Each enabled EQ band = a **node handle** on the curve: 11 px circle,
  night fill, 1.5 px ink-62 ring; selected band = accent ring + soft halo.
- Drag node = freq (x) + gain (y). **Wheel over node = Q.** Double-click
  node = toggle band bypass. Right-click node = band menu (type, dynamic
  amount, remove). Drag on empty curve area near the curve = create/grab
  nearest disabled band (stretch goal, ⏳ within M4).
- Node edits are ordinary parameter gestures (host-visible, undoable).
  The band strips in the workspace remain the numeric/precise editor;
  the curve is the tactile one.

## 4 · Module strip (78) — icon tiles

- 13 equal tiles, hairline vertical separators (ink-7). Each tile:
  **primitive-drawn line icon (~22 × 22, 1.5 px stroke, ink-62)** above a
  Label (9.5, caps, ink-38). Selected: icon + label turn accent, tile
  gets ink-4.5 % fill + accent underline notch pointing at the workspace
  (mockup 1's notch). Hover: one emphasis step. Enabled-but-not-selected:
  a 2.5 px accent point in the tile's top-right corner (replaces the
  meridian station dot's job).
- Drag a tile sideways to reorder (existing behavior).
- **Icon glyphs** (all composable from Rule/Point/Arc; never icon fonts):
  GATE 3 vertical bars, center tall · EQ a bell curve stroke · FILTER a
  descending slope with knee · COMP two waves converging (top/bottom
  rules pinching a wave) · MULTIBAND 5 ascending-descending bars ·
  SATURATE an S-curve (transfer function) · TRANSIENT a spike waveform ·
  MOTION two interlocked circles · PITCH a sine with rising tail ·
  DELAY three fading echo bars (or concentric arcs offset right) ·
  REVERB concentric rings from a center point · STEREO two overlapping
  circles (venn) · LIMIT a waveform clipped by top/bottom rules.
- The meridian *rule* survives as the strip's bottom edge: a full-width
  accent hairline under the tiles (mockup 1). The strip IS the meridian,
  worn as a toolbar.

## 5 · Workspace (210) — contained panel

- One **rounded container** (radius 14, ink-4.5 % fill, ink-16 border)
  inset 16 px from canvas edges. All module controls live inside it.
- Same tier layout as 08 (hero left ~280, supporting row, advanced
  right), plus, right-aligned column: the mode Selector rendered as a
  **contained dropdown field** (rounded 10, hairline border, value Body
  15 + caret) under its Micro label — matching the preset field's family.
- **Module power button** bottom-right of the panel (mockup 2): 44 px
  circle, hairline ring, primitive power glyph; accent when the module is
  on. Mirrors the strip tile's state.
- Sync-aware value displays: when a time parameter is host-synced its
  arc shows the division (`1/2`) with unit `SYNC` (Micro). Applies to
  Delay now; Motion gains rate-sync in M6.

## 6 · Performance strip (76) — contained band

- One rounded container full-width (inset 16). Three groups with hairline
  vertical separators:
  - `IN OUT MIX` mini arcs, **Label above the arc, value below** (mockup
    2): `0.0 dB` / `100 %` in Body 12.
  - `M1 M2 M3 M4 MORPH` same treatment (values `0 %`).
  - **Telemetry rows** right (~300): three labeled rows `LUFS · CORR ·
    GR`, each Micro label left, Value right (`−17.8`, `0.45`, `0.0 dB`),
    with a 3 px meter bar under each row (track ink-16, fill accent;
    GR bar fills from the right). Discipline: `−0.0` is forbidden — clamp to
    `0.0`.

## Anti-generic ruleset (what we strip from the mockups)

1. Casing/format: `1k` not `1K`; values per 03-TYPOGRAPHY (one decimal
   dB, no negative zero, thin-space before units).
2. One accent only: the mockup-1 amber EQ node becomes accent-ring
   selection; no second hue anywhere.
3. No redundant controls (`BYP` word + power icon → one power pill;
   `DICE ⋮` stray dots → none).
4. Glow budget: spectrum unlit (neutral); glow only on engaged elements
   and the selected tile/node.
5. All icons primitive-drawn to one grid (22 px, 1.5 px stroke, round
   caps) — no mixed visual weights, no icon fonts (D-011 stands).
6. Grid discipline: every inset 16, every gap from the cadence scale.

## Migration map (what becomes what)

| Today | Target |
|---|---|
| Meridian stations (points + labels) | Icon tiles; meridian rule = strip's bottom edge |
| Free-floating workspace | Contained workspace panel |
| Analyzer freq ticks (3) | Full bottom ruler (10 graduations) + dual dB edges |
| Accent spectrum | Neutral spectrum, accent EQ curve + nodes |
| EQ numeric-only editing | + on-curve node editing |
| Utility row loose | Contained performance strip, values under arcs |
| Header preset text button | Contained preset navigator field |

## Milestones (mirrored in ROADMAP Phase 3 · UI v2)

- **M1 [M]** Containment + strip tiles + icons (no behavior change)
- **M2 [S]** Header: preset navigator field + power pill
- **M3 [M]** Signal field: dual dB edges, full frequency ruler, neutral
  spectrum, accent EQ curve (+ghost state)
- **M4 [L]** On-curve EQ node editing (drag/wheel/dbl-click/right-click)
- **M5 [S]** Performance strip: containment, values-under-arcs, telemetry
  rows with meter bars
- **M6 [M]** Sync displays (`1/2 SYNC`) + Motion rate-sync param
- Each milestone ends with the full ritual + a screenshot against this
  spec; M4 additionally needs an interaction review.
