# 04 · Motion

Motion is information. Every animation must answer at least one of:
**what changed · where did it move · why did it happen.** If it answers
none, it does not ship. Motion never entertains; it explains, and it
should feel inevitable — the visual consequence of the user's action.

## The one easing

`vesperEase` — cubic-bezier(0.4, 0.0, 0.2, 1.0). Confident start, precise
landing. No bounce, no overshoot, no elastic, anywhere, ever. An
instrument settles; it does not wobble.

## Durations

| Token | ms | Use |
|---|---|---|
| `instant` | 80 | state flips, value catch-up, engagement |
| `move` | 170 | context morphs, reordering, overlay reveal |
| `settle` | 240 | overlay dismissal, whole-state transitions |

Nothing exceeds 240 ms; hands are faster than that.

## The catalog (information events — layout-agnostic)

Implementations (08) map these events to their geometry; the events and
their meanings are fixed across products.

| Event | Motion | Answers |
|---|---|---|
| context selected | outgoing content fades (`instant`); incoming rises one 1u-step and fades in (`move`); the selector's Point brightens | what/where |
| order changed | elements *travel* to their new positions (`move`) — order change is visible as travel, never as a swap | where/why |
| element enabled | its Point fills accent (`instant`) | what |
| value edited | the Arc tracks live; the Numeral catches up within `instant` | what |
| state compared (A/B, morph) | values glide to the other state (`settle`) — audible change is visible change | what/why |
| overlay | slides over the face (`move`), leaves the same way (`settle`); the instrument never reflows beneath | where |
| meters | attack instantaneous; release ballistic ≈ 20 dB / 300 ms | truth |
| clip / destructive | error appears instantly, holds 1.5 s | why |

Additions to this catalog require a design review; there is no other
sanctioned animation.

## Laws

1. **Stillness at rest.** No idle motion, breathing, loops or particles.
   An untouched Vesper is perfectly still except live signal.
2. **Interruptible always.** New input cancels any motion at its current
   position. The user never waits for an animation.
3. **One clock.** All motion runs off a single display-refresh timebase.
4. **Reduced motion:** every catalog entry collapses to instant except
   meter ballistics and live arcs — those are signal truth, not styling.
