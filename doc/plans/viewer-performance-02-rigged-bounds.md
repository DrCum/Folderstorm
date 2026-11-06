# Checkpoint 2: skip irrelevant rigged-bound joint lookups

Plan written before implementation. Parent checkpoint: #90, avatar profiling.

## Hypothesis and scope

`LLVOAvatar::calculateSpatialExtents` visits all 216 possible animated joint IDs
when rigged bounds are requested. It calls `getJoint()` before checking whether
rigging data exists or marks that joint as used. IDs beyond the bone/collision
arrays can perform attachment-map lookups. Empty and sparsely rigged tables
therefore cause work that cannot contribute to the resulting box.

Bound iteration by both the existing joint cap and rigging table size. Check
`isRiggedTo()` before retrieving the joint. Preserve the original traversal
order, rigging refresh, missing-joint handling, HUD exclusion, matrix transform
and min/max operations for contributing joints. This is a serial optimization;
it adds no cache, worker, policy, reduced detail or stale data.

## Correctness and validation plan

- Run the actual production rigged-bound block in a small generated C++ harness,
  compared with the frozen legacy block. Stub only the surrounding viewer
  ownership/types; the changed traversal is extracted from production source.
- Verify identical bounds, transform order, rigging refresh counts and relevant
  joint lookups for empty, sparse, fully populated, oversized and changing tables;
  missing joints; HUD joints; transformed boxes; and lower detail modes.
- Use seeded varied fixtures in addition to named boundary cases. Count lookups
  explicitly. Empty tables should perform zero lookups, while a table with three
  marked joints should perform three rather than 216.
- Register the harness in the standalone Folderstorm CMake regression project,
  with source regeneration when the production block or harness changes. Support
  Windows/MSVC and ordinary C++ compilers without a viewer build.
- Compile the complete changed translation unit natively with Tracy off/on.
- Record operation-count evidence separately from FPS. A smaller count is not
  evidence of a measurable whole-frame gain on the user's hardware.

## Local acceptance and rollback

Build this exact PR head on the included feature/performance stack and compare
against #90. Keep graphics and avatar settings identical. Inspect crowd movement,
camera turns, avatar arrival/departure, outfit/mesh LOD changes, HUD attachments,
sit/stand, animesh and shared posing. Watch for changed avatar culling, clipping,
impostor size or shadows. Repeat a loaded solo control and crowd capture.

Any bounds mismatch, changed contributing-joint order or visual culling/clipping
regression blocks acceptance. Revert this one checkpoint without changing the
profiling baseline if necessary. Full viewer linking, runtime quality and frame
time improvement remain pending the building/testing agent.

## Results

Implemented table-bounded traversal and rigged-flag rejection before joint
lookup. All contributing joint transforms and their order remain unchanged.

- Native MSVC Release CTest `avatar_rigged_bounds`: 4,013 comparisons pass,
  including 2,000 seeded varied tables and their changed-rigging follow-ups.
  The generated executable runs the actual production block and frozen legacy
  traversal with typed boundary fixtures. Full viewer SIMD math is unchanged
  and is not replaced by the harness's transform stub.
- Old traversal makes 216 joint lookups per high-detail invocation. Empty and
  three-marked-joint cases now make 0 and 3. Fully marked tables retain all
  relevant lookups; oversized tables preserve the original 216-joint cap.
- The complete changed `llvoavatar.cpp` passes native MSVC 19.51 C++20/AVX2
  syntax checks with Tracy off and on. The existing Boost macro-redefinition
  warning remains. Whitespace and source review pass.
- Full viewer compile/link, local visual acceptance and measured FPS/frame-time
  gain remain pending. No broad viewer build or shared-cache writes performed.

Next plan: inspect and reuse the joint's existing aligned world-matrix cache in
this bounds path, if actual joint cache coherence can be verified. Keep this
separate so a matrix-access regression can be isolated from traversal changes.
