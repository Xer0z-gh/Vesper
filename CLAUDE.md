# Vesper: notes for Claude

Vesper is a 13-module JUCE 7.0.12 audio plugin (C++20, GPLv3), built as a
long-lived flagship. `docs/ROADMAP.md` is the plan, `docs/ARCHITECTURE.md` the
structure, `docs/design/` governs every pixel.

## Build and test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release           # ~1 min cold, fetches JUCE
cmake --build build --target VesperTests VesperIntegration   # ~4 min cold on 4 cores
ctest --test-dir build --output-on-failure                   # ~30 s, both suites
```

- Cloud sessions: `.claude/hooks/session-start.sh` installs JUCE's Linux deps.
  Start the build as a background command early; it is the slow part.
- Local Windows: same commands with the portable MinGW toolchain on `PATH`
  (`docs/BUILDING.md`).
- `cmake --build build` with no target also builds the VST3 and Standalone. Do
  it whenever you touch the processor, the editor or `CMakeLists.txt`.
- Each suite prints one `PASS  name` or `FAIL  name` line per check. Take
  counts from your run (`grep -c '^PASS  '`), never from the README. Last
  verified: 142 unit and 49 integration checks, 0 failures, on Linux.
- Windows, macOS and pluginval (strictness 8) run only in CI
  (`.github/workflows/ci.yml`). A Linux run never proves them; cite the CI run.

## Rules specific to this repo

1. **Claims match the record.** README, CHANGELOG, docs and code comments state
   only what the code, test output or a cited record supports. When behaviour
   changes, grep for every place that describes it and fix them in the same PR.
2. **Architecture.** `src/dsp` never sees a parameter object. No locks,
   allocation or waiting on the audio thread after `prepare`.
   `src/core/Params.h` is the only source of parameter ids.
3. **Stable ids.** Module ids and parameter ids are a contract with every saved
   session and preset: append, never rename or remove. Bump a
   `juce::ParameterID` version only for a breaking range change.
4. **Factory presets.** Changing anything in `presets/factory/` means bumping
   `factoryRevision` in `src/core/PresetManager.cpp` so installed copies refresh.
   The integration suite audits every shipped preset.
5. **UI.** `docs/design/` is law, `07-TOKENS.md` the machine contract. No ad-hoc
   colours, sizes or timings; animation goes through `src/ui/Motion.h`.
6. **Formatting.** The tree is not clang-format clean (hundreds of diffs per
   file). Never run clang-format over a file; match the surrounding code by hand.
7. **Decisions.** Decisions are numbered D-001 to D-067, but the decision log,
   and the "10-AUDIT" doc that comments cite, are not in this repo. Cite a
   D-number only where the repo already ties it to that topic. Put a new
   decision under a "Decision" heading in the PR body; Tanner numbers it.
8. **Tests are never loosened to get green.** No relaxed tolerances, no skipped
   or deleted checks. A new behaviour gets a check that fails without it.

## Working with Tanner

- **Inbox:** open GitHub issues labelled `claude`; the "Task for Claude" issue
  form applies the label. Tanner can also dispatch directly: `vesper <task>` in
  the agents view, or `/vesper-inbox 12` in any session.
- **Outbox:** one draft PR per task. The body carries the evidence: counts from
  your run, and what only CI can prove.
- **Questions:** when a task needs a product or design call, post one comment on
  the issue with two or three options and your recommendation, add
  `needs-tanner`, and stop. Claude and Tanner both post as Xer0z-gh, so the
  answer is any later comment without the Claude Code footer.
- **Never:** push to main, merge, mark a PR ready for review, or force-push a
  branch you did not create.
- **Commits:** the subject states what is now true ("The chain order is set from
  a menu, and that shipped"), the body gives file:line evidence.
- **Autopilot:** routines run `/vesper-inbox` (one issue per run) and
  `/vesper-audit` (weekly truth audit). Routines clone `main`, so a change to
  these skills or this file reaches them only once merged.

## Lessons

Standing calls and corrections from Tanner. Append one imperative line per
correction, in the same PR as the fix.

- Do not rebuild the XY overlay pad (removed D-062, "it overcomplicates
  things"). Propose any new UI surface on an issue before building it.
