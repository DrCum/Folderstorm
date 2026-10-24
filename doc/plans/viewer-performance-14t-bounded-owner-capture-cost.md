# 14t: isolate bounded owner-binding retention cost

Written after completing 14s and before this implementation. Start from its
published exact head on a separate performance branch. Preserve all prior
programs, accepted observations and build checkpoints.

14s's serial-owned path makes exactly 32 later-frame binding allocations per
32-character frame. Its first-consumption batch also misses every immediate
deadline. These are separate concerns. This plan isolates capture/storage cost
without changing queue readiness strategy, controller order or viewer dispatch.

1. Add a private serial-owned retained-binding mode alongside unchanged direct
   and per-frame-release controls. Reuse the production current-context capture
   and independent application checks. Keep the generated fixed cohort, asset
   identity and source math identical. No production API, cache or setting changes.
2. Keep retention inside the controlled trial's Scene lifetime, bounded to one
   admitted ordinary motion per character, at most 32 characters. Record actual
   retained binding bytes and release them on the owner before Scene destruction.
   Charge final cleanup to the complete owner cycle window. Explicitly scope
   this cost experiment to its fixed cohort; it is not a general viewer cache or
   a proof of deprecated-motion/reentrant scene lifetime handling.
3. Reuse the actual controller oracle for poses, hand output and callback order;
   add controller index/time to the private callback trace so same asset IDs on
   different characters remain distinguishable. Cover frame/skeleton/asset/
   permission staleness with retained bindings, verify cleanup, and retain the
   broader existing context/lifetime suites as distinct evidence. No stale result
   becomes authorized merely because a buffer stays allocated.
4. Before timing, freeze a small paired/reversed matrix for direct, serial-owned
   released and serial-owned retained paths at 1 or 32 characters and zero synthetic
   gap. Use separate allocation instrumentation. Include cold and later-boundary
   allocation counts, retained/peak bytes, complete owner wall/cycles and final
   cleanup. Keep first-consumption policy unchanged; do not repeat the scheduler
   matrix for an unrelated cache hypothesis.
5. Require full-host/private-builder idle, exact source/program identities,
   nonoverlap and preserved failures. Publish a separately reviewable test-only
   checkpoint with the resulting next plan. If capture remains costly, retain
   that result. Any later same-frame staggered readiness experiment is separate;
   neither this cache probe nor a synthetic gap authorizes a viewer pilot.

No live viewer/account, quality reduction, extra profiling variant, main-thread
wait or production animation worker is part of this work. Q4/Q7 and rendered/
crowd acceptance stay deferred; no new user action is requested now.
