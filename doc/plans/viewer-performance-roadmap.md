# Viewer performance program

Started October 22, 2026. Owner: performance work in this chat.

## Objective and first target

Improve viewer frame times while preserving rendered quality, behavior and
stability. The first target is steady performance around many avatars on a
Ryzen 5 3600 and RTX 3060 12 GB. Loading and travel smoothness are secondary.
CPU utilization alone is not a success metric.

## Isolation and baseline

- Dedicated checkout: `viewer-performance`, separate from the feature and build
  checkouts. Do not use or reconfigure their shared build cache.
- Frozen branch: `codex/perf-baseline`, initially
  `d0f05af90f4ed788cb59a5b2d91dd405ed06717f`.
- Includes main `8a217b6b72c3aae7552a660c6d2dcced1c4923dc`, the retained
  through-87 integration `b41f8e03f8e18ebf5c5bab52d0011561a7e1ebfc`, and
  PR 88 head `85f620154fe0da09d2a64d7fcc0f9f352f358c49`. Verify all open
  feature heads 79 through 88 are ancestors before publishing.
- Performance branches use `codex/perf-*`. Each behavior change gets its own
  PR and build/test instructions. Target the preceding performance checkpoint
  when dependent; use the frozen baseline when independent. State dependencies
  explicitly, including full baseline/head SHAs.
- Do not merge PRs, publish releases, alter feature branches, restart automatic
  build monitoring, or operate the user's logged-in viewer. A separate local
  building agent will build selected PR heads.
- New feature changes are integrated only as an explicit baseline update, never
  silently bundled into a performance change. Preserve published history.

## Delivery loop

1. Write a small implementation plan with a source-based hypothesis, ownership
   boundaries, expected benefit, correctness checks and rollback criteria.
2. Implement and run focused tests, including numerical equivalence and
   lifecycle coverage where concurrency is involved. Check the actual code path.
3. Review the diff for races, stale references, shutdown behavior, memory bounds,
   changed visual results and dependencies. Record validation honestly.
4. Commit, push and open a PR. Include a narrow Windows acceptance checklist,
   build scope and what still requires measurement. Keep completed checkpoints
   available individually. A passing unit test is not proof of an FPS gain.
5. Write the next plan before its implementation. Continue independent work
   while hardware results are pending. Do not stack unvalidated concurrent scene
   mutations just to make progress. Record pending questions in
   [viewer-performance-questions.md](viewer-performance-questions.md).

## Checkpoints

| Stage | Work | Exit evidence |
| --- | --- | --- |
| 0 | Freeze feature baseline; publish this roadmap and measurement protocol. | Ancestor checks and clean performance-only diff. |
| 1 | Fill gaps in existing avatar profiling; provide a repeatable capture and comparison workflow. Reuse existing timers/Tracy rather than add per-avatar logging. | Focused instrumentation checks and explicit instructions for a profiling-enabled build. No visual or scheduling changes. |
| 2 | Remove demonstrable redundant CPU work in avatar update and skinning preparation. Inspect existing caches before adding new ones. | Production-path equivalence and operation-count evidence; crowd benchmark pending local capture. One independently reversible optimization per PR. |
| 3 | Extract a measured expensive numerical kernel from live scene objects. Retain a serial reference path. | Equivalent results for normal and boundary inputs, bounded temporary memory, cancellation/lifetime design. Extraction alone makes no speedup claim. |
| 4 | Run that kernel on a bounded existing or dedicated worker pool with immutable inputs and versioned results. Begin with remote avatars if the profile supports it. | No concurrent scene mutation, stale-result rejection, safe logout/shutdown, serial fallback, measured frame-time gain without quality loss. Experimental path remains opt-in until accepted. |
| 5 | Investigate visibility/detail calculations, geometry preparation, particles/flex and terrain decoding in that order as measurements warrant. | Each has its own plan/PR; shared scratch buffers, renderer state and scene lifetime are resolved before parallel execution. |
| 6 | Tune worker admission for the six-core target; repeat mixed-workload and stability validation. | No oversubscription regression during texture/mesh decoding, bounded queues/memory, stable solo/crowd/travel comparisons. |

Ordering after stage 2 is conditional on evidence. Do not parallelize a cheap
function or replace a useful cache with task-queue overhead. Prefer eliminating
work, then improving data access, then parallelizing independent work.

## Quality and threading constraints

- Do not lower avatar limits, draw distance, texture quality, animation rate,
  detail levels, shadows or other graphics settings to produce a claimed gain.
- Preserve attachment motion, constraints, local-avatar responsiveness, posing,
  animesh, impostors, name tags, RLV restrictions and existing tuning behavior.
- Snapshot plain owned inputs. Workers must not retain raw viewer object,
  drawable, joint, settings, LLSD or GL state without an established ownership
  contract. Recheck generation/lifetime before applying a result.
- Keep graphics operations on their owning context and thread. Existing optional
  texture/media upload workers are not a general-purpose parallel renderer.
- Use bounded pools and queues, coalesce obsolete jobs, and account for existing
  image-decode, mesh, network and media workers. Avoid one thread per avatar.
- Define dependency/barrier points before dispatch. Avoid making the main thread
  wait immediately after every task, blocking it on worker locks, or introducing
  an unnoticed frame of animation/input latency.
- Do not ship large concurrency changes enabled by default without local visual,
  performance and lifecycle evidence. Regressions remain on their own branch for
  diagnosis, not combined with unrelated optimizations.

## Measurement protocol

Use the frozen baseline and one checkpoint at a time, with identical settings,
resolution/window size, camera, avatar limits, viewer focus and session count.
Record CPU/GPU, driver, build configuration, commit, time, settings, nearby and
visible avatar counts, cache state, and whether assets have finished arriving.

1. Solo control: fixed camera in a loaded scene.
2. Crowd: fixed camera with many visible avatars; warm assets first.
3. Crowd motion: repeat a camera turn and walk, with animation/attachments visible.
4. Travel: separately record arrival/rez frames; do not mix them into steady FPS.
5. Lifecycle: avatar arrival/departure, outfit change, sit/stand, animesh,
   teleport/logout and viewer exit; check existing posing and character switching.

Capture at least three alternating baseline/checkpoint samples per scenario,
after warm-up. A recommended starting sample is 60 seconds. Public crowds
change continuously; record population/settings changes and repeat noisy runs.
Do not automate account login, travel or uploading captures without permission.

Report median, p95 and p99 frame time in milliseconds, long-frame counts,
main-thread time and GPU time where available. Include sample duration/count,
memory and worker queue pressure. Aggregate FPS cannot establish a cause.
Use timing distributions and bottleneck attribution, not a universal promised
percentage. Any changed appearance, new crash, unbounded queue/memory growth,
or consistent control-scene regression prevents default enablement.

## Build handoff

Build each PR's exact head, which includes the documented feature baseline.
Do not combine independent performance PRs or add newer feature heads implicitly.
Build one viewer at a time against the shared cache, as owned by the local
building agent. Retain each completed installer/package separately.

Project defaults remain 64-bit `ReleaseFS_open_AVX2`, Nightly and NSIS unless
the user instructs the building agent otherwise. Profiling options needed for a
specific checkpoint must be explicit in that PR and applied equally to baseline
and comparison. Normal release builds remain valid without Tracy enabled.

## Status

- Stage 0: roadmap and baseline prepared; all ten open feature heads 79 through
  88 verified as ancestors. Documentation-only checkpoint; no viewer build needed.
- Stage 1: avatar profiling implemented; complete affected translation unit passes
  native MSVC syntax checks with Tracy off/on. See
  `viewer-performance-01-profiling.md` for capture and acceptance instructions.
- Stage 2a: rigged bounds skip irrelevant joint lookups; 4,013 production-block
  comparisons and native Tracy off/on syntax checks pass. Hardware gain and
  visual acceptance pending; see `viewer-performance-02-rigged-bounds.md`.
- Stage 2b: rigged bounds reuse the existing aligned joint matrix cache after
  8,013 actual-class coherence comparisons; see
  `viewer-performance-03-matrix-cache.md`. Full linking/visual/performance
  acceptance remains pending.
- Stage 2c: bit-exact SIMD palette packing passes 188 layout/guard comparisons
  in default x64 and AVX2 builds. A Ryzen 5 3600 microbenchmark shows lower
  kernel time; see `viewer-performance-04-palette-packing.md`. Whole-frame
  performance, ARM execution and visual acceptance remain pending.
- Stage 2d: single-pass skinning generation fixes the reproduced missing-joint
  fallback error, eliminates temporary copies and reduces compiled stack
  reservation. Both typed and actual-SIMD variants pass 1,252 comparisons; see
  `viewer-performance-05-skinning-generation.md`. Runtime acceptance is pending.
- Stage 3a: read-only animation sampling retains exact results across 56,232
  actual-math comparisons and unchanged key/metadata state; see
  `viewer-performance-06-animation-sampling.md`. A separate contiguous-storage
  experiment is planned before any container change or worker dispatch.
- Stage 3b: the opt-in map/flat storage experiment passes 146,502 cross-container
  comparisons plus the sampling suite for each container. Longer curves sample
  faster, but naive flat storage regresses single-key sampling and unordered
  loading. Production storage is unchanged; see
  `viewer-performance-07-curve-storage.md` and the bulk-loading follow-up plan.
- Stage 3c: refined bulk loading and one-key sampling pass 293,004 cross-container
  cases plus three sampling suites. Bulk loading avoids quadratic insertion but
  adds loading cost and transient bytes. Normal viewer storage is unchanged;
  plan 9 proposes an off-by-default experimental build for runtime acceptance.
- Stage 3d: `FS_CONTIGUOUS_ANIMATION_KEYS` supplies that off-by-default variant,
  with actual-helper lifecycle checks, exact sampling checks in both modes,
  CMake usage propagation and native caller checks in both Tracy configurations.
  Plan 9 contains explicit matched build instructions. No worker dispatch or
  default storage change; plan 10 addresses canonical asset ownership next.
- Stage 4a prerequisite: hand-pose values now belong to the character, removing
  transient pointers into cached assets. Thirty-six actual character/keyframe
  checks pass in both curve modes; prior pointer behavior fails the regression
  check. Plan 10a covers runtime hand-morph acceptance; plan 10b covers asset
  leases and obsolete per-motion constraints before any worker dispatch.
- Stage 4b: canonical cache/motion owners and protected const leases pass 34
  actual ownership checks in both curve modes, including decode failure,
  eviction/replacement, constraint cleanup and background final release. The
  36 hand-pose checks remain green. Plan 11 defines owned sample inputs/outputs;
  no production worker scheduling is enabled.
- Stage 4c: move-owned sample inputs/results pass 24,193 byte-exact pose
  comparisons and 109,806 checks in each curve mode, with stale-context,
  malformed-shape, buffer-move and background lifetime coverage. No playback
  caller uses the sampler yet. Plan 12 measures bounded execution and full cost,
  including the existing Windows pool's sleep/wake behavior.
- Stage 4d: test-only bounded execution passes 1,140 ownership/admission/lifecycle
  checks in each storage mode, plus negative checks for callback growth and stale
  generation claims. Private-pool wake/idle-CPU measurement is opt-in. No live
  playback or scheduler change; plan 13 measures complete sampling costs next.
- Stage 4e: complete-cost batching passes 1,734 correctness/reuse/fallback checks
  in each storage mode. Two 120-trial timing runs show immediate consumption is
  unsuitable for the existing pool; synthetic overlap can help larger batches.
  Separate allocation probes count cancellation churn and retained buffers.
  Plan 14 audits real update ordering before any playback integration.
- Runtime crowd captures and Windows acceptance remain pending. No measured
  improvement has been claimed.
