# Vesper — User manual

## The 30-second tour

1. **Load a preset** — click the preset name (or press `P`). Presets live in
   category folders: click a folder to open it (the loaded preset's folder
   opens by itself), and your last 8 loads sit in a **Recents** folder pinned
   at the top. Type to search — every matching folder opens. Click a preset's
   dot to favorite, double-click the preset to load. Turn on **Audition** to
   hear presets on a single click without the browser closing; Return or
   double-click commits and closes. The library is two panes: pick a
   category on the left (All · Recents · Favorites, then every category with
   its count), browse its presets on the right. Searching spans everything.
   **New here? Pick the Showcase category.** It holds one preset per module,
   each deliberately obvious, so you can hear exactly what every section
   does before you start combining them.
2. **Shape the chain** — the rack shows all thirteen modules in processing order.
   Click a card to open it, click its dot to switch it on/off, **drag it
   sideways to reorder** (audio never clicks).
3. **Commit or compare** — `X` flips A/B, ⇥ copies the current state to the
   other slot, `Ctrl+Z` undoes anything, including preset loads and dice rolls.

## Modules

| Module | What it does |
|---|---|
| **Gate** | Gate (hysteresis + hold) or 2:1 downward Expander. Range sets how far closed it falls. **Sidechain** switch keys it from the external input. |
| | *Sidechain in Ableton:* drop Vesper on the track, then in the device's title-bar routing chooser (triangle) pick the **Sidechain** input and select the source track. Enable the SIDECHAIN switch in Compress or Gate. |
| **EQ** | 6 bands (bell, shelves, HP/LP, notch), each with a **DYN** knob: the band listens to itself and shifts gain by up to ±18 dB when its energy rises above its own average — negative DYN tames flare-ups (de-ess, resonance control), positive opens the band. No threshold to set. With the EQ panel open, its response curve overlays the analyzer. **LIN PHASE** (right of the band strip) renders the static curve with zero phase shift — ideal on parallel buses and masters; it adds ~21 ms of reported (compensated) latency, and DYN is inactive while it's on. |
| **Filter** | Clean 12/24 dB SVF, Ladder, Acid, Comb, Formant. Drive pushes into the filter's own nonlinearity — on the Clean models it stays perfectly clean at 0 dB and only starts adding harmonics once you turn it up. Every model is level-compensated as Resonance rises, so sweeping resonance changes tone without lunging at your ears. |
| **Compress** | Downward + upward in one. Detector: Peak/RMS; Style: Clean / Punchy / Vintage (program-dependent release). **Sidechain** switch makes it duck to the external key. |
| **Multiband** | 3-band Linkwitz-Riley (24 dB/oct, phase-matched) compressor: per-band threshold/ratio/gain, shared attack/release. |
| **Saturate** | 17 algorithms (incl. Soft/Hard Clip). Tone tilts *what gets saturated* (not the output EQ); Bias adds asymmetry; Trim rebalances. Oversampling (header, 1–16×) applies here. |
| **Transient** | Attack/Sustain ±100 %, three detector speeds. |
| **Motion** | Chorus · Ensemble · Doubler · Flanger · Phaser · Vibrato · Tremolo · Rotary. Spread is stereo offset; Feedback applies to flanger/phaser. **Sync** locks the rate to the host tempo via the Division selector — the RATE arc then reads the division (`1/8 SYNC`). |
| **Pitch** | Micro (stereo detune) · Octave Up/Down · Fifth · Wide Double (detuned Haas double). Granular crossfade engine — zero added latency. |
| **Delay** | Sync (with dotted/triplet) or free ms. Color: Digital/Tape/Analog; Wow only breathes in Tape. Feedback >100 % self-oscillates — ride the Mix. |
| **Reverb** | Room / Hall / Plate / Bloom (FDN) + **Spring / Cathedral / Reverse** (convolution — Reverse swells into the hit) + **User IR**. **Shimmer** adds an octave-up voice in every space. **LOAD IR** takes any .wav/.aiff/.flac impulse response (auto-resampled, up to 10 s) and switches SPACE to User IR; the file is remembered with your session, and if it ever goes missing the button reads IR MISSING and the reverb falls back to Cathedral rather than going silent. Lo Cut keeps the tail out of your low end. |
| **EQ curve** | With EQ selected, drag the numbered nodes on the graph: drag = freq/gain, wheel = Q, double-click = band on/off, **right-click a node** for Type / Dynamic / Flatten gain / Remove. Nodes carry no numbers — the band panel below is the precise editor. |
| **Match EQ** | Bottom of the EQ panel. Play a **reference** tone through Vesper and click **LEARN REF**; play your own track and click **LEARN SRC** (the REF/SRC dots light when each has enough audio). **MATCH** designs a linear-phase filter that pushes your source's tone toward the reference and turns it **ON**; its dashed curve shows on the analyzer. **AMOUNT** dials the correction 0–100 %, **SMOOTH** sets how finely it traces (fractional octave — lower = more detail), **MAX** caps the boost/cut. **CLEAR** removes it. **REF A / REF B** keeps two references side by side — learn a second into B and flip to compare matches without re-learning. **TAP IN / TAP OUT** picks what LEARN listens to: the raw plugin input, or your processed output ("make my finished chain sound like this"); the filter itself always sits at the input, so with TAP OUT a heavily nonlinear chain makes the match approximate. Match adds latency (linear-phase, auto-compensated) and matches *tone, not level*. |
| **Stereo** | Width, M/S gains, Haas, pan, and **Mono <** (folds lows to mono below the set frequency). |
| **Limit** | Lookahead brickwall. Gain pushes in, Ceiling caps out. **True Peak** (default on) makes the detector answer the reconstructed analog waveform, so inter-sample overs never pass; Soft Clip adds a safety stage at the ceiling. |

**Signal flow extras:** IN/OUT trim and global **MIX** (latency-compensated —
100 % wet nulls against the chain, lower it for instant parallel processing).

## Creative tools

- **Moves** (header): one-gesture transformations — *More Punch, Wider,
  Brighter, Air, Vintage, Aggressive, Smooth, Glue, Loud, Dreamy, Huge,
  Intimate, Clean, Dirty.* Each is one undo step.
- **Dice** (`R`): musical randomization of the **selected module** (whole
  chain if none focused). It never touches your gain staging or bypass
  states, and stays inside genre-sane ranges.
- **Macros (M1–M4)**: right-click any knob → *Follow macro* → pick M1–M4.
  The macro now drives that parameter across its full range; set the knob
  where you want an endpoint and choose *Macro travel: MIN/MAX is here* to
  narrow the ride. Assign as many parameters as you like to one macro —
  then automate the single macro in your DAW. *Stop following* removes it.
  Assignments save with the session. Each M knob shows a small badge with
  how many parameters it currently drives.
- **Morph** (footer): blends *every* parameter from the live state (0) to
  the other A/B slot (1). Load one preset, press ⇥ to copy it to B, load a
  second preset, then turn Morph to play the space between them.

## Every knob

drag = coarse · **Shift** = fine · wheel = step · double-click = default ·
right-click = type a value · hover = live readout.

## Keyboard

`Ctrl+Z` / `Ctrl+Shift+Z` (or `Ctrl+Y`) undo/redo · `P` presets ·
`X` A/B · `B` bypass · `R` randomize · `[` `]` prev/next preset ·
`1`–`0` select module (in rack order).

## Presets on disk

`<user app data>/Sable Audio/Vesper/Presets/{Factory,User}` — plain XML
(`.vpreset`), safe to edit, share, or version-control. Only non-default
parameters are stored. Favorites live in `favorites.json` alongside.

## Settings (···)

Everything lives in the **···** menu in the header — Interface (Dark/Light,
UI scale 75–200 %, High contrast, Reduced motion), Signal chain (Chain
order), and Session (**Reset to Init**, plus **Audio / MIDI settings** when
you run the standalone). Nothing is hidden behind the standalone window's
own Options button, which your DAW does not show.
The window sizes itself to your screen the first time it opens, then
remembers whatever scale you pick.

Oversampling (1–16×) stays in the header rather than the menu, because it is
a sound decision, not a preference.
