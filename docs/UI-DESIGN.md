# Vesper — design authority

**The design system lives in [`docs/design/`](design/) and governs every
pixel.** This file is a map, not a second authority.

The system separates **language** (timeless, product-family DNA) from
**layout** (this plugin's implementation, replaceable):

| Doc | Scope | Covers |
|---|---|---|
| [00-PHILOSOPHY](design/00-PHILOSOPHY.md) | language | philosophy · emotional contract · laws · release gates |
| [01-GEOMETRY](design/01-GEOMETRY.md) | language | the five primitives (incl. Space) · proportion ladders · rhythm laws |
| [02-COLOR](design/02-COLOR.md) | language | 4-color system · emphasis derivations · high contrast |
| [03-TYPOGRAPHY](design/03-TYPOGRAPHY.md) | language | type scale · numeral rules · value formatting |
| [04-MOTION](design/04-MOTION.md) | language | one easing · the information-event catalog · reduced motion |
| [05-INTERACTION](design/05-INTERACTION.md) | language | gesture contract · states (single source) · accessibility system |
| [06-COMPONENTS](design/06-COMPONENTS.md) | language | primitive-composed components, tiered, placement-agnostic |
| [07-TOKENS](design/07-TOKENS.md) | contract | language vs layout tokens · Theme.h mirror · conformance checklist |
| [08-PLUGIN-LAYOUT](design/08-PLUGIN-LAYOUT.md) | layout | the meridian architecture · sections · zones · per-module tiers · key map |

In one sentence: **Vesper is a precision instrument composed from five
primitives — Rule, Point, Arc, Numeral, Space — in fixed proportions;
light means live, typography carries hierarchy, motion explains, and
nothing decorative exists.** The meridian is this plugin's composition,
not the brand (D-017).

History: v1 "HORIZON" (gradients, watermarks, spent glow) superseded per
D-013…D-015; the v0.9→v1.0 language review is D-016/D-017.
