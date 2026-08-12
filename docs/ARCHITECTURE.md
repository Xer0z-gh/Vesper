# Vesper — Architecture

## Layering

```
┌──────────────  src/ui  ───────────────┐   message thread only
│ Theme · LNF · Knob · Rack · Panels    │   (attachments, timers @60fps)
│ Analyzer · Meters · Browser · Stepper │
├──────────────  src/core  ─────────────┤   message thread
│ Params (layout, ids)  PresetManager   │
│ Creative (Moves, Randomizer)          │
├──────────  VesperProcessor  ──────────┤   the only class both threads touch
│ APVTS · chain order atomic · A/B      │
├──────────────  src/dsp  ──────────────┤   audio thread, allocation-free
│ Core (primitives) · Saturation        │
│ Dynamics · EQFilter · TimeFX · ModFX  │
│ Analysis (atomics / SPSC fifo out)    │
└───────────────────────────────────────┘
```

**Rule: `src/dsp` includes only `juce_dsp` + `juce_audio_basics` and never
sees a parameter object.** Modules expose plain `set(...)` methods; the
processor is the sole translator from APVTS atomics to module state. This is
what makes the unit-test target possible without loading a plugin host.

## Threading model

| Data | Writer | Reader | Mechanism |
|---|---|---|---|
| Parameters | host/UI | audio | APVTS raw atomics, cached in a map at construction |
| Chain order | UI | audio | one `std::atomic<uint64>`, 4 bits per slot |
| Meters / GR | audio | UI | relaxed atomics, UI owns ballistics |
| Spectrum | audio | UI | SPSC sample FIFO; FFT runs on the UI thread |
| Presets / A/B | UI | — | `replaceState` on the message thread; audio reads atomics as usual |

No locks, no allocation, and no waiting on the audio thread after `prepare`.

## The chain

Ten stateful module instances live for the plugin's lifetime; *order* is data
(a permutation), so reordering never destroys state and never glitches.
Parameters are static in the APVTS — hosts see a stable automation surface
regardless of order.

### Constant latency

Total latency = saturator oversampling (factor-dependent) + limiter lookahead
(1.5 ms), reported via `setLatencySamples`. When either module is **disabled**
the processor routes audio through a matched delay instead, so toggling a
module never slips the mix against the compensated dry path. The global Mix
control blends against a fractionally-delayed dry copy and **nulls exactly at
100 % wet**.

## State & serialization

- Everything is one `ValueTree` (APVTS state) — parameters plus non-parameter
  properties: `chainOrder` (csv permutation), `presetName`, `uiScale`.
- Host session state = binary-wrapped XML of that tree (`getStateInformation`).
- Preset files (`.vpreset`) are *sparse*: only non-default parameters. Loading
  resets to defaults first, so presets are forward-compatible (unknown ids
  ignored, new params get defaults).
- A/B = two ValueTrees swapped wholesale; Copy duplicates active → other.
- Undo/redo = `juce::UndoManager` wired through the APVTS; preset loads,
  Moves and randomize each open a named transaction.

## Precision & performance decisions

- Filter and delay **state is double**; sample I/O is float (the same
  internal-64-bit approach used by the major commercial plugins). A full
  double I/O path is on the roadmap.
- Denormals: `ScopedNoDenormals` per block plus explicit flushing in every
  feedback path (they'd survive FTZ on some hosts).
- Oversampling uses JUCE's polyphase IIR halfband — lowest latency; the
  saturator owns pre-built 2×/4×/8×/16× instances so switching factors never
  allocates on the audio thread.
- Per-sample smoothing (`SmoothedValue` / one-pole slews) on every gain,
  mix, time and cutoff — zipper noise is treated as a correctness bug, and
  delay-time changes glide (tape-style) instead of clicking.
- SIMD: the hot loops are written branch-light and auto-vectorizable
  (`/arch:AVX2`, `-ffast-math`); explicit `juce::dsp::SIMDRegister` batching
  is a planned optimization, profiled first.
- Multi-threading: a serial chain gains nothing from splitting one block
  across cores (it adds latency and jitter); heavy *analysis* work (FFT)
  already runs off the audio thread. This is deliberate, not an omission.

## Parameter system

`src/core/Params.h` is the single source of truth: ids, ranges, skews,
defaults, choice lists, and the packed-order helpers. UI, DSP binding,
presets and the randomizer all include it — they cannot disagree with each
other. Parameter versioning uses `juce::ParameterID{id, 1}`; bump the version
only on breaking range changes.
