---
name: vesper-inbox
description: "Take one Vesper task from the GitHub inbox (open issues labelled `claude` on Xer0z-gh/Vesper) to a verified draft PR, or answer it if it is a question. Use when asked to check the inbox, work issue #N, or do the next task, and when a routine fires with this skill. With an issue number, works that issue; with none, tends open PRs first and then takes the oldest actionable issue."
---

# Vesper inbox

One task per run. `CLAUDE.md` is the contract; this is the procedure. The repo
is Xer0z-gh/Vesper. Claude and Tanner both post as Xer0z-gh: a comment ending
in the Claude Code footer is Claude's, any other comment is Tanner's.

Reach GitHub through the GitHub MCP tools (`mcp__github__*`); a local session
without them uses the `gh` CLI. The first failed or denied call is the answer:
stop and say GitHub is unreachable in one line. Never retry it, try variants,
or disable the sandbox.

## 1. Tend before starting

Skip this step when given an issue number.

List open PRs labelled `claude`.

- One has failing CI, a merge conflict, or a comment from Tanner newer than its
  last commit, and no later Claude comment already reports it blocked: work
  that PR under the steward skill, then stop. Finishing beats starting.
- Three or more are open and waiting on Tanner: stop. Say so in one line.

## 2. Pick the issue

Given a number, take that issue. Otherwise list open issues labelled `claude`,
oldest first, and take the first one that none of these rule out:

- An open PR already references it (`#N` in its title or body).
- A PR for it was closed without merging and Tanner has not commented on the
  issue since: that attempt was rejected, so wait for direction.
- It has `needs-tanner` and no comment from Tanner after the last Claude comment.
- Its latest Claude comment contains `<!-- vesper-inbox:pickup -->` and is
  less than 12 hours old: another run has it.

Nothing left: stop without commenting. Say "inbox empty" in one line.

## 3. Claim it

Post one comment: `On it: https://claude.ai/code/${CLAUDE_CODE_REMOTE_SESSION_ID/#cse_/session_}`
(resolve it with `echo`; local sessions write "On it (local session)"),
followed by the marker `<!-- vesper-inbox:pickup -->` and the footer. If the
issue has `needs-tanner` and Tanner has answered, remove that label.

Then start the baseline build as a background command, before reading any code:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target VesperTests VesperIntegration
```

## 4. Understand

- Read the issue and every comment. Issue-form fields render as `### What
  should change?`, `### Done when`, `### Area`, `### Mode`.
- **A question, not a change:** answer on the issue with file:line evidence,
  add `needs-tanner`, stop.
- **Mode "Plan first"** and no go-ahead from Tanner yet: post the plan (files,
  approach, the checks that will prove it, open questions), add
  `needs-tanner`, stop.
- Read what governs the area. DSP: `docs/DSP-DESIGN.md` and the module's
  header. UI: `docs/design/` (`07-TOKENS.md` first, then
  `09-TARGET-UI-V2.md`). Presets: `src/core/PresetManager.cpp` and the preset
  QA in `tests/IntegrationMain.cpp`. Structure: `docs/ARCHITECTURE.md`.
- Ask instead of guessing (post the question, add `needs-tanner`, stop) when the
  task conflicts with a documented decision or the design docs, adds a UI
  surface, changes how a factory preset sounds, removes a feature, or two fair
  readings lead to different code.

## 5. Change

- Work on the branch your session was given. With none, create
  `claude/issue-<N>-<slug>` from the latest `main`.
- Make the smallest change that fully does the job. List drive-by ideas in the
  PR instead of doing them.
- Every behaviour change gets a check that fails without it: a unit check for
  a DSP class, an integration check for anything that goes through the
  processor. When unsure it can fail, run it once against the old code.
- Grep for every doc and comment that describes what changed and fix them. Add
  a line under `[Unreleased]` in `CHANGELOG.md` for anything a user would notice.

## 6. Verify

```bash
cmake --build build --target VesperTests VesperIntegration && ctest --test-dir build --output-on-failure
```

- Count each suite's `PASS  ` and `FAIL  ` lines from this run. Explain any
  count that moved.
- Touched the processor, the editor or `CMakeLists.txt`: also `cmake --build build`.
- Red and you cannot fix it: do not present the work as done. Open the PR with
  the failure stated first, or ask on the issue.

## 7. Ship

- Commit in the repo's style (`CLAUDE.md`), push, open a **draft** PR against
  `main`, and label it `claude`. Body:
  - `Closes #N`
  - What changed and why, in a few lines
  - Evidence: the Linux build, unit and integration counts from this run, and
    what only CI can prove (Windows, macOS, pluginval)
  - Decisions for Tanner, each with a recommendation
- Edit the pickup comment to add the PR link instead of posting a second one.
- Subscribe to the PR's activity and drive it to green under the steward skill.
