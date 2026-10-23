# Viewer performance: questions and local debugging

Answers can wait. Continue work that does not depend on a pending answer.
Never present a missing hardware result as a successful test.

## Confirmed

- Include the current feature stack, frozen through PR 88 at program start.
- First priority: many avatars and steady FPS.
- Test system: Ryzen 5 3600, RTX 3060 12 GB.
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

## Local debugging and acceptance queue

- Baseline and each checkpoint: full native Windows compile/link and runtime
  verification by the building/testing agent; portable helper tests do not cover
  complete viewer integration.
- Collect matched solo/crowd frame-time samples and profiler captures as described
  in the roadmap. Raw captures may contain user data; share only deliberately.
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

## Results

No in-world captures or runtime acceptance results received yet. A native
Ryzen 5 3600 palette-packing microbenchmark was collected for checkpoint 4;
its scope and results are documented in that plan and do not establish FPS gains.
Native skinning generation and animation-storage experiments are also documented
in plans 5 and 7. The naive storage candidate is not adopted because single-key
sampling and unordered insertion regress despite longer-curve sampling gains.
