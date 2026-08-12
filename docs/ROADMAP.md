# Vesper — Master Plan

The governing document of a long-lived flagship project. Vesper is not being
"finished"; it is being refined until it is a benchmark. Every phase ends
with a design review against one question: *would FabFilter, Apple, Ableton
and Teenage Engineering — with unlimited time — ship this?*

**⚑ 2026-07-27: the FLAGSHIP PRODUCT QUALITY PASS (D-054)**
governs all remaining work.** Its six gates apply to every item below; its
five workstreams are the active queue. Read it before picking up any task.

Legend: ✅ shipped · 🔄 in flight · ⏳ planned · 💭 research
Complexity: S (<1 day) · M (days) · L (weeks) · XL (a system)

---

## Phase 0 — Vision & identity ✅ (living)
- ✅ Product thesis: a creative sound-design *environment*, not a multi-FX
- ✅ Brand: VESPER / Sable Audio (renamed from Tannin, D-058), the evening-star mark
- ✅ HORIZON visual identity (see UI-DESIGN.md, D-012)
- 🔄 Identity audit each phase: does every new pixel still read as Vesper?

## Phase 1 — Core architecture ✅ (guarded by tests)
- ✅ Params.h single source of truth (D-001) · lock-free chain order (D-004)
- ✅ Latency model: constant per OS setting, matched-delay bypass, dry-path
  compensation (global Mix nulls at 100 %)
- ✅ State: APVTS + UndoManager · A/B slots · preset XML (D-002)
- ❌ **Sub-block parameter splitting — REJECTED** (D-037): JUCE's VST3
  wrapper keeps only each queue's last point and drops `offsetSamples`, so
  APVTS values are constant within a block — splitting has nothing finer to
  read. Superseded by the **automation-continuity harness** (every
  parameter→sample path is already ramped; proven with a negative control)
- ❌ **Double-precision I/O — REJECTED** (D-045): module state is already
  double; float exists only *between* modules. Measured a linear chain as
  scale-invariant to **145 dB SNR** across 100 dB — beyond 24-bit delivery.
  Templating 13 modules for that buys nothing audible; the measurement ships
  as a standing test so the claim stays true
- 💭 [L] Module graph v2: keep the *reading* model (one line) while allowing
  parallel sends (delay/reverb wet buses) without patch-cord UI

## Phase 2 — DSP engine ✅ core · 🔄 depth
Shipped: 13 modules — Gate, 6-band EQ, 6-model Filter, Compressor
(down+upward, 3 characters), 3-band LR4 Multiband, 17-algorithm Saturator
(1–16× OS), Transient, 8-mode Motion, 5-mode Pitch, character Delay, FDN
shimmer Reverb, Stereo tools, lookahead Limiter. All double-precision state,
denormal-safe, allocation-free post-prepare.
- ✅ **Dynamic EQ** (D-018): per-band adaptive-reference dynamics, ±18 dB
  DYN per band, chunk-rate detection, slew-guarded coefficients, with the
  "listening" point of light under active bands
- ✅ **True-peak limiting** (D-019): 16-tap windowed-sinc inter-sample
  detector (4× reconstruction) in the Limiter, TRUE PEAK switch, on by
  default; regression-tested against a 32-tap reference reconstructor
- ✅ **Sidechain bus** (D-021): dedicated stereo/mono input bus; SC toggles
  route the Comp/Gate detectors to it (ducking, keyed gating); bus-aware
  main-channel staging fixed a latent mono+SC indexing hazard
- ✅ **Convolution reverb** (D-029): Spring + Cathedral + **Reverse** spaces
  (reverb types 4-6) via juce::dsp::Convolution with procedural IRs;
  message-thread IR regen (AsyncUpdater). ✅ Follow-ups done (D-038):
  **user IR loader** (7th SPACE entry, any wav/aiff/flac, path persists,
  falls back to Cathedral + reports `IR MISSING` if unreadable) and
  **convolution shimmer** (octave-up folded into the conv *input* —
  feedforward, so runaway is structurally impossible)
- ✅ **Match EQ** (D-032): learn reference + source long-term spectra, then
  apply a **linear-phase FIR** (frequency-sampling design, fractional-octave
  smoothing, ±max-dB clamp, amount) via partitioned convolution at the input
  stage; latency reported + dry-path compensated; curve persists in state.
  Unit-tested (flat→unit-delta, ±2 dB magnitude accuracy, linear-phase
  symmetry, latency/finite integration). ✅ Follow-ups done (D-039):
  **A/B reference slots** (`mch_ref`) and **capture point** (`mch_tap`,
  input or post-chain output)
- ✅ **Linear-phase EQ** (D-036): `eq_lp` renders the static 6-band curve
  as a 2048-tap linear-phase FIR (shared Match EQ design path) with
  reported + bypass-matched latency; DYN inactive in this mode; FIR kept
  warm on every EQ edit. LIN PHASE toggle beside the band strip
- 💭 [XL] Phase-vocoder pitch: formant-preserving shift + harmonizer
  (v1 granular engine stays as the "character" mode)
- ⏳ [M] Explicit SIMD batching where profiling proves it (saturator, FDN)

## Phase 3 — UI framework ✅ language v1.0 · ✅ **UI v2 reference realized (D-022)**
- ✅ Design language v1.0 (docs/design 00–08) · calibrated SIGNAL field ·
  graduated meridian · tiered workspace · vector-only · high contrast
- 🔄 **UI v2 — reference design** (docs/design/09-TARGET-UI-V2.md
  is authoritative; build in this order, full ritual per milestone):
  - ✅ **M1** Containment + module-strip icon tiles (13 primitive-drawn
    glyphs, accent underline notch, corner enable point)
  - ✅ **M2** Header: centered preset-navigator field (name + FACTORY/USER · category subtitle), separate save field, drawn power pill for bypass; + reduced-motion toggle in the ... menu
  - ✅ **M3** Signal field: dual dB edges to the floor, 10-graduation
    frequency ruler (20…20k), neutral spectrum, accent EQ curve + ghost
    state (screenshot vesper-uiv2-m3.png)
  - ✅ **M2.5** Chain Order editor in the ··· menu (D-024): per-module
    move earlier/later/to start/to end + reset to default
  - ✅ **EQ band-focus panel** (D-025): band-selector strip + selected
    band's FREQ/GAIN/Q/DYN as full arcs (fixes cramped/characterless dials)
  - ✅ **M4** On-curve EQ node editing: draggable numbered nodes on the
    curve (drag = freq/gain · wheel = Q · dbl-click = band on/off · click
    empty near a freq = enable nearest off band), live freq/gain readout,
    two-way band-selection sync with the panel (screenshot
    vesper-m4-eqnodes.png). **UI v2 reference fully realized.**
    ✅ Follow-up done (D-041): right-click node → Type / Dynamic / Flatten /
    Remove menu — the graph is a complete band editor
  - ✅ **M5** Performance strip: contained band, mini arcs with
    values-under-arcs, telemetry as 3 labeled rows (LUFS/CORR/GR) each with
    a meter bar (CORR bipolar centre-zero, GR fills from the right)
  - ✅ **M6** Sync-aware value displays + Motion rate-sync (D-035): the
    Delay TIME and Motion RATE arcs read the division (`1/8 SYNC`) while
    synced; Motion gained SYNC + DIVISION (shares the delay's musical
    table, computed from host BPM). Macros read `%`, MORPH label becomes
    its value while morphing. **UI v2 M1–M6 complete.**
- ✅ **Display-aware default size** (D-050): first run scales to ~half the
  screen height (clamped 1.0–1.75×), then remembers the user's choice —
  1.59× on a 2160 px panel instead of a fixed 1080×680
- ✅ **Typography** (D-050): real DIN-like display face + real UI face,
  resolved from a preference list; synthetic `boldened()` retired, tracking
  tightened. Fixes the "looks AI-ish" reading
- ⏳ [M] Resize grip + per-DAW default-size memory polish
- ✅ **Subculture card language, part 1** (D-051): panel **header strips**
  with the module name, **‹ › steppers** with page dots for short choice
  lists (long lists keep the pick-list), redundancy cull (Options button,
  duplicate Settings, duplicate enable dot), drawn ··· affordance, telemetry
  row spacing
- 🔄 [M] **Subculture card language, part 2**: per-module curve visualisation
  inside the card, vertical segmented meter bars beside controls
  - ✅ `src/ui/CurveView.h` + `ModulePanel::setCurve()` (D-066); **Saturate**
    plots the real shaper with live Drive/Bias
  - ⏳ dynamics curves (Compress knee, Gate, Limit ceiling) — the component
    is generic, each needs its own pure `f(x)` from the DSP
  - ⏳ vertical segmented meter bars beside controls

## Phase 4 — Interaction & motion 🔄
- ✅ Bloom transition (rise + fade, 170 ms), hover light, station halos
- ✅ Motion engine (`ui/Motion.h`): one VBlank clock, vesperEase (true
  bezier solve), interruptible, reduced-motion aware — the only sanctioned
  animation path
- ✅ Reduced-motion *setting* in the ··· menu (M2 header work)
- ⏳ [M] Drag "magnetism": station snap preview while reordering
- ⏳ [M] Arc value springs (display value eases to target)
- 💭 [L] Per-module sky scenes: GR ribbon for dynamics, echo pulses for
  delay, bloom particles for reverb — each earns its place only if it
  *informs* (no decoration)

## Phase 5 — Creative features ✅ v1 · 🔄
- ✅ 17 Moves · musical Dice · M1–M4 macros · **Morph** over A/B (D-008)
- ✅ Macro assignment editor (D-020): right-click any arc → follow M1–M4,
  pin MIN/MAX travel, stop following; session-persistent, audio-thread-safe.
  ✅ Assignment count badges on the M knobs (D-041). Follow-up ⏳ [S]:
  per-target curve shaping
- ❌ **XY overlay pad** — shipped D-048, **REMOVED D-062** at my
  call ("i dont like it and it overcomplicates things"). Macros keep the
  M1–M4 knobs, assignment menu and count badges; the surface is gone.
  Do not rebuild without a new argument.
- ⏳ [M] Content-aware suggestions: rank presets by input spectrum/crest
- 💭 [L] Gesture record: capture a knob ride, replay as motion source

## Phase 6 — Preset system ✅ v1 · 🔄
- ✅ Search/tags/favorites browser, factory-in-binary, chain-order recall
- ✅ Browser rebuilt as a language Overlay: slides on the motion clock,
  bare-rule search, favorite/selection Points, typographic rows
- ✅ **Recents folder + audition lock** (D-042): last 8 loaded presets
  (newest-first, de-duped, persisted in `recents.json`) as a pinned,
  collapsible folder at the top; Audition toggle previews on single click
  without closing, Return/double-click commits
- 💭 [M] Cloud/shared preset exchange (architecture: presets stay files)

## Phase 7 — Performance ✅ baseline · recurring
- ✅ No allocations post-prepare, denormal flushing, lock-free UI taps
- ✅ **CPU budget** (D-045, refined D-049): offline render reports realtime factor for the
  whole chain and each module (empty 151×, all 13 modules 26×, heaviest
  Reverb 57×). Asserts the load-invariant RATIO of chain cost to an empty
  chain (absolute factors move with machine load); optimised builds only
- ⏳ [M] Paint profiling at 200 % scale; dirty-region audit
- ✅ **Startup budget** (D-045): instantiate+prepare 7.9 ms, editor open
  2.5 ms — against the <50 ms target

## Phase 8 — Accessibility 🔄
- ✅ Keyboard map, screen-reader titles, high-contrast, 200 % scale
- ⏳ [M] Full keyboard *editing* traversal audit (tab order, arc arrow-keys)
- ✅ **Announced value changes** (D-046): one formatter drives both the drawn
  numeral and the accessible value (units + sync/percent/choice overrides);
  asserted across the live editor — **103/103** sliders named, valued and
  keyboard-focusable. Caught the MORPH slider being neither
- ⏳ [S] Reduced-motion mode (ties into Phase 4 engine)

## Phase 9 — Factory content ✅ 24, audited · 🔄
- ✅ Stability + category audit (D-023): 12-bucket taxonomy, shimmer/
  feedback/resonance caps, factory-revision marker refreshes stale
  installs
- ⏳ [M] Author against real stems per genre list (Ableton
  material); every preset must justify its Moves/macros mapping
- ✅ **Preset QA automation** (D-040): every shipped preset audited against
  the live parameter set (ids exist, values in range, D-023 stability caps)
  straight from `VesperData`, so the test sees the exact shipped bytes.
  Caught two presets with a raw `&` in their XML name that had never
  loaded; `factoryRevision` bumped so installed copies refresh

## Phase 10 — Professional QA ✅ harness · recurring
- ✅ 114 unit tests incl. the **knob harness** (D-023: every parameter of
  all 13 modules proves min-vs-max changes the output) · pluginval
  strictness 8 on every milestone · CI (Win/mac/Linux)
- ⏳ [M] Host matrix: Ableton, FL, Reaper, Bitwig, Cubase, Logic (AU), Studio
  One — scripted smoke sessions + state round-trips
- ⏳ [M] Sanitizer lanes (ASan/UBSan on Linux CI) — the DelayLine UB (D-005)
  is exactly what these catch
- ✅ **Automation continuity harness** (D-037): parameters stepped at block
  rate; block-edge curvature vs in-block curvature, with a negative control
  proving the harness can actually fail
- ✅ **Full-processor integration suite** (D-043): 36 checks driving the real
  VesperProcessor — rate/block matrices, every preset rendered, features
  end-to-end, state round-trip, bus layouts, latency honesty, editor layout
- ✅ **Automation torture**: random parameter storms while reordering, with a
  divergence (not magnitude) invariant — part of D-043
- ✅ **pluginval strictness 10** across 5 sample rates × 7 block sizes, plus a
  **Debug build** run of both suites (JUCE assertions + leak detector active)

## Phase 11 — Beta ⏳
- Private beta (producers across the genre list) · crash reporting opt-in ·
  structured feedback → this plan

## Phase 12 — Commercial release ⏳
- Code signing + notarization in CI · installers (Inno/pkg) · licensing
  (LICENSING.md design) · AAX via Avid/PACE · JUCE commercial license
  (removes splash) · trademark search on "Vesper" (BRANDING.md)

## Integrations (opportunistic)
- ✅ Ableton (VST3, in the VM) · standalone
- 📋 System-wide via Equalizer APO — needs the VST3 fork or a wrapper
  (docs/EQUALIZER-APO.md); VST3 staged at Documents\Vesper VST3\

## Phase 13 — Continuous evolution 💭
Analysis-driven move suggestions · intelligent gain-staging · modular ecosystem ·
hardware/MIDI-controller profiles · mobile companion. Architecture rule:
none of these may require rewriting Phases 1–2.

---

## Review cadence
End of every milestone: code / architecture / UX / UI / DSP / accessibility /
performance / memory / cross-platform review. Failures get tasks here, not
apologies. Temporary code gets a tracked task the moment it lands
(currently: none known).
