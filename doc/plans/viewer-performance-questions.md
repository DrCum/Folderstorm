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

## Results

No hardware captures or local acceptance results received yet.
