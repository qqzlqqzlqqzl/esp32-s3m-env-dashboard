# AI Backlog And Issue Closure Protocol

This document is for future AI agents. It defines how GitHub issues are created,
updated, reviewed, and closed for this project.

## Roles

Main agent:

- May create issues when it finds a real gap or residual risk.
- May implement fixes and add evidence.
- May comment with implementation evidence.
- Must not close an issue it implemented unless an independent verifier has
  produced explicit passing evidence.

Curator subagent or independent verifier:

- Reviews issue acceptance criteria against committed code, docs, tests, and
  hardware evidence.
- May close an issue when all acceptance criteria are met.
- Must leave the issue open and comment exact gaps when evidence is incomplete.
- Must not modify firmware or test artifacts while acting as curator.

User:

- May explicitly authorize destructive tests such as confirmed log clear.
- May override priority or acceptance criteria.

## Required Issue Fields

Every issue should include:

- Priority: `P0`, `P1`, or `P2`.
- Context: why this matters for the monitoring station.
- Acceptance criteria: concrete checks, not vague intent.
- Reproduction or verification commands when known.
- Residual risk: what cannot be proven yet or needs a longer test window.

## Evidence Required Before Closure

Each closed issue must have a comment with:

- Commit hash or branch state reviewed.
- Commands run and PASS/FAIL output.
- Hardware target when hardware evidence is used, for example `COM20`,
  `COM9`, and device IP.
- Whether destructive endpoints were avoided or explicitly authorized.
- Residual risks that remain after closure.

Examples of acceptable evidence:

- Host test output such as `[PASS] 5 time continuity tests`.
- Hardware patrol output such as `[PASS] hourly QA patrol passed`.
- Browser UX metrics from `tools/browser_ux_check.mjs`.
- Power metrics from `tools/power_regression_check.py`.
- Direct API output summarized from `/api/status`, `/api/health`,
  `/api/history`, or `/api/log.csv`.

Examples of insufficient evidence:

- "Looks OK" without command output.
- Main agent statement with no independent review when the main agent made the
  change.
- A source-contract test when the acceptance criteria require physical hardware
  behavior and no residual risk is documented.
- Destructive log clear verification without explicit user approval.

## Closure Workflow

1. Main agent checks open issues before choosing new work.
2. Main agent implements a scoped fix and runs local/hardware verification.
3. Main agent commits and pushes the result.
4. Main agent asks a curator subagent to review the specific issue.
5. Curator compares issue acceptance criteria to committed evidence.
6. Curator either:
   - comments evidence and closes the issue, or
   - comments the remaining gaps and leaves it open.

## Destructive Or Long-Window Issues

Some issues cannot be closed in a normal short session:

- Confirmed `POST /api/log/clear?confirm=1` requires explicit user approval.
- 24h/72h soak requires elapsed runtime evidence.
- Physical BOOT/LCD verification requires manual observation or a GPIO/display
  fixture.

For these, leave the issue open with the current best evidence and the exact
next safe test.

## Priority Meaning

`P0`:

- Blocks safe unattended development or can hide major device failure.
- Should be handled before feature work.

`P1`:

- Important reliability, UX, power, storage, or hardware validation gap.
- Should be handled proactively when it can be tested safely.

`P2`:

- Process, documentation, or polish that improves future agent quality.
- Should be completed when it reduces repeated mistakes.
