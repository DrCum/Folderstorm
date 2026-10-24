# Performance work: current handoff

Updated October 24, 2026. The user resumed the performance program; the October
23 shutdown pause is historical. The objective remains active and incomplete.

## Current checkpoint

- Worktree: `C:\Users\Marty\Documents\ChatGPT\folderstorm 2\viewer-performance`.
- Branch: `codex/perf-motion-query-shadow`, based on PR 110 at
  `4adf6a948e32927afbafea78f5e8511ba552297f`.
- Plan 14f is implemented: ordinary-factory permission, explicit ordinary remote
  character permission, independent current lifecycle/frame time/count/skeleton/
  appearance/asset/binding context, exact current-input validation and direct
  fallback. No production worker dispatch or controller reordering is enabled.
- Its private authority resets on base copy/assignment. Owner-only binding
  snapshots retain bounded joint-state references; jobs never retain live scene
  pointers. Identity never authorizes dereferencing a dead motion/character.
- Plan 14f is published as PR 109 and its automated review completed with no
  findings at exact `e08cf82050`. Plan 14f1 is now implemented: stable key names,
  map-authoritative typed priority lookup and unchanged priority/publication order.
  Plan 14f1 is published as PR 110; its automated review completed with no
  findings at exact `4adf6a948e`. Both checkpoint PRs are attached to this chat.
- Plan 14g's controller/avatar audit and concrete implementation sequence are
  written. This branch currently contains planning only, with no predictor,
  observer, setting or production worker caller yet. Begin the nonmutating
  clock/query contract next, then complete the bounded shadow observer before
  publishing its separately testable PR. Do not guess that PR's number.
- The frozen feature baseline through PR 88 is still
  `d0f05af90f4ed788cb59a5b2d91dd405ed06717f`; preserve it and published history.

## Evidence already obtained

- 1,203 actual current-owner checks in each key-storage mode, including three
  current applications and 25 stale fallbacks in the mutation matrix. Real
  factory/controller, copy/assignment, destruction, same-ID replacement, binary
  publication, independent clock updates and counter exhaustion are covered.
- Prior suites pass in each mode: 5,956 phase checks / 1,474 frozen updates;
  109,806 owned checks / 24,193 exact poses; 1,734 batch checks; 829 actual numeric
  checks, including 116 reproduced unguarded mismatches.
- Allocation probes: cold preparation two allocations; warm preparation zero;
  current application adds no allocations over direct playback. Both playback
  paths still make two hand-pose name allocations per update. Raw allocation
  reports and 81 owner timing rows are in `doc/plans/results/current-motion-*`.
- The 14f1 fix passes 171 actual hand publication/lifetime/consumer checks in
  both modes; prior phase and current-context suites remain green. Warm direct
  and owned updates now make zero replacement-new calls, while post-removal
  map insertion remains explicit (96 calls / 5,632 bytes for 32 republications).
  Four paired before/after timing runs total 324 rows / 324,000 calls, with raw
  reports and verified parent-source/executable hashes in `hand-pose-name-*`.
- Native fresh-source MSVC x64 AVX2 runs and independent Tracy/contiguous source
  checks passed. Cached dependencies were read only, scratch outputs are in the
  sibling `performance-validation` folder. Full viewer builds are the other
  chat's responsibility. No FPS or rendered acceptance claim is established.
- PR 108 automated code review completed without findings at exact `eea3c55`.
  The independent builder retained PRs 105-108 separately; latest 108 version
  `7.2.5.82247` passed full build and 23 focused checks. Its final receipt is
  `performance-worker-final-20261024-062156-pr108.json` in the parent workspace.
  That build is not evidence for the newer 14f source.

## Next sequence

1. Inspect current status/head/PR state and preserve any newer work. Read 14g's
   concrete implementation sequence; begin the nonmutating clock/query contract.
2. Review PR 109/newer checkpoint comments and independent build evidence.
   Preserve published heads; keep any corrective fix scoped and propagate its
   dependency explicitly. Do not treat older full builds as new-head validation.
3. Continue `viewer-performance-14g-shadow-query-prediction.md`: audit controller
   arithmetic/order, add nonmutating prediction and shadow comparisons, establish
   real overlap before any off-by-default bounded worker trial. Bound owner
   bindings and job buffers together. Never manufacture overlap by changing
   animation order or adding an animation frame of latency.
4. Record deferred acceptance/questions in `viewer-performance-questions.md`;
   missing captures do not block independent source work. Quality/stability and
   whole-frame CPU/frame-time improvement remain the objective.

## Build and ownership boundaries

- Primary feature/build checkout and shared cache are not this code worktree.
  Do not switch/edit `C:\Users\Marty\..Projects\Folderstorm-all` or configure/
  build/write `D:\Folderstorm-stack-build`. Another chat owns private builds,
  monitoring, installer archives, packaging and cache/source ownership.
- Its current accepted private setup is x64 AVX2 OpenSim Nightly Velopack, Tracy
  OFF and contiguous keys OFF. Existing matched profiling/storage pairs remain
  preserved. Do not control its monitor or start additional full builds here.
- Check actual live process ownership before compiling probes using read-only
  dependencies. Sandboxed `Get-Process` cannot see other chats' processes; a
  missing sandbox result alone does not prove a worker has stopped. An approved
  full process inventory can verify ownership when necessary.
- Do not merge, release, upload artifacts, install or operate logged-in viewer
  accounts. Do not message another chat without explicit human authorization.
- Target Ryzen 5 3600 / RTX 3060 12 GB and crowded steady FPS, without lowering
  quality. Runtime crowd/visual/lifecycle and other-platform acceptance remain
  pending; no automatic live login or capture has been performed.
