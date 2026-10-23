# Checkpoint 4: isolate and measure palette layout conversion

Plan written before implementation. Parent checkpoint: #92, aligned joint cache.

## Hypothesis and boundaries

On each skinning-palette cache miss, `updateSkinInfoMatrixPalette` converts each
aligned 4x4 joint matrix into twelve GPU-layout floats. The current loop selects
indices `0,1,2,12,4,5,6,13,8,9,10,14`. That is a fixed data permutation with no
arithmetic, so a candidate SIMD implementation can be tested bit for bit.
Before changing production, check what MSVC already generates and benchmark
the actual reference/candidate kernels. Do not assume hand-written SIMD wins.

This work keeps the existing once-per-frame palette cache, palette ownership,
matrix calculation, shader layout and graphics upload order. It dispatches no
tasks and retains no scene references. A worker split is a later decision based
on real crowd profiles and task/copy overhead, not a consequence of extraction.

## Work sequence

1. Extract a testable packing kernel with a scalar reference matching all twelve
   existing assignments. Document input/output count, alignment and alias rules.
   Avoid increasing allocations or copied buffers in the production path.
2. Compare a candidate SIMD permutation using the viewer's existing SIMD
   abstraction, including its ARM compatibility layer. Do not introduce a new
   CPU requirement or external dependency.
3. Test all selected lanes and exact bytes with identity, translation, signed
   scale, arbitrary matrices, zero/small/maximum counts, unaligned output,
   guard regions, signed zero and non-finite bit patterns. Packing must not
   perform arithmetic or change a NaN payload. Keep this separate from numerical
   matrix multiplication tolerance tests.
4. Benchmark optimized reference and candidate kernels with multiple palette
   sizes and repeated alternating samples. Make outputs observable so the
   optimizer cannot discard work. Keep benchmark execution opt-in; correctness
   tests remain fast and deterministic. Record machine/compiler/options and
   avoid presenting synthetic throughput as an in-world FPS result.
5. Integrate only if correctness and useful native throughput evidence justify
   it. Otherwise retain the reference implementation or publish an investigation
   result and select the next candidate. Compile-check the complete production
   caller and keep new headers registered in its existing build system.

## Future worker eligibility

Packing operates on an owned matrix array and output buffer, which is a cleaner
ownership boundary than live joints. However, a per-palette job may be too small
to amortize queueing and waiting. Before dispatch, measure CPU self time in the
#90 zones, total palette counts/sizes, queue overhead, and the caller's immediate
need for uploaded data. Batch only where dependencies allow it. Never capture
a live `MatrixPaletteCache` reference or introduce a blocking per-draw task.

## Acceptance and follow-up

The reference bytes and existing cache/upload behavior must remain exact. Local
acceptance includes rigged attachments, animations, posing, sit/stand, outfit and
LOD changes, animesh, shadows, camera movement and matched solo/crowd samples.
Actual gains, ARM execution and full viewer linking remain explicit pending
items unless exercised. Write the next plan based on these results before
implementing another optimization.

## Results

Investigation started. No packing or worker behavior change implemented yet.
