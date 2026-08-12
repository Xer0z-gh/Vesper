# Vesper — Requirements coverage

Where every requirement from the product brief lives in v1.0. ✅ shipped ·
🟡 covered by an equivalent/simplified mechanism (explained) · 📋 designed +
scheduled (see `ROADMAP.md`).

## Workflow & UI

| Requirement | Status | Where |
|---|---|---|
| Minimal, premium dark UI, glass + soft depth | ✅ | `ui/Theme.h` (tokens), `ui/VesperLNF.h` |
| Mouse wheel / Shift-fine / double-click reset / right-click | ✅ | `ui/Knob.h` |
| Full keyboard shortcuts | ✅ | `VesperEditor::keyPressed` (documented in MANUAL) |
| Scalable UI + HiDPI | ✅ | Transform-scaled canvas, 75–200 %, JUCE DPI awareness |
| Undo/Redo history | ✅ | `juce::UndoManager` through APVTS; covers preset loads, dice, Moves |
| A/B comparison | ✅ | `VesperProcessor::toggleAB/copyActiveToOther` (X / ⇥) |
| Preset browser: search, favorites, tags | ✅ | `core/PresetManager`, `ui/PresetBrowser.h` |
| Randomize (musical, range-bounded, module-aware) | ✅ | `core/Creative.h` Randomizer |
| Smart suggestions | 🟡 | Tag+category filtered browsing and per-module dice; content-aware ranking 📋 |
| Zero unnecessary buttons | ✅ | 12 header controls, each documented in a 30-second workflow |

## Audio quality

| Requirement | Status | Notes |
|---|---|---|
| 64-bit processing | ✅ | All filter/detector/delay state is double; I/O buffers float (VST3 float path). Full double I/O 📋 |
| Oversampling 2–16× | ✅ | Polyphase IIR halfband around the saturation stage; latency reported + dry-path compensated |
| Zero zipper noise | ✅ | Every audible parameter rides `SmoothedValue`/one-pole slews |
| Sample-accurate automation | 🟡 | Block-accurate + per-sample smoothing (JUCE host path); sub-block splitting 📋 |
| Low CPU | ✅ | Single pass/module, no allocations post-prepare, denormal flushing everywhere |
| SIMD | 🟡 | `-msse4.2`/`/arch:AVX2` + JUCE vector ops in hot paths; explicit batching 📋 (profile first) |
| Multi-threaded DSP | 🟡 | Serial chain is latency-optimal by design; analysis/FFT runs on the UI thread. Deliberate. |
| No aliasing / clicks / pops | ✅ | OS on nonlinears, Hermite-interpolated modulated delays, matched-delay bypass for latency modules |
| Ultra-low latency | ✅ | 0 samples at 1×; only OS filters + 1.5 ms limiter lookahead otherwise |
| Linear phase where appropriate | 📋 | FIR EQ option (v1.2) |

## Processing modules (13-module reorderable chain)

| Family | Shipped in v1.0 |
|---|---|
| Dynamics | Compressor (down + **upward**, Peak/RMS, Clean/Punchy/Vintage) · **Multiband** (3-band LR4) · **Gate/Expander** · Transient Designer · Lookahead **true-peak Limiter** (16-tap inter-sample detector, D-019) · Soft/Hard **Clipper** (Saturator algos + limiter soft clip) |
| EQ | 6-band surgical parametric (bell/shelves/HP/LP/notch) with analyzer-overlaid response and **per-band dynamics** (adaptive reference, ±18 dB — de-ess, tame resonances, or open bands with one knob). Tilt = complementary shelves; Match/Harmonic 📋 |
| Saturation | 17 unique curves: Tube, Tape, Transformer, Console, Soft Analog, Warm, Exciter, Triode, Pentode, Germanium, Silicon, Foldback, Wavefold, Bitcrush, Downsample, Soft Clip, Hard Clip |
| Spatial | FDN reverb (Room, Hall, Plate, Bloom) + **convolution** Spring & Cathedral (procedural IRs, D-029) + **Shimmer**; predelay, damping, width, locut. Reverse reverb + user IR loader 📋 |
| Delay | Digital/Tape/Analog color, sync incl. dotted+triplet, ping-pong, wow, diffusion, width, filtered feedback. Multi-tap 📋 |
| Stereo | Width, M/S gains, Haas, constant-power pan, mono-below (Doubler lives in Motion + Pitch Wide Double) |
| Modulation | Chorus, Ensemble, Doubler, Flanger, Phaser, Vibrato, Tremolo, Rotary |
| Filters | SVF 12/24 (SEM-class), ZDF Ladder (Moog-class), Acid (MS-20-class), Comb, Formant; Butterworth = Clean 24; LR4 lives in Multiband |
| Pitch | Micro detune, Octave Up/Down, Fifth (harmonizer interval), Wide Double. Formant shift / vocoder-grade 📋 |
| Utility | In/Out gain, pan, mono-maker, DC-safe saturator output, global latency-compensated Mix |

## Metering & visualizers

Peak, RMS, momentary **LUFS** (K-weighted, BS.1770 constants), **correlation**
(`dsp/Analysis.h`, `ui/MeterStrip.h`) · 2048-pt spectrum analyzer with EQ
response overlay (`ui/AnalyzerView.h`) · compressor/limiter GR readouts.
Spectrogram, scope, Lissajous 📋.

## Creative layer

**Moves** (17 one-gesture transforms incl. Punch/Wider/Air/Vintage/Modern/
Presence/Glue/Loud/Dreamy/Huge/Intimate…) · **Macro Designer**: M1–M4 with
right-click assignment on any control, MIN/MAX travel pinning, unlimited
targets per macro, session-persistent (D-020) ·
**Morph knob** — continuous every-parameter morph between the live state and
the other A/B slot (load any two presets into A and B, then blend) · musical
**Randomizer** · genre-tagged factory library (24 presets: EDM, trap, lo-fi,
vocals, mastering, podcast, sound design…). Per-macro assignment editor 📋.

## Platform & engineering

C++20 · JUCE (7.0.12 pinned for the portable MinGW toolchain; MSVC/Xcode CI)
· CMake + Ninja · VST3/AU/Standalone (AAX hook documented — needs Avid SDK) ·
Windows/macOS/Linux CI with unit tests + **pluginval strictness 8** ·
factory presets embedded as BinaryData, self-installing on first run ·
Inno Setup + pkgbuild installers · EULA + licensing design (`LICENSING.md`).
