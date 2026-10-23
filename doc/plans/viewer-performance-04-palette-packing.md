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

Implemented `FSAvatarPalette::packMatrix` and integrated it into the existing
per-palette loop. The helper reads one complete 16-float matrix and writes
exactly twelve floats; buffers must be valid and non-overlapping, but need not
be SIMD-aligned. The original loop/count, cache, allocations, matrix calculation
and upload order remain unchanged. Empty batches are exercised by the test's
loop; the single-matrix helper itself always requires a valid matrix.

The implementation uses only loads, shuffles and stores from the existing
SSE/SSE2 baseline, with the existing `sse2neon` compatibility layer on ARM. It
adds no AVX2 requirement, arithmetic, worker or runtime preference. The original
scalar assignments are preserved in the production comment and benchmark path.

### Evidence

- 188 exact layout/guard comparisons pass in native MSVC Release builds with
  default x64 instructions and explicit AVX2. Includes an independent lane map,
  zero/small/110/216/4,096-matrix batches, input/output offsets, input immutability,
  signed zero, subnormal, infinity and NaN payload/signaling patterns.
- The 4,013 production bounds checks still pass after caller integration.
- Complete `llvoavatar.cpp` passes native MSVC C++20/AVX2 syntax checks with
  Tracy off/on. Whitespace and source review pass. Normal compatibility warnings
  remain; no full viewer build or shared-cache mutation was performed.
- MSVC assembly inspection shows the old loop issuing individual DWORD
  loads/stores, while the candidate uses three vector stores per matrix.
  An initial no-alias scalar variant remained close to legacy throughput and
  was not added to production.

Native microbenchmark on the actual Ryzen 5 3600, MSVC 19.51.36244, x64 Release,
`/O2 /EHsc /fp:precise /arch:AVX2`:

| Joints per palette | Legacy ns/matrix | Packed ns/matrix |
| --- | ---: | ---: |
| 1 | 5.365 | 2.184 |
| 4 | 4.008 | 1.416 |
| 15 | 3.401 | 1.135 |
| 32 | 3.261 | 1.155 |
| 64 | 3.327 | 1.308 |
| 110 | 3.270 | 1.251 |
| 216 | 3.252 | 1.276 |

These are medians of nine alternating-order samples for each kernel, processing
about three million matrices per sample across eight palettes. Non-inlined
wrappers, a compiler fence and an observed output checksum prevent removal of
the work. The timing covers the packing loop and call overhead, not matrix
generation, task dispatch, scene update, drawing or whole-frame performance.
At 110 joints the kernel time is about 62% lower in this workload. This is not
a claimed FPS increase. Default x64/SSE correctness and a separate benchmark
also pass; ARM execution and full viewer linking remain pending.

### Reproduce and local acceptance

Configure the standalone `indra/newview/tests/folderstorm` project and build only
`avatar_palette_packing`. Run CTest with `-R '^avatar_palette_packing$'`; the
normal test never benchmarks or asserts a timing threshold. Run the built
executable with `--benchmark` explicitly for timings. For matched AVX2 MSVC
numbers use a separate build directory and preserve the normal compiler flags
when adding `/arch:AVX2 /fp:precise` (including `/EHsc`). On ARM, supply
`FS_PALETTE_SSE2NEON_INCLUDE_DIR` if it is not discoverable.

Build this PR's exact head separately on the frozen feature/performance stack,
using normal 64-bit `ReleaseFS_open_AVX2`, Nightly and NSIS. Compare with #92 at
unchanged settings. Check rigged meshes, attachments, animations, posing,
sit/stand, outfit/LOD changes, animesh and shadows. In a Tracy build, inspect the
`Avatar palette packing` zone independently from matrix generation. In-world
FPS/frame-time improvement and visual acceptance still require local testing.

Next plan: investigate the temporary world-matrix array in skinning matrix
generation and its missing-joint fallback, then test before attempting fusion.
