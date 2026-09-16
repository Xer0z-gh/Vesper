# Vesper — DSP design notes

Why each algorithm is what it is. File references are to `src/dsp/`.

## Primitives (`Core.h`)

- **Biquad** — RBJ cookbook, transposed direct form II, double coefficients
  *and* state. TDF2 in double keeps low-frequency shelves clean at 44.1 kHz.
- **SVF** — Zavalishin TPT (the cytomic form). Used where cutoff moves per
  sample (creative filter, rotary crossover) because it stays stable under
  fast modulation, unlike swapping biquad coefficients.
- **DelayLine** — power-of-two ring buffer, Hermite (4-point) fractional
  reads for anything that modulates; linear reads where the delay is static.
- **OctaveShifter** — dual-tap crossfaded delay ("granular" shimmer core),
  60 ms window, triangular crossfade. Cheap, smooth, and exactly the classic
  shimmer sound; a full phase-vocoder shifter is roadmap.
- **fastTanh** — Padé 3/2 rational, |err| < 2·10⁻³ over ±6. Used in feedback
  paths where std::tanh would dominate the profile; the ladder keeps real
  `std::tanh` because its tone *is* the transfer curve.

## Saturation (`Saturation.h`)

Fifteen distinct transfer curves (see `shapeSample`). Design intent:

| Algo | Fingerprint |
|---|---|
| Tube / Triode | asymmetric → 2nd harmonic; Triode is the same family, lazier negative lobe |
| Tape | arctan knee — compresses before it distorts |
| Transformer | blended tanh at 1× and 3× drive → dense odd series |
| Console | cubic only — "glue", nearly linear until pushed |
| Soft Analog | sine shaper — folds gracefully at the extreme |
| Warm | tanh + x² term, 2nd-harmonic dominant |
| Exciter | hard-driven tanh mixed with dry — brightens without mud (pair with Tone) |
| Pentode | exponential knee — later, harder break-up |
| Germanium / Silicon | diode pair models: lazy asymmetric vs. tight aggressive |
| Foldback / Wavefold | iterative triangle fold vs. smooth sin fold |
| Bitcrush / Downsample | drive maps 16→3 bits / 1→32× hold |

Shared plumbing: drive is loudness-compensated (√drive), **Tone is a
pre/post tilt pair** — it changes *what gets saturated*, then restores the
static balance; bias adds even-harmonic asymmetry with the DC term
subtracted; a 8 Hz DC blocker sits post-shape. Nonlinear stages run inside
2–16× polyphase-IIR oversampling; 1× is offered for lo-fi aliasing on
purpose (Bitcrush/Downsample want it).

## Dynamics (`Dynamics.h`)

- **Compressor** — linked stereo, peak/RMS detector in double, log-domain
  soft-knee curve. *Characters* alter the detector, not the curve: Punchy
  shortens effective attack; Vintage adds a slow second release stage
  (program-dependent release). **Upward** lifts below threshold, capped at
  half the distance and faded out below −70 dB so silence stays silent.
- **Transient designer** — classic two-follower topology (fast − slow =
  transient); ±100 % ≈ ±12 dB on the isolated component. Speed presets set
  both follower time constants.
- **Limiter** — 1.5 ms lookahead; attack coefficient chosen so the gain
  envelope fully lands within the lookahead window (no overshoot by
  construction), user release, optional tanh soft-clip pinned to the ceiling
  for inter-sample energy. **True-peak detection reconstructs rather than
  oversamples** (D-019): a 16-tap Blackman-windowed sinc evaluates the three
  inter-sample points at 1/4, 1/2 and 3/4, so the detector costs no extra
  latency and no resampler. Enabled by the `lim_tp` parameter.

## EQ & Filter (`EQFilter.h`)

- **ParametricEQ** — 6 bands × {Bell, LS, HS, HP, LP, Notch}. Coefficients
  update only when a band's values actually change (cached compare), which
  keeps automation cheap. `responseDbAt` is static so the UI computes the
  curve from parameter values without touching audio state.
- **CreativeFilter** — Clean 12/24 (TPT SVF), **Ladder** (4-pole ZDF cascade
  with tanh input/feedback, Huovilainen-flavored), **Acid** (same core,
  hotter drive + resonance trim), **Comb** (tuned feedback comb, tanh-limited
  so max feedback sings instead of exploding), **Formant** (two SVF bandpasses
  morphing A→E→I along the cutoff axis). Cutoff is smoothed per-sample.

## Time FX (`TimeFX.h`)

- **Delay** — time changes are slewed through a 120 ms one-pole, so tempo or
  knob changes *repitch* like tape instead of clicking. Colors: Tape adds wow
  (rate follows depth) + tanh feedback rounding + the user hi-cut; Analog is
  darker with harder feedback compression (BBD-ish); Digital is clean.
  Ping-pong crosses the feedback. Diffusion is two allpasses in the loop.
  Feedback allows up to 115 % — self-oscillating dub throws are a feature;
  the nonlinearity in the loop is what keeps it bounded.
- **Reverb** — 8-line FDN with a Householder matrix (energy-preserving,
  colorless), mutually-prime line lengths per space type, one-pole damping
  per line, ±0.11 % modulated Hermite taps (kills metallic ringing),
  per-line gains solved from T60. **Shimmer** feeds octave-shifted line sums
  back into the network — inside the loop, so it blooms recursively.
  Bloom = hall lengths + deeper/longer input diffusion (slow onset).

## Stereo (`ModFX.h → StereoTools`)

M/S width & gains, Haas (right-channel delay up to 40 ms), constant-power
pan, and **Mono Below** implemented as a 4th-order highpass on the side
signal only — the standard "mono-maker" that tightens lows without touching
the mid.

## Analysis (`Analysis.h`)

Momentary LUFS per BS.1770 (K-weighting: +4 dB shelf @1681.97 Hz Q 0.7071 +
RLB highpass @38.13 Hz Q 0.5, 400 ms window), correlation over a 300 ms
window, peak-hold handed to the UI as consume-and-clear atomics. The FFT tap
is a lock-free SPSC fifo of raw samples; windowing (Hann) and the 2048-point
transform run at UI rate, never on audio.

## Quality gates (tested in `tests/TestMain.cpp`)

Every saturation algorithm bounded & finite at max drive · silence-in →
silence-out · compressor GR sanity · limiter ceiling never exceeded · delay
timing exact · reverb tail decays to <−40 dB · ladder stable at max
resonance + drive · mono-below kills LF side content · order pack/unpack
roundtrip.
