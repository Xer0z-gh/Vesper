---
name: steward
description: "Vesper's rules for driving a pull request to green. Covers which CI jobs can be reproduced in a cloud session (Linux) and which only from logs (Windows, macOS, pluginval), what to run before every push, and what never to do on this repo. Use when handling CI failures, review comments or merge conflicts on a Vesper PR."
---

# Driving a Vesper PR to green

## CI map (`.github/workflows/ci.yml`)

CI runs on PRs to `main`, pushes to `main` and `v*` tags.

| Job | What it does | Reproduce here? |
|---|---|---|
| Linux | build + ctest | Yes, with the commands in `CLAUDE.md` |
| Windows (MSVC), macOS (AppleClang) | build + ctest + collect the plugin | No: read the job log |
| pluginval (Windows, macOS) | strictness 8 on the VST3, `--skip-gui-tests` | No: read the job log |

- **Windows or macOS only:** fix from the first error in the log. MSVC and
  AppleClang reject some things GCC accepts; a missing include GCC pulled in
  transitively is the usual one. Say in the PR that only CI verifies the fix.
- **pluginval:** the log names the failing test (state restore, parameter
  fuzzing, threading, bus layouts, latency). Fix it, and add or tighten an
  integration check so Linux catches the same thing next time.

## Before every push

1. Test targets built and `ctest` green on Linux, with both suites' counts noted.
2. Touched the processor, the editor or `CMakeLists.txt`: full `cmake --build build`.
3. Read your own diff: no debug prints, no reformatted untouched lines, every
   doc that describes the change updated.

## Never on this repo

- Relax a tolerance, loosen an assertion, or skip or delete a check to get
  green. The automation harness carries a negative control because a suite
  that passes vacuously is worse than a red one.
- Change pluginval's strictness or flags in CI.
- Merge, mark ready for review, or push to `main`.

## Review comments

Tanner's comments are the spec. Do small asks directly. For anything that
changes the design language or a documented decision, reply with a proposal
first. When a comment corrects how you work rather than what the code does,
add a line to `CLAUDE.md` under Lessons in the same push.
