# 07 · Design tokens — the machine contract

`src/ui/Theme.h` must mirror this file exactly. A change to either without
the other is a defect; the commit that alters one alters both. Tokens are
split: **language tokens** hold for every Vesper product; **layout tokens**
belong to 08 and may be replaced wholesale.

## Language tokens

### Color (02)
| Token | Value |
|---|---|
| `night` | `#0A0C0F` |
| `ink` | `#F2F4F6` |
| `light` | `#86EFD5` |
| `error` | `#FF6B6B` |
| emphasis ladder | 7 · 16 · 38 · 62 · 100 % |
| light emphases | 16 · 62 · 100 % |
| high-contrast derivation | 38→56 · 16→28 · 7→14 · ink→`#FFFFFF` |

### Proportion (01)
| Token | Value |
|---|---|
| base unit `u` | 8 px @ reference scale |
| size ladder | … 24 · 38 · 60 · 96 · 152 … (×1.6) |
| space cadence | 0.5u · 1u · 2u · 4u · 8u |
| stroke ladder | 1 · 1.5 · 2 · 3 · 4 |
| point ladder | r 2.5 · 3.5 · 5 |
| arc sweep | 270°, bipolar from top |

### Type (03) — tabular numerals throughout
| Token | Spec |
|---|---|
| display | 28 regular |
| value | 17 regular |
| body | 13 regular |
| label | 11 medium caps +6 % |
| micro | 9.5 regular caps +4 % |

### Motion (04)
| Token | Value |
|---|---|
| `vesperEase` | cubic-bezier(0.4, 0, 0.2, 1) |
| `instant` / `move` / `settle` | 80 / 170 / 240 ms |
| meter release | ≈ 20 dB / 300 ms |
| clip hold | 1.5 s |

## Layout tokens

Defined in 08 (canvas, section bands, meridian, zones, calibration sets).
Code referencing a layout token outside layout-owned components is a
defect — that is what keeps the language portable.

## Conformance checklist (every design review)

- [ ] No hex color in code outside Theme.h; no fifth color anywhere
- [ ] No gradient; the field is flat
- [ ] No shape outside the five primitives
- [ ] Every size from the size ladder; every gap from the cadence scale
- [ ] Sibling gaps equal; rank changes double the gap; one pause per view
- [ ] No text below 9.5 px; tracking ≤ +6 %; numerals tabular
- [ ] Accent only on live values / active states / signal
- [ ] Every animation is in the 04 catalog; still at rest
- [ ] Watermarks, decorative glow, idle motion, icon fonts: absent
- [ ] No layout constant referenced outside layout code
- [ ] Recognition test passed on the release build (00 gates)
