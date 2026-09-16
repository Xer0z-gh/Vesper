# Changelog

## [Unreleased] — state as of 2026-08-11

What the source in this repository does today. Nothing past 1.0.0 is tagged yet;
the last dated record behind these entries is decision D-067 (2026-08-11).

- Chain grew to 13 modules: Gate, EQ, Filter, Compressor, Multiband,
  Saturator, Transient, Motion, Pitch, Delay, Reverb, Stereo, Limiter
- 17 saturation algorithms: Tube, Tape, Transformer, Console, Soft Analog,
  Warm, Exciter, Triode, Pentode, Germanium, Silicon, Foldback, Wavefold,
  Bitcrush, Downsample, Soft Clip, Hard Clip
- Linear-phase match EQ: learn a reference and a source, design a corrective
  FIR by frequency sampling
- Convolution reverb with Spring, Cathedral, Reverse and user IRs; shimmer
  restructured as feedforward so it cannot run away
- True-peak limiting via a 16-tap Blackman-windowed sinc inter-sample
  detector (D-019)
- 42 factory presets, embedded at build time and audited against the live
  parameter set by the integration suite (D-040)
- Filter gain calibration: worst-case broadband gain cut from +11.8 dB to
  +2.9 dB, with the resonance mapping untouched
- Editor sizes against the display it actually lands on, not the primary
  display (D-060)
- Test suites: 142 unit checks, 49 integration checks, 0 failures

## [1.0.0] — 2026-07-16

Initial release.

- 10-module reorderable chain: EQ, Filter, Compress, Saturate, Transient,
  Motion, Delay, Reverb, Stereo, Limit
- 15 saturation algorithms with 1–16× oversampling
- 8-line FDN reverb with shimmer; tape/analog/digital delay
- Motion module: 8 modulation modes
- A/B compare, undo/redo, preset browser (search / tags / favorites)
- Moves (one-gesture transformations), musical randomizer
- 24 factory presets, embedded and self-installing
- Display-aware UI scaling (75–250 %), high-contrast mode, full keyboard map
- CI: Windows/macOS/Linux builds, unit tests, pluginval level 8
