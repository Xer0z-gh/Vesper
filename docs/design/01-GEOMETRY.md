# 01 · Primitives & proportion

The language, independent of any layout. Everything visible in any Vesper
product is composed from **five primitives**; recognition comes from their
fixed proportions, not from where a given screen puts them.

## The five primitives

| Primitive | Definition |
|---|---|
| **Rule** | a hairline — structure, scale, or path |
| **Point** | a dot of light — position, state, presence |
| **Arc** | a portion of a circle about a point — any continuous value |
| **Numeral** | a set value — the loudest voice (03) |
| **Space** | composed emptiness — rhythm, hierarchy, calm |

There is no sixth shape. Boxes, bezels, pills and filled panels do not
exist; regions are bounded by Rules and Space. Compositions, not new
shapes, make components (06): a switch is a Point on a short Rule; a meter
is a Rule with a moving Point; a control is an Arc with a Numeral inside.

## Space — the active element

Space is designed, never left over.

- **Cadence scale:** gaps come only from the doubling ladder
  **0.5u · 1u · 2u · 4u · 8u** (u = the base unit, 07). No other gaps.
- **Sibling law:** elements of equal rank share one gap value.
- **Rank law:** crossing a hierarchy boundary at least *doubles* the gap.
  Near-even spacing across different ranks (< ×2) is a defect — this is
  what makes rhythm enforceable rather than a vibe.
- **The pause:** every view composes exactly one dominant Space — a
  deliberate area of silence that lets the composition breathe. A view
  with uniform density fails review.

Rhythm over grid: the base unit aligns edges; it does not entitle anything
to fill a cell. Large · medium · small · pause — cadence, not symmetry.

## Proportion system (the recognizable ratios)

| Ladder | Values | Law |
|---|---|---|
| **Size** | … 24 · 38 · 60 · 96 · 152 … (×1.6 geometric) | every element's diameter/length comes from the ladder; adjacent hierarchy tiers sit ≥ 1 step apart — a hero is *visibly* ×1.6 its satellites |
| **Emphasis** | 7 · 16 · 38 · 62 · 100 % | one ladder for ink opacity, data weight, and state (02, 05); engagement moves exactly one step |
| **Stroke** | 1 · 1.5 · 2 · 3 · 4 px @ reference | structure 1 · data 1.5 · arc tiers 2/3/4 |
| **Point** | r 2.5 · 3.5 · 5 | state ladder; engagement moves one step |
| **Sweep** | 270°, bipolar rises from top-center | every Arc, every product, always |

These ratios *are* the brand's geometry. A hardware Vesper would express
the same ladders in millimetres and LED intensities; a website in rem.

## Orientation & weight

Weight leads, detail follows: the dominant element sits at the start of
the reading direction, telemetry at edges. Perfect symmetry is reserved
for at most one datum element per product (in the plugin, the meridian —
08); everything else composes asymmetrically around the pause.

## What this file deliberately does not contain

Canvas sizes, section bands, the meridian, calibration frequencies,
per-module hierarchies — those are layout (08). If a rule here would break
when the layout changes completely, moving it to 08 is the fix.
