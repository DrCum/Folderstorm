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

The unchanged production body failed the deterministic missing-joint test with
`Production read an uninitialized joint world matrix`. The fixture tracks matrix
initialization and refuses to read an unset value; this does not rely on random
stack bytes or claim to be a sanitizer report.

Generation now multiplies directly from each valid joint's aligned matrix, with
the same `matMulUnsafe` operation and operands. Missing joints keep their exact
inverse-bind fallback and existing diagnostic call. Nonpositive counts return
before indexing. The cached palette caller uses `.data()` for both empty-capable
vectors, rather than `&vector[0]`.

### Ownership and malformed-data audit

- Both callers pass output separate from skin inverse-bind and joint storage.
  Their counts come from `getMeshJointCount`, capped at 110; a 216-entry stress
  case remains in the tests. The output/non-alias contract is now documented.
- `LLMeshSkinInfo::fromLLSD` adds a joint-number entry for each joint name and
  clears names, numbers and binds when name/bind counts mismatch. This checkpoint
  preserves that loader and existing positive-count short-bind rejection. It
  does not claim to repair arbitrarily corrupted internal skin containers.
- All three shader upload overloads already reject an empty palette before
  accessing its first packed element. Their draw-skipping behavior is retained.
  No new shader fallback, default setting or mid-frame cache behavior is added.

### Validation

- Standalone native MSVC Release CTest `skinning_generation`: 1,252 comparisons
  pass, including the before-fix failure, valid/short/empty inputs, missing
  first/middle/last/all joints, repeated IDs, guards, diagnostic/lookup counts
  and 400 seeded changing-input fixtures.
- The same generated production-body test passes all 1,252 comparisons with the
  viewer's actual SIMD matrix multiplication. Typed fixtures wrap initialization
  and ownership boundaries; this is not a full avatar runtime test. Valid outputs
  match the frozen two-pass reference byte for byte. Valid-path temporary matrix
  copies drop from count to zero.
- Existing 188 packing and 4,013 bounds comparisons pass after integration.
- Complete `llskinningutil.cpp` and `llvoavatar.cpp` pass native MSVC C++20/AVX2
  syntax checks with Tracy off/on. Both pre-change and post-change complete
  `llskinningutil.cpp` also compile to optimized native objects/assembly.
- Actual viewer-function assembly reserves 13,952 fixed stack bytes before and
  40 after; the `__chkstk` call disappears. This is compiled function evidence,
  separate from the tracked fixture's deliberately different matrix layout.
- Whitespace/source review pass. Existing compiler warnings remain. Full viewer
  linking, fresh full-build test-target integration and in-world acceptance are
  pending. The shared cache was read only for existing dependencies.

### Native kernel timing

Ryzen 5 3600, MSVC 19.51.36244, x64 `/O2 /EHsc /fp:precise /arch:AVX2`:

| Matrices per call | Two-pass ns/matrix | Fused ns/matrix |
| --- | ---: | ---: |
| 1 | 14.994 | 10.966 |
| 4 | 7.051 | 6.373 |
| 15 | 7.766 | 5.620 |
| 32 | 6.900 | 5.474 |
| 64 | 7.937 | 5.582 |
| 110 | 6.475 | 5.128 |
| 216 | 6.250 | 6.210 |

Nine alternating-order samples per kernel, about three million matrices per
sample across eight fixture avatars/skins; output checksums and a compiler fence
keep work observable. These use actual SIMD multiplication and production
function bodies with typed lookup/asset boundaries, without initialization/copy
tracking in the timed path. They exclude the real avatar lookup, loader, dirty
world-transform update and drawing costs. Desktop frequency/background load are
not locked. The oversized 216-entry stress case shows little gain; current
production calls are capped at 110. These results establish no FPS percentage.

### Reproduce and build handoff

Standalone: configure `indra/newview/tests/folderstorm`, build only target
`skinning_generation`, then CTest with `-R '^skinning_generation$'`.

When the full viewer is configured with `LL_TESTS=ON` and Python available,
`avatar_skinning_generation_tracked` builds and runs the actual-SIMD variant.
`avatar_skinning_generation_benchmark` builds the untracked native variant;
it runs timings only with explicit `--benchmark`. It never connects a viewer
session, logs in or dispatches worker tasks.

Build this PR's exact head separately using the normal 64-bit
`ReleaseFS_open_AVX2`, Nightly and NSIS defaults. Compare against #93 at identical
settings. Check loaded and arriving avatars, rigged attachments, outfit/LOD
changes, posing, sitting/standing, animesh, shadows, camera movement, travel and
exit. If a known legacy mesh with missing joints is available, verify stable
fallback geometry and existing diagnostics. Inspect the `Avatar skinning
matrices` Tracy zone independently from packing and collect matched crowd frames.

Next plan: audit and benchmark animation curve sampling/data ownership, with
exact interpolation and asset-edit behavior gates before storage or threading
changes. Larger worker changes still need frame-time attribution and safe
dispatch/apply boundaries.
