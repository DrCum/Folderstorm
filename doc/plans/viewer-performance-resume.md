# Performance work: shutdown handoff

Resumed by the user on October 24. Current checkpoint branch:
`codex/perf-sampling-fp-context`, based on PR 107 at
`14b9f8471bf3cb97de35601fc19b89de9e104705`. Checkpoint 14c is implemented and
passes 5,956 checks/1,474 frozen-update comparisons in each storage mode.
Checkpoint 14d implements the initial controller-mask fix found during those
tests. PRs 106 and 107 have completed automated review with no findings at their
published heads. Plan 14e's numeric context is implemented and tested: 791 guard
checks, 829 actual-math/application checks per storage mode, prior regressions
green, 248 complete-cost trials and isolated guard/capture timings retained.
Plan 14f covers explicit eligibility/current lifecycle/clock identity and owner
validation storage before live integration; it is not implemented yet.
The independent build agent resumed its monitoring under the user's October 24
instruction and retained 106/107. This chat does not control that monitor/cache.
The shutdown details below are history;
read the current roadmap and latest checkpoint plans before continuing.

Saved October 23, 2026 at the user's request before computer shutdown. Pause the
performance goal after publishing this checkpoint. Do not start further work or
scheduled runs until the user resumes it.

## Saved checkpoint

- Worktree: `C:\Users\Marty\Documents\ChatGPT\folderstorm 2\viewer-performance`.
- Current branch: `codex/perf-animation-constraint-padding`. Its PR is based on
  `codex/perf-readonly-animation-export`, commit
  `7e719af9dece7b3987e1c378663a41bd54a8a5d2` (PR 104).
- PR 104: https://github.com/DrCum/Folderstorm/pull/104. Export/size queries now
  quantize a local position copy instead of modifying shared live curve keys.
- This checkpoint zero-initializes both fixed 16-byte constraint-name buffers.
  Existing field widths, truncation, names and decoded semantics are preserved;
  previously uninitialized trailing bytes become deterministic zero.
- Current production change is only those two array initializers. Focused tests,
  results and the next plan accompany it. No live worker dispatch is enabled.

## Validation already completed

- New constraint-name test reproduced nonzero padding with the old serializer.
- Fixed serializer: 11,270 name-padding/decoding/layout/failure checks passed in
  each of map and contiguous storage modes.
- Existing read-only serializer regression: 896 checks and 10,000 concurrent
  owned samples passed in each storage mode. Five frozen packets remain exact.
- Complete keyframe source and new test passed standalone Tracy/contiguous
  syntax checks. Native runs used fresh character/controller/keyframe sources,
  MSVC x64 AVX2, optimized precise floating point, assertions enabled and cached
  dependencies read only. Scratch results are in the sibling
  `performance-validation` directory (`*14b*` logs and executables).
- A final test-only assertion was relaxed to allow a future valid early return
  on rejected output; current successful runs take the same two-field path.
- Full viewer builds, export/re-open workflows and in-world acceptance remain
  pending. PR 104 and this checkpoint still need review/acceptance follow-up.

## Resume sequence

1. Inspect git status, branch/head and attached PRs before changing anything.
   Resolve the current checkpoint PR by its branch rather than guessing a number.
2. Read `viewer-performance-roadmap.md`, checkpoint 14's audit, checkpoint 14b's
   results and `viewer-performance-14c-keyframe-phase-contract.md` in this folder.
   Checkpoint 14c is planned and has no implementation yet.
3. Review any build-agent results and PR comments. Keep fixes scoped to their
   own checkpoint; do not merge or silently combine newer feature branches.
4. Begin 14c on a new `codex/perf-...` branch from this checkpoint. Preserve the
   legacy timing arithmetic and callback/constraint/pose ordering, validate with
   a frozen actual-update oracle, keep default playback serial, then publish a
   separately testable PR and write the following plan.

## Boundaries and pending user evidence

- Include the frozen current feature stack through PR 88, baseline
  `d0f05af90f4ed788cb59a5b2d91dd405ed06717f`. Keep the primary feature checkout and
  shared `D:\Folderstorm-stack-build` cache untouched by this work.
- Target crowded places and steady FPS on Ryzen 5 3600 / RTX 3060 12 GB.
  Do not claim a viewer FPS improvement from synthetic sampling timings.
- The local building agent handles exact-head builds. Defaults are x64
  `ReleaseFS_open_AVX2`, Nightly, NSIS, contiguous keys OFF for the map control.
  Retain separate completed builds; do not resume recurring build checks.
- `viewer-performance-questions.md` records deferred scene, camera, settings,
  matched Tracy capture and visual/lifecycle debugging requests.
- For this checkpoint, export and re-open an animation using short collision
  volume names and a ground constraint; verify intended names/behavior and
  deterministic repeated output. No account or in-world operation was performed.
- Before any future live worker, establish immutable key ownership, explicit
  subclass eligibility, actual current lifecycle/frame/edit identity, worker
  floating-point context, and a measured independent overlap window. A generation
  check alone cannot repair a concurrent write or establish those guarantees.

No long-running build/test process or automation was intentionally left running
at this checkpoint.
