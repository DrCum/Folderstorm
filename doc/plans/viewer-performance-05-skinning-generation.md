# Checkpoint 5: skinning matrix generation and missing-joint fallback

Plan written before implementation. Parent checkpoint: #93, palette packing.

## Source hypothesis

`LLSkinningUtil::initSkinningMatrixPalette` gathers joint world matrices into a
216-entry temporary array, then multiplies each inverse-bind matrix by the
corresponding temporary. The array occupies 13,824 bytes before compiler
optimization. Each valid entry is copied before multiplication.

The missing-joint branch assigns the intended inverse-bind output fallback but
does not initialize `world[j]`. The later unconditional multiply reads that
uninitialized entry and overwrites the fallback. This needs a deterministic
regression before any generation change is accepted.

Both current callers supply separate output storage. Inspect their count and
ownership contracts before removing the array. All scene/joint access remains
on the main thread. This checkpoint does not move scene objects to workers.

## Implementation gates

1. Run the actual production function body against typed boundary fixtures that
   track whether a matrix is initialized. Reproduce a missing-joint read/fallback
   failure deterministically, without depending on arbitrary stack contents.
2. Preserve `initJointNums`, invalid inverse-bind-size rejection, diagnostic
   behavior and exact output fallback. Bound any new indexing by the same count
   contract. Exercise both callers' typical counts and empty inputs.
3. For valid joints, consider directly calling the unchanged `matMulUnsafe`
   with the existing aligned joint matrix, combining gathering and multiplication
   in one loop. Do not reorder floating-point operations within a matrix or
   introduce a different transform convention.
4. Verify valid results against the frozen two-pass reference, lookup/refresh
   and diagnostic counts, missing first/middle/last/all joints, short data,
   repeated IDs, and inputs changing between invocations. Guard output storage.
5. Add native actual-SIMD equivalence coverage and an opt-in benchmark for valid
   cases. Inspect compiled frame/copy behavior before claiming a throughput
   benefit. Keep failures and performance evidence separate.
6. Compile-check both changed native callers with Tracy off/on as appropriate,
   run affected focused checks, review and publish this checkpoint separately.

## Empty and malformed input audit

The palette caller currently takes `&vector[0]` before calling generation and
packing. If zero joint counts are supported, use safe empty-vector access and
make the generation no-op defined; do not leave a test harness passing an empty
batch while the real caller still invokes undefined indexing. Inspect short
joint-number and bind arrays before extending malformed-input behavior. Avoid
silently rendering unrelated fallback transforms for valid data.

## Acceptance

Normal matrices and packed bytes must match exactly. Missing joints retain the
intended inverse-bind fallback and existing diagnostics instead of multiplying
uninitialized memory. No new allocations, caches, workers, frame latency or
graphics-setting changes. Full native linking, visual/quality acceptance and
whole-frame benefit remain pending local tests of avatars, attachments, posing,
sitting, outfit/LOD changes, animesh and crowd motion.

Future worker work must snapshot owned matrices and output storage; it cannot
capture the new live joint reference. Numerical fusion and thread ownership
remain separate review decisions. Write the next plan from the evidence after
this checkpoint is implemented.

## Results

Investigation started. No generation or fallback changes implemented yet.
