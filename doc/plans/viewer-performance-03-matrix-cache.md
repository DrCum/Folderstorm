# Checkpoint 3: reuse aligned joint matrices in rigged bounds

Plan written before implementation. Parent checkpoint: #91, sparse rigged bounds.

## Hypothesis

The bounds path loads an `LLMatrix4` into a temporary aligned `LLMatrix4a` for
each contributing joint. `LLJoint` already maintains an aligned world matrix,
updated alongside its transform matrix. Its `getWorldMatrix4a()` getter uses
the same parent/dirty update path as `getWorldMatrix()`.

If the existing cache is coherent under all relevant operations, the bounds
path can pass its aligned matrix directly to `matMulBoundBox`, avoiding the
explicit 64-byte load and stack temporary per contributing joint. This does
not introduce a new cache, delayed results, parallel access or different math.
Any whole-frame gain will need measurement; the compiler may already eliminate
some copies in the old path.

## Steps and gates

1. Exercise actual `LLJoint`, `LLXformMatrix` and SIMD matrix classes, comparing
   both getters bit for bit. Include initial/clean states, parent and child
   changes, translation, rotation, signed/nonuniform scale, world-space setters,
   PRS getters before matrices, tree traversal, reparenting and detachment.
   Alternate the getter called first so refreshing the legacy getter cannot
   hide a cache coherence defect.
2. Run this native test using read-only existing libraries if possible. Keep
   outputs outside the shared cache. Register the focused test for full viewer
   builds with `LL_TESTS` enabled.
3. Only after cache coherence checks pass, replace the temporary load with a
   const reference to `getWorldMatrix4a()` in the bounds path. The aligned
   reference stays on the main thread for the immediate transform call; it must
   not be captured by future workers.
4. Extend the production traversal harness's typed matrix boundary, rerun it,
   and compile-check the complete affected native source. Inspect getters and
   cache update paths for writes outside their dirty-state contract.

## Acceptance

Any getter mismatch blocks the optimization. Preserve refresh/lookup/transform
order and output bounds from #91. Build/test this PR separately, comparing
camera culling, shadows, impostor sizing, animated/rigged attachments, signed
scale, posing, sit/stand, outfit changes, animesh, travel and exit. Full viewer
linking and in-world performance/visual acceptance remain the local agent's work.

## Results

The gate passed before the production change. The native test executable links
read-only existing Release libraries for the actual `LLJoint`, transform and SIMD
classes, whose source is unchanged from the frozen baseline. It passes 8,013
bit-for-bit comparisons, including an external parent transform that models
sitting and getting off an object. Outputs stay in the separate validation
directory. The test source is committed and its `avatar_joint_matrix_cache`
build-and-run target is available when the full viewer is configured with
`LL_TESTS=ON`; that fresh full-build integration remains pending.

The bounds path now passes a const reference to the existing aligned matrix.
The source audit found both getters share `updateWorldMatrixParent()`, which
refreshes both representations together. Direct joint-transform accesses in the
viewer do not separately rewrite the legacy world matrix behind this cache.

The 4,013 production bounds comparisons still pass, and the complete changed
`llvoavatar.cpp` passes native MSVC C++20/AVX2 syntax checks with Tracy off/on.
Whitespace review passes. Existing compiler warnings are unchanged. This proves
coherence/equivalence and removes an explicit source-level load; it does not
establish a frame-time gain or how much copying the optimizer already removed.

Full viewer linking, live visual acceptance and comparative crowd captures remain
pending. Next plan: isolate and benchmark palette layout conversion with a
bit-exact reference before considering a SIMD replacement or worker dispatch.
