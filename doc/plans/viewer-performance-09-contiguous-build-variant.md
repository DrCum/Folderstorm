# Checkpoint 9: isolated contiguous-animation build variant

Plan written before implementation. Parent: checkpoint 8 refined experiment.

## Objective and decision

Make the tested candidate available to the local building agent while retaining
original map storage in normal builds. Use a compile-time CMake option named
`FS_CONTIGUOUS_ANIMATION_KEYS`, default OFF, with consistent public definitions
for every consumer of the affected curve classes. There is no runtime toggle or
automatic enabling. The option changes type layout and requires a complete
consistent rebuild of dependent translation units.

The candidate improves synthetic longer-curve sampling and retained key bytes,
but adds loading cost and temporary memory; short rotations still have a small
sampling penalty. Keep these limits in the PR and acceptance handoff. No FPS
claim is justified without matched real-scene samples.

## Work sequence

1. Audit deserialization allocation/failure paths and cache publication. Ensure
   incomplete staged curves cannot become visible as valid assets. Check legacy
   and quantized formats, empty/duplicate/invalid keys, and edit/reload cleanup.
2. Introduce a small curve-storage helper/alias selected by the build option.
   Normal builds retain std::map. Experimental builds use bundled flat_map and
   a scoped temporary map while decoding. Finalize once per curve with an
   ordered unique range and release scratch nodes before the next curve loads.
   Avoid a second permanent representation or untrusted-count reservation.
3. Update the three curve aliases and one-key candidate paths only under the
   option. Preserve upstream code/comments. Keep arithmetic, query time,
   loop/hand-pose behavior, serialization ordering and `mNumKeys` diagnostics.
4. Exercise the actual helper with malformed/partial-load exits, duplicates,
   empty/long/reversed keys, signed timestamps and exact output checks. Regenerate
   the production sampling suite for both compile configurations. Measure helper
   allocations/peak behavior if implementation differs from the prototype.
5. Compile-check complete animation and pose callers with the option off/on and
   Tracy off/on as appropriate. Confirm the option propagates to all class-layout
   consumers. Keep native focused targets and full viewer acceptance distinct.
6. Publish a runtime-checkpoint PR with explicit OFF/ON build variants, exact
   comparison heads and separately retained installers/packages. Default builds
   continue using maps until real-scene acceptance supports a later decision.

## Build and runtime acceptance handoff

Use x64 ReleaseFS_open_AVX2, Nightly, NSIS. The building agent owns the shared
cache and may build only one viewer at a time. Request experimental configuration
explicitly with `-DFS_CONTIGUOUS_ANIMATION_KEYS=ON` through autobuild's CMake
arguments; give the control OFF explicitly when reusing a configured cache.
Retain both completed variants under distinct archive names. Do not run full
viewer builds or configure the shared cache from the source-work agent.

Test animation arrival/rez and retained/peak memory as well as steady crowded
frame times, poses, loop edits, gestures, animesh, preview close/reload, outfit
changes, teleport/logout and exit. Record exact settings and changing population.
Check solo and short-animation controls for regressions. Disabled-by-default
status is retained until these results are available.

## Next worker boundary

The candidate remains main-thread owned. Separate design work must give workers
owned immutable asset snapshots, versioned inputs/results and bounded queues,
and preserve same-frame application before constraints. Do not move raw-pointer
motion sampling or live joint-state writes to workers merely because sampling
is const or storage is contiguous.

## Results

Not implemented yet. Deserialization publication/lifetime audit is the first step.
