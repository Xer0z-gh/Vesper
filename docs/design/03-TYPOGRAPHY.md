# 03 · Typography

Typography is Vesper's loudest identity carrier. Values are set large and
confident; labels are quiet and exact. Hierarchy comes from **size and
opacity**, never color.

## Scale (at 1080 × 680 canvas)

| Style | Size | Weight | Case | Tracking | Use |
|---|---|---|---|---|---|
| **Display** | 28 | regular | — | 0 | hero arc values |
| **Value** | 17 | regular | — | 0 | supporting arc values, LUFS |
| **Body** | 13 | regular | sentence | 0 | selectors, menus, browser |
| **Label** | 11 | medium | UPPER | +6 % | control & station labels |
| **Micro** | 9.5 | regular | UPPER | +4 % | units, calibration numerals |

Face: system sans (Segoe UI Variable / SF Pro). All numerals **tabular** —
values must not wobble while changing. (Shipping a bundled face is a
one-line change in `theme::font`; candidate review is a Phase 3 milestone.)

## Laws

1. **No tiny text.** Nothing below 9.5 px at base scale; Label is the
   floor for anything a user must read to operate.
2. **Restrained tracking.** +6 % maximum (the v1 system's +14 % is
   retired — wide tracking reads as decoration).
3. **Weight before opacity, opacity before size, size only for rank.**
   A label brightens (38→62 %) on engagement; it never changes size.
4. **Units are part of the numeral.** Set in Micro, ink-38, after the
   value: `6.0 dB` · `1.2 kHz` · `140 ms` · `55 %` · `3.0:1`. Never on a
   separate line, never uppercase-forced (dB, Hz, ms, %, :1 keep their
   canonical forms).

## Value formatting (exact, per unit)

| Unit | Rule | Examples |
|---|---|---|
| dB | one decimal, explicit sign when bipolar | `6.0 dB` · `−3.5 dB` · `+0.0 dB` |
| Hz | integer < 1 kHz; 2 decimals in kHz above | `240 Hz` · `1.20 kHz` · `12.5 kHz` |
| ms | one decimal < 10; integer ≥ 10 | `0.5 ms` · `8.2 ms` · `140 ms` |
| s | one decimal | `2.6 s` |
| % | integer | `55 %` |
| ratio | one decimal + `:1` | `3.0:1` |
| LUFS | one decimal | `−12.3` |

A value that cannot be read at a glance is a defect, not a style choice.
