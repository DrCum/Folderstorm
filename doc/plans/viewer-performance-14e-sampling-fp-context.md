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

Implementation, correctness checks and complete-cost measurements completed;
no live dispatch is enabled.

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

### Implemented contract

`NumericContext` contains semantic MXCSR controls, excluding sticky status flags.
Its capture supports native MSVC x64 with nearest rounding and all floating-point
exceptions masked, including each FTZ/DAZ combination. Other owner modes and
unsupported compiler/architecture branches reject owned preparation and retain
the original direct path. Clang/Intel compatibility macros and ARM64EC do not
silently opt in to the native MSVC x64 guarantee.

`ScopedNumericContext` saves the complete worker MXCSR, establishes the owned
controls with clear status, and restores the exact previous controls/status on
normal return, rejection and C++ unwind. It uses no allocation or viewer state.
The owned sampler enables local environment access and puts the scope outside
profiling/result operations, so early exits and profiling cleanup stay inside
the restoration boundary. Standard fenv object layout is not used as identity.

Owner preparation captures context alongside actual time/duration/usage/lease.
Current-input validation compares independently captured controls before any pose
write. Explicit test request builders now capture owner controls, including when
recycling buffers; batch copies use the independently prepared owner descriptor.
Ordinary virtual `onUpdate` remains direct and does not capture or change context.
The guard does not confer eligibility, lifecycle authority or curve immutability.

### Correctness evidence

- Standalone guard: **791 checks across 256 reused-worker control/status cases**,
  including owner denormal modes, different prior worker rounding/denormal modes,
  sticky flags, invalid descriptors, exception unwind and same-thread use.
- Actual kernel/application: **829 checks across 128 worker cases per storage
  mode**. Unguarded `JointMotion::update` reproduces **116 byte mismatches**;
  guarded samples match the owner's direct update exactly in all cases. Curves
  contain runtime-created subnormal values and ordinary rounding-sensitive values.
  Tests verify full worker restoration after success, invalid context/time/shape
  and a failure after sampling an earlier row; changed current controls reject
  live application, status-only changes remain valid, and unsupported owner
  rounding takes the exact direct fallback.
- Existing tests pass per storage mode: 24,193 exact joint-state comparisons /
  109,806 owned checks; 5,956 phase checks / 1,474 frozen actual-update comparisons;
  1,140 bounded-executor checks; 1,734 batching checks; 896 export/pose checks plus
  10,000 concurrent owned samples. No old curve/reference bytes are regenerated.
- A forced-unsupported branch rejects owned contexts and passes **5,040 direct/
  fallback checks and 1,440 frozen-update comparisons**. Other owned-only fixtures
  explicitly report skipped unsupported coverage; serial export tests still run.
  This is branch coverage on Windows, not execution proof for ARM/macOS/Linux.
- Complete sampler/context sources and standalone affected test/benchmark sources
  pass independent Tracy/contiguous syntax checks. Native optimized x64 AVX2
  programs use fresh context/character/controller/keyframe sources and read cached
  dependencies only. The shared cache is neither configured nor modified here.

### Measurement and handoff

The complete-cost experiment now calls `LLVector4a::initClass` for **every** mode,
including direct, and records that owner-mode policy in CSV metadata. Existing
plan 13 data used different startup conditions; a numerical difference against
that old CSV cannot be attributed solely to guard overhead. New same-run direct,
owned and worker comparisons include preparation, validation/application,
allocation/queue behavior and the context guard. Allocation instrumentation runs
separately; its timings are never used for performance conclusions.

The context-only benchmark links the actual guard as a separate object and uses
nine alternating paired samples, two million observed calls per stage/sample.
`results/sampling-context-guard-cost.csv` retains all 36 timing rows/checksums.
Median capture costs **7.307 ns/call**, or **5.982 ns** after the paired baseline;
worker guard entry/restoration costs **15.382 ns**, or **14.153 ns** net. The
worker starts with different rounding/denormal controls and sticky flags.
These are isolated costs on the target Ryzen/MSVC build, not frame-time gains.
`analyze_sampling_context_cost.py` verifies all rows, identities and checksums;
truncated, duplicate and incorrect-checksum files are rejected even under `-O`.

The complete experiment retains two 120-trial timing matrices and eight longer
CPU trials, **248 trials / 63,488 measured frames**, in
`results/sampling-context-cost-{run1,run2,cpu}.csv`. The existing analyzer passes
all accounting checks under `-O`; each trial also checks its final pose against
actual direct sampling. Preparation/validation/application stages are retained
alongside total distributions, wasted work, queue/fallback counts and CPU time.

For the default batch-8 large synthetic case (64 motions x 128 joints), the long
run's median is **2,963.0 us direct versus 2,127.7 us barrier/two workers** with
roughly 2 ms of separately generated independent work. Main CPU totals are
12,546.875 ms direct versus 8,937.500 ms main + 3,500.000 ms worker = 12,437.500 ms.
That small aggregate CPU difference does not prove improved CPU efficiency.
The medium case saves wall time but uses 9,625.000 ms aggregate CPU versus
9,406.250 ms direct. Windows CPU counters are quantized; these totals cover
4,096 frames, and neither result establishes the real viewer's overlap window.

Immediate barriers still mostly fall back; waiting pays roughly 2 ms pool wake
latency in the medium short trials. The two orderings also show noisy samples
(e.g. large serial-owned median 1,187.7 us in run 1 versus 898.6 us in run 2).
Keep those distributions rather than selecting the favorable result. The
evidence still rejects immediate/default live dispatch.

The separate allocation run retains 15 cases in
`results/sampling-context-cost-allocations.csv`. Direct and serial-owned modes
make zero post-warm replacement-new calls in the tested workloads; the context
guard itself allocates nothing. Payload descriptor bytes and queue/cancellation
churn are still charged and can dominate the scope's small isolated cost.
The existing fixture's current-query descriptor storage/known unchanged inputs
are not a measurement of the real protected phase bridge: that bridge currently
builds a temporary current usage vector. Its allocation/preparation cost needs
measurement/reuse before live integration, as recorded in plan 14f.

Reproduce the isolated data report with `python -O
indra/llcharacter/tests/analyze_sampling_context_cost.py
doc/plans/results/sampling-context-guard-cost.csv`; use the existing cost analyzer
for the three complete timing matrices. No allocator instrumentation is used in
any timing executable. No other viewer build was active during these runs.

Full viewer linking, rendered/lifecycle acceptance, real crowd frame times,
sanitizers and other platform execution remain pending. No FPS gain or real
overlap window is established by these generated assets.

Focused LL_TESTS targets: `avatar_sampling_numeric_context`,
`avatar_sampling_fp_math`, and the existing owned/phase/bounded/batch/serialization
targets. `avatar_sampling_context_cost --benchmark` is opt-in; build timing and
allocation targets separately and opt in explicitly.
Keep x64 AVX2 OpenSim Nightly normal controls Tracy OFF / contiguous keys OFF;
retain this exact dependency head separately. The other build agent owns private
viewer builds and monitoring; this work does not start or change them.

Next: `viewer-performance-14f-current-motion-context.md`.
