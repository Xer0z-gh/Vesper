# 05 · Interaction & accessibility

## The gesture contract (every continuous control, every product)

| Gesture | Result |
|---|---|
| drag | coarse edit (velocity mode) |
| **Shift** + drag | fine |
| wheel / Shift + wheel | step / fine step |
| double-click | reset to default |
| right-click | *Enter value… · Reset · (assignment actions)* |
| hover | engagement: one emphasis step up, Point one size step up |

Identical everywhere, from hero controls to utility. Muscle memory is a
feature; this table never changes casually (Philosophy law 7).

## Reach law

Common actions are **one gesture**, never a menu: toggle, select, reorder,
edit, compare, audition, undo. Anything reached through a menu is by
definition uncommon; if telemetry shows it's common, it earns a gesture.

## States — the single source (referenced by 02 and 06)

| State | Expression |
|---|---|
| rest | ink at documented emphasis, crisp, flat |
| engaged (hover/focus) | +1 emphasis step, Point +1 size step |
| active (dragging/selected) | accent; the only earned glow |
| disabled | emphasis floor (16 %), no affordance |
| error | error color, only when true |

State is always expressed through the emphasis and size ladders (01) —
never through new colors or shapes.

## Keyboard

Full traversal in eye-path order; arrows edit the focused control
(Shift = fine); every control reachable without a pointer. Product key
maps live with their layouts (08 · MANUAL); the *principles* — complete
coverage, no overloading, stable across versions — live here.

## Accessibility system

- **Contrast:** operative text ≥ 4.5:1 at rest, ≥ 7:1 engaged; structural
  rules exempt (non-operative). Verified per release.
- **High contrast** via the color derivation table (02) — inherited by
  every component with zero component code.
- **Screen readers:** grammar is `context · parameter · value`
  ("Saturate, Drive, 6.0 dB"); stateful elements announce state and
  position ("Reverb, on, slot 11 of 13").
- **Hit targets:** ≥ 44 × 44 px logical minimum, primary controls larger.
- **Reduced motion:** 04 law 4.
- **Scaling:** 75–200 % first-class; vector-only rendering guarantees it.

## Disclosure

Nothing is more than one gesture deep. Advanced parameters occupy a
visibly quieter zone (emphasis + size ladders), not a hidden panel.
Tooltips: one line, Body type, 600 ms delay, never covering their subject;
depth belongs to the manual.
