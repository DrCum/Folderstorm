# Viewer performance: questions and local debugging

Answers can wait. Continue work that does not depend on a pending answer.
Never present a missing hardware result as a successful test.

## Confirmed

- Include the current feature stack, frozen through PR 88 at program start.
- First priority: many avatars and steady FPS.
- Test system: Ryzen 5 3600, RTX 3060 12 GB.
- User confirmed PR 114 runtime testing is still pending. Continue independent
  source work and retain local testing/capture requests for later.
- Local builds are handled by a separate agent; performance PRs need explicit
  independent/dependent build instructions and retained versions.

## Questions for later

| ID | Question | Why it matters | Blocks |
| --- | --- | --- | --- |
| Q1 | Typical crowded places, approximate avatar counts and a repeatable camera/view? | Reproducible CPU workload. Do not commit private locations or names. | Final performance acceptance only. |
| Q2 | Resolution, graphics preset, shadows, draw distance, avatar limits and autotune/impostor settings? | Separate CPU work from GPU pressure and dynamic quality changes. | Benchmark comparison only. |
| Q3 | Usually one active viewer or several hosted characters? | Existing processes share the six physical cores. | Final worker admission/default tuning. |
| Q4 | Can a matched baseline/checkpoint profiling capture be collected once stage 1 is built? | Select the first expensive kernel from evidence. | Default enablement of major threading changes. |
| Q5 | Any existing animation, attachment, posing or character-switch defects to distinguish from regressions? | Keep quality checks attributable. | Acceptance triage only. |
| Q6 | Any known legacy meshes/animesh that become unstable while loading or changing outfits? | Optional in-world validation of the missing-joint fallback fix. | Acceptance only. |
| Q7 | After the shadow checkpoint has a retained build, can you collect its aggregate line from a warmed crowded scene using plan 14g's deliberate TRUE/FALSE debug-setting interval? | Establish query coverage and a conservative real same-frame window; enabled diagnostics add CPU cost. | A live bounded worker trial and final threading acceptance. |

## Local debugging and acceptance queue

- Baseline and each checkpoint: full native Windows compile/link and runtime
  verification by the building/testing agent; portable helper tests do not cover
  complete viewer integration.
  The independent agent has retained full native builds through PR 108, including
  its 23 focused checks. Newer checkpoint heads still need their own build, and
  rendered/lifecycle/crowd acceptance remains pending for the stack.
- Collect matched solo/crowd frame-time samples and profiler captures as described
  in the roadmap. Raw captures may contain user data; share only deliberately.
- Native 14l density controls varied by more than twofold even in raw thread
  cycles. The cause is not identified. Preserve those records as inconclusive;
  a quiet repeatable workload/process environment or matched real scene evidence
  is needed before reconsidering that cache. No process/affinity changes were
  made, and the prototype is withdrawn. Q3 remains relevant; no new answer is
  required for independent preparation work.
- Watch attachment/skeleton alignment, sitting/constraints, animesh, outfit swaps,
  posing, avatar arrival/departure, teleport/logout and exit.
- Track exact head, settings, scene conditions, logs and reproduction steps for
  each regression. Do not overwrite retained builds.
- Checkpoint 5 reproduces and fixes the `initSkinningMatrixPalette` missing-joint
  fallback overwrite. Typed and actual-SIMD generation checks pass; a known
  legacy-mesh visual reproduction would be useful later. No such asset is needed
  to continue source work.
- Checkpoint 9 needs separately retained map/contiguous builds with the CMake
  option explicitly OFF/ON. Include arrival/rez latency and peak memory alongside
  steady crowd frame times; the candidate has measured loading/memory tradeoffs.
  Full asset-parser/preview/pose behavior and ARM/macOS execution remain pending.
- Ownership audit: verify preview/reload and motion removal around character
  hand-pose/priority pointers. LLHandMotion normally consumes and clears those
  entries per update; they must stay under main-thread lifetime handling.
- Checkpoint 10a makes those published values character-owned. Validate hand
  morphs, competing priority/equal-priority animations, editing tools and preview
  edits/close/reload. Native checks cover data/lifetime ordering, not rendered
  hand shapes or complete avatar lifecycle acceptance.
- Checkpoint 10b retains active asset data across cache eviction/replacement and
  clears obsolete constraints on adoption. Test repeated preview/reload,
  constraints/sitting, active cache removal and shutdown. Memory should release
  after the last motion/lease ends, rather than at cache eviction alone.
- Checkpoint 11 supplies pure owned sampling data, with no playback dispatch or
  live version-counter wiring. Queue/wake overhead, bounded fallback and full
  preparation/application cost are measured next; runtime FPS remains unproven.
- Checkpoint 12's bounded executor and wake benchmark are test-only. Its focused
  lifecycle checks pass in both curve modes, but do not establish useful overlap
  in the viewer. Plan 13 accounts for complete costs before any production caller
  or scheduler change. The historical Windows scheduler CPU issue needs sustained
  load/idle, coroutine/fairness and shutdown testing before considering a change.
- Checkpoint 13 compares complete sampling costs over generated assets. Immediate
  consumption mostly falls back; larger batches benefit with synthetic independent
  work. A real sufficient overlap window and total CPU/FPS improvement remain
  unproven. Plan 14 maps actual motion/pose dependencies before any live dispatch.
- Checkpoint 14a fixes live position changes caused by size/export calls. Check
  repeated export/preview of a loaded animation and re-open its encoded packet;
  source pose should stay stable while the exported pose retains expected format
  quantization. Generated fixtures cannot replace that workflow acceptance.
- The export audit also found uninitialized trailing bytes in short fixed-width
  constraint-name fields. Checkpoint 14b fixes that padding; verify export/re-open
  with short ordinary volume names and ground constraints. The preceding frozen
  byte fixtures still use full-width names to isolate their old-byte oracle.
- Checkpoint 14f adds current owner identity and rejects stale results in native
  factory/controller fixtures. Test successful preview/reload and close/reopen,
  character/skeleton replacement, stop/reactivate and scene role transitions.
  The binary replacement fixture proves context publication only, not rendered
  active-reload pose-map correctness. No live dispatch is enabled.
- Checkpoint 14f measured two repeated hand-pose key-name allocations per update.
  Checkpoint 14f1 removes them while preserving priority ties and generic data overrides.
  Rendered competing hand animations and hand cleanup/republication remain an
  acceptance item; generated exact poses do not cover visible morphs.
  Both storage modes pass 171 actual publication/lifetime/consumer checks and
  zero repeated allocations; post-removal node/key allocations remain counted.
- Checkpoint 14g's source/native implementation is complete, with manual viewer
  observation still pending. Record exact build/settings/duration and only the
  aggregate `AvatarSamplingShadow` line from the explicit debug setting; keep
  unrelated logs private. Compare normal FPS with that setting FALSE. No setting
  or account was operated by this chat. The next purge/blender CPU work can
  proceed independently; generated tests do not establish a real queue deadline.
- Checkpoint 14h removes idle motion-purge allocation. Native eviction/order and
  ownership comparisons pass; check cache-heavy travel, animation/outfit changes,
  arrivals/departures and ordinary/hidden avatar transitions in its retained
  viewer build. Compare warmed crowded frame times with diagnostics FALSE.
- Checkpoint 14i changes active-blender storage. Its frozen/native comparisons
  cover cache, interpolation, priorities, state lifetimes and controller paths;
  check visible easing, low-FPS interpolation, pause/visibility transitions,
  outfit/skeleton replacement and crowded travel/churn in its retained build.
  Ordinary controller updates allocate less; real scene FPS and memory remain
  acceptance work. No production worker dispatch is enabled.
- Checkpoint 14j preserves normal pool behavior and repairs failed publication
  and canonical entry reuse. Native failure/retry and frozen frame comparisons
  pass. Check ordinary cache/animation churn and frame times in its retained
  viewer build; controlled allocation failures are exercised in the dedicated
  native probe, not through an in-world memory-exhaustion test.

## Results

Checkpoint 14e passes native per-job numeric-control/restoration and actual
underflow/rounding-sensitive sampling checks in both storage modes. Current
numeric-mode changes reject application; unsupported modes retain direct fallback.
Its synthetic complete-cost results still do not prove a real overlap window or
FPS improvement. Plan 14f audits actual eligibility/lifecycle/clock authority and
the real phase bridge's temporary current-usage allocation before integration.

The independent building agent has retained private PR 106 and 107 builds after
the user's October 24 resume. That compile/retention evidence is distinct from
rendered/crowd acceptance; no in-world measurements have been received here.

Checkpoint 14c adds a protected phase contract with frozen update comparisons and
actual controller callbacks. Full viewer and rendered constraint/hand-morph
acceptance remain pending. Checkpoint 14d reproduces and fixes the controller
constructor's uninitialized initial joint mask; verify initial constraints just
after character creation as well as normal priority/sit/pose transitions.
14c's historical controller fixtures explicitly establish masks by updating
first; the new regression also covers activation before any update. Plan 14e
addresses worker floating-point context before further live integration.

No in-world captures or runtime acceptance results received yet. A native
Ryzen 5 3600 palette-packing microbenchmark was collected for checkpoint 4;
its scope and results are documented in that plan and do not establish FPS gains.
Native skinning generation and animation-storage experiments are also documented
in plans 5 and 7. The naive storage candidate is not adopted because single-key
sampling and unordered insertion regress despite longer-curve sampling gains.
