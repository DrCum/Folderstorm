# Checkpoint 14e: owned sampling floating-point context

Plan written before implementation. Parent: protected phase contract and defined
initial controller masks. No production worker dispatch is enabled.

## Objective

An owned sampling result must not depend on which unrelated job last ran on a
worker. Capture the owner's relevant numeric controls with its query, establish
those controls while sampling, restore the worker's previous environment on all
exit paths, and reject a result if current numeric controls no longer match.
Keep ordinary live serial playback free of this experimental preparation cost.

## Work sequence

1. Audit the actual viewer startup, platform intrinsics/CRT headers and supported
   compiler floating-point behavior. Identify rounding, denormal handling,
   exception masks/status and any precision controls that affect the kernel.
   Do not assume standard fenv alone captures FTZ/DAZ or compare opaque padded
   environment objects byte for byte. Use primary platform documentation where
   the local implementation does not establish the contract.
2. Define a nonallocating owned numeric-control descriptor and scoped restore
   guard for the target x64 AVX2 build. Fail preparation/sampling safely when a
   context cannot be captured or established. Unsupported platforms retain the
   normal direct path; do not silently claim equivalent background math.
3. Capture the descriptor on the owner thread in checkpoint 14c's preparer.
   Carry it in the move-owned request, include the relevant semantic controls in
   independent current-input validation, and establish/restore context inside
   the pure sampler. Keep workers away from live state/settings/caches. Update
   manually assembled test requests explicitly rather than relying on an
   accidental default context or copying context back from a completed result.
4. Test actual curve/JointMotion math with underflow-sensitive values and
   different rounding/denormal modes. Deliberately alter a reused worker's prior
   controls, require exact results against the owner's direct computation, and
   verify the prior worker controls/status are restored after success, rejected
   requests and failure paths. Include same-thread use and ordinary serial-path
   preservation; tests must restore their own caller state too.
5. Repeat owned-sampling and phase/controller fixtures in both curve modes and
   compile supported platform branches independently where available. Record
   any limits of compiler optimization, CRT capture or platform execution; avoid
   broad scheduler changes to solve a per-job numeric contract.
6. Measure the guard/preparation/application overhead separately using the
   existing complete-cost experiment. Do not combine allocator probes with timing
   or compare a new guarded worker path against an old cost report as if only
   sampling time mattered.
7. Publish this checkpoint separately and write the next plan for explicit
   ordinary-motion eligibility, independently current frame/activation/skeleton/
   edit identity and exact same-frame controller-time prediction. Preserve
   subclass callback order and real pose-consumer barriers. A predicted query
   still requires validation at application, and a real overlap window remains
   a measurement requirement before live dispatch/default enablement.

## Acceptance limits

No inherited stale worker mode, leakage into the next worker job, accepted
results from a different current numeric context, worker access to live objects,
changed ordinary serial behavior, or unsupported-platform equivalence claims.
Numeric controls do not replace asset immutability or lifecycle validation.
Native fixtures do not establish real crowd FPS or rendered/lifecycle acceptance.

## Results

Implementation has not started. Initial audit and native context probe completed.

### Initial evidence

- `LLVector4a::initClass` sets SSE flush-to-zero and nearest rounding. It does
  not explicitly set DAZ. The General pool starts three `std::thread` workers
  through `ThreadPoolBase::start`; neither that entry point nor queue delivery
  establishes an owned sampling environment per job.
- CMake selects `/fp:precise`, and the retained generated llcharacter project
  confirms `FloatingPointModel=Precise`. Microsoft's compiler contract still
  requires explicit environment-access handling for mode changes; a wrapper
  cannot assume arbitrary inherited rounding is supported just because the
  source is built precise. See [compiler floating-point behavior](https://learn.microsoft.com/en-us/cpp/build/reference/fp-specify-floating-point-behavior?view=msvc-170)
  and [fenv_access](https://learn.microsoft.com/en-us/cpp/preprocessor/fenv-access?view=msvc-170).
- A native x64 MSVC probe deliberately sets its owner to nearest rounding with
  FTZ and DAZ enabled: MXCSR `0x00009fc0`. Its fresh `std::thread` reads
  `0x00001f80`. This is an observed test-process startup difference, not a
  measurement of the logged-in viewer/General queue. It disproves relying on
  this runtime to inherit those owner controls at thread creation.
- The Windows SDK UCRT header defines `fenv_t` as control/status fields. A native
  probe with environment access enabled tests four rounding modes, four FTZ/DAZ
  combinations and four status patterns, all with exceptions masked. All 64
  fegetenv/fesetenv round trips restore the complete tested MXCSR exactly, with
  zero mismatches and the probe's caller state restored. This is evidence for
  this Windows runtime only, not a cross-platform guarantee or an unmasked-
  exception/actual-kernel equivalence test. See [fesetenv](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/fesetenv1?view=msvc-170).
- `_controlfp_s` alone is insufficient as a presumed full-state restore: its
  documented denormal-exception-mask behavior differs from `_control87`, and
  x64 precision/infinity controls are unsupported. See
  [Microsoft's control-word contract](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/controlfp-s?view=msvc-170).

Probe source/log: sibling `performance-validation/fp-context-audit14e.cpp` and
`fp-context-audit14e.log`. It runs with optimized precise x64 AVX2 settings and
`fenv_access(on)`, and does not access a viewer, shared build outputs or accounts.

Next: establish supported owner modes explicitly (reject/fall back where the
compiler/runtime contract cannot be honored), define the semantic descriptor and
exact worker restoration boundary, and test actual curve math under deliberately
different worker modes. No sampling payload, kernel or runtime caller has been
changed in this audit commit.
