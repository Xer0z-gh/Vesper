---
name: vesper-audit
description: "Truth audit for Vesper. Checks every factual claim in README.md, CHANGELOG.md, docs/ and code comments against the source and a live test run, fixes wording drift in one docs-only draft PR, and files issues for real gaps. Use when asked to audit the docs or claims, check the README is true, or when the weekly routine fires. Never changes behaviour."
---

# Vesper truth audit

Most of this repo's recent commits make a claim match the record. This skill
does that sweep on purpose instead of by accident. Code and test output are the
record; prose is what gets corrected.

Reach GitHub through the GitHub MCP tools (`mcp__github__*`); a local session
without them uses the `gh` CLI. The first failed or denied call is the answer:
never retry it, try variants, or disable the sandbox. Finish the audit, report
the findings in your reply instead of a PR, and say GitHub was unreachable.

## 0. Guard

If an open PR titled `Audit: ...` exists, stop and say so in one line. One
audit PR at a time.

## 1. Build the record

- Start the build and run both suites (commands in `CLAUDE.md`). Test counts
  come from this run only.
- Pull the facts the docs repeat from the source: module count and names
  (`src/core/Params.h`), saturation algorithms (`src/dsp/Saturation.h`),
  factory presets (`presets/factory/*.vpreset`), UI scale range, true-peak
  taps, reverb spaces, and whatever else a claim under review depends on.

## 2. Check every claim

Files: `README.md`, `CHANGELOG.md`, everything in `docs/` (design docs
included), and comments in `src/` that describe behaviour or planned work.
Scoped runs ("audit README.md") check only what was named.

For each factual claim (a number, a behaviour, an instruction, a status, a
link), find the code, test or record that proves or refutes it. Also check:

- relative links and image paths resolve
- a D-number means the same topic everywhere it is cited
- comments that describe planned work which has shipped, or behaviour the
  code does not have

Dated history is not drift. "114 unit tests" recorded under a past milestone is
fine; the same number presented as the current state is not.

## 3. Sort each finding

- **Drift** (the code is right, the prose is stale): fix the prose.
- **Gap** (the prose describes something the product should do and does not,
  or the check found a bug): one issue per gap labelled `claude`, after
  searching open issues for a duplicate.
- **Call** (either side could be right, e.g. the README sells a feature the UI
  does not have): an issue labelled `claude` and `needs-tanner`, with both
  options and a recommendation.

## 4. Ship

- One docs-only draft PR titled `Audit: YYYY-MM-DD`, labelled `claude`. Group
  commits by file or theme, in the repo's commit style (subject says what is
  now true, body cites file:line).
- The body is a table (claim, where, record, fix), then the issues filed, then
  the test counts from this run.
- Nothing found: no PR and no issues. Say "clean" in one line.
