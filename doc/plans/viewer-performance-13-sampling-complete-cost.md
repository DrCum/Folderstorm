# Checkpoint 13: complete-cost bounded sampling measurement

Plan written before implementation. Parent: checkpoint 12's test-only bounded
execution protocol and wake benchmark. Default playback and pool scheduling stay
unchanged.

## Objective

Determine whether owned curve sampling can save main-thread time within the same
frame, after accounting for preparation, batching, callback allocation, wake-up,
sampling, validation, output application, fallback and discarded work. Compare
with the real serial JointMotion update path and the serial owned request/result
path. Do not interpret a worker kernel timing as a viewer speedup.

## Work sequence

1. Build a test-only opt-in executable around the actual protected sampling
   contract and bounded executor. Reuse request/output capacity, prepare current
   inputs independently, and compare final joint-state bytes with serial updates
   outside timing. Include all usage gates and preserve original apply order.
2. Model a batch as owned requests/results, with no live joint-state pointers or
   asset-cache access. Test uneven/failing batches and ownership before timing.
   Bound jobs, callbacks, joints, retained buffers and total requested bytes.
   Move returned request masks into reusable storage rather than hiding repeated
   allocations behind the preparation stage.
3. Compare direct serial updates, serial owned sampling, one- and two-worker
   batches. Vary motions, joint counts, key lengths, forward/seek queries and
   batch sizes. Report actual pool width and total work, rather than assuming
   one motion corresponds to one avatar.
4. Measure both a test-only complete round trip and a same-frame application
   barrier with immediate serial fallback. Any benchmark completion wait must
   have an explicit test timeout and must not become the viewer algorithm.
   If synthetic independent main-thread work is used to vary overlap windows,
   label it and include its cost in every comparison. Measure cold and warm
   queues separately. Use the existing Windows scheduler with a documented
   scoped timer request; keep stock-scheduler comparison test-only if included.
5. Attribute preparation, queue/submission, wake/worker execution, validation,
   application and fallback. Report full median/p95/p99/max latency, main CPU,
   worker CPU, accepted/rejected/ready/cancelled/abandoned counts, queue high-water,
   useful/duplicate/discarded work and bounded peak/retained allocations. Keep
   allocation instrumentation out of timing runs and do not drop slow samples.
6. Include saturation, disabled/missing workers and controlled background CPU
   contention. Correctness tests use explicit gates, not timing assumptions.
   Retain raw output, source/configuration, compiler flags, CPU and sample counts.
   Repeat selected cases without concurrent compiler activity; record remaining
   uncontrolled desktop load and noise.
7. Use the evidence to accept or reject this boundary. If unsuitable, write the
   next independent plan for a more substantial batch, a different boundary or
   a narrowly scoped scheduler experiment, without enabling production work.
   Only demonstrated useful overlap justifies planning opt-in playback
   integration, which must separately wire live generations, eligibility,
   ordered application and visual/lifecycle checks.

## Acceptance limits

No worker touches live character, joint-state, UI, settings, cache or GL objects.
No main-thread queue-capacity wait, unbounded queue or reuse of running buffers.
Late/stale results must never apply. Failure and missed barriers must produce
the same current serial pose, with bounded duplicate work and predictable memory.

No default threading/scheduler enablement or performance claim without matched
real-viewer crowd captures and quality/lifecycle acceptance. The user's deferred
questions remain in `viewer-performance-questions.md`.

## Results

Implemented as test-only batching, correctness tests, a timing executable and a
separately instrumented allocation executable. Existing playback, scheduler,
settings and viewer thread counts remain unchanged. The evidence supports further
overlap-boundary investigation, not immediate/default worker enablement.

### Implementation and correctness

`sampling_cost_fixture.h` batches up to 32 owned requests over the actual sampler.
The fixture caps 128 motions, 216 joints per motion and 128 keys per channel;
worker callbacks contain no live motion/character/joint-state/cache/UI/GL pointer.
It retains and recycles the moved request envelope, mask arrays and result
capacity. It invalidates every row of a partially failed batch, and validates all
rows against independently prepared current requests before applying any row.
Application and direct fallback preserve motion/joint and scale/rotation/position
order. Per-motion states retain usage, weight, priority and unrequested transforms.

`avatar_sampling_batch` passes **1,734 checks in each storage mode** with the real
JointMotion/LLJointState serial oracle: one/uneven/multiple batches, one/32/128-key
curves, forward/seeking queries, repeated envelope/mask/output address reuse,
partial failure, an edit on a later batch row, malformed batch sizes and a held
callback missing its same-frame barrier. A deterministic acknowledgement/reuse
check ensures observation counts the current slot generation after submission;
an old ticket can expire between a prior scan and a new admission. The executor
itself is unchanged from checkpoint 12. A deliberately broken partial-failure
variant fails the readable-row/lease check.

Both new C++ translation units pass independent native syntax checks with map/
Tracy off and contiguous/Tracy on (including the allocation variant). Native
execution compiles fresh complete character/controller/keyframe sources and links
cached unchanged dependencies read only. MSVC 19.51.36244, x64 AVX2,
`/O2 /fp:precise /std:c++20`, assertions enabled. No viewer is built/run here.
Full viewer linking, runtime/visual acceptance, sanitizers and ARM/macOS execution
remain pending.

### Measurement method and coverage

The uninstrumented executable is `avatar_sampling_cost_benchmark`. Invoke
`--benchmark`, then `--benchmark-reverse` to reverse comparison order. `--smoke`
is a short launch/correctness check, not a performance sample. Timing never runs
as a build-time pass/fail check.

The retained map-storage runs each contain **120 trials and 15,360 measured
frames**, with eight warm-up frames per trial. Every trial checks its final pose
and metadata against a fresh direct serial fixture outside timing/CPU measurement.
The matrix varies 8/32/64 motions, 20/64/128 joints, 1/32/128 keys, batch sizes
1/8/32, forward/seeking queries, one/two workers, back-to-back versus 5 ms requested
idle, one-slot saturation and disabled workers. Selected matched comparisons
also run with two controlled CPU-load threads. Motions are work units, not avatar
counts. Four generated shared assets model reuse; real animation mixes differ.

The direct path calls actual JointMotion updates. The serial owned path includes
current-input preparation, copies, sampling, validation, application and recycling.
The nonwaiting barrier path uses actual nonblocking tryPost and immediately uses
direct serial fallback for rejected, failed, stale or unfinished batches. Running
work keeps exclusive buffers and is discarded later. Counts include useful,
cancelled, abandoned and wasted sampling, with observed slot/queued high-water.
Observation uses atomic ticket states, not a blocking queue-size call.

The separate `wait` mode measures a complete test round trip, with an explicit
five-second timeout. Its callback wrapper/condition-variable notification costs
are included. That wait is not a playback algorithm. Batches contain owned timing
records; records are inspected only after acquisition or pool drain/join. Stage
timings include clock/observation overhead; serial and worker paths both use the
owned sampler instrumentation. Frame totals include all preparation, submission,
validation, recycling, application and fallback. Separate stage quantiles do not
add up to a frame quantile.

`--cpu` adds eight selected direct/two-worker barrier trials of 4,096 frames each
to improve aggregate CPU resolution. It uses the same frame pipeline and exact
final-pose check. This longer dataset is separate from the two full matrices.

Synthetic overlap is a calibrated near-2 ms integer workload with fixed iterations
within each comparison and separately reported actual timings. Every mode pays
for it. It represents a possible overlap window, not actual viewer work. Iteration
counts differ between the two calibration runs. Compilation/validation processes
were stopped during timing; remaining desktop load was uncontrolled. Run 2 has
substantial noise in some no-overlap medium-workload serial samples. Retain those
samples and counts rather than attributing every elapsed-time difference to
parallelism.

### Results and decision

Raw metadata, all stage median/p95/p99/max distributions, sample counts, admissions,
fallback/waste and cumulative CPU readings are in `results/sampling-cost-run1.csv`
and `sampling-cost-run2.csv`. `tests/analyze_sampling_cost.py` checks complete-matrix
and accounting invariants and prints selected comparisons. Both retained runs
pass: no sampling/stale failures, slot limits respected, accepted jobs accounted
for, and useful plus fallback work equals the requested pose workload.

Selected batch-8/two-worker cases, microseconds, with no controlled background load:

| Workload / overlap | Direct median, runs 1 / 2 | Barrier median, runs 1 / 2 | Barrier p95, runs 1 / 2 |
| --- | --- | --- | --- |
| 32 motions x 64 joints, none | 180.2 / 256.6 | 195.0 / 204.7 | 252.5 / 300.7 |
| 64 motions x 128 joints, none | 924.8 / 939.8 | 957.8 / 1,023.6 | 1,379.9 / 1,415.3 |
| 32 motions x 64 joints, synthetic | 2,363.7 / 2,307.3 | 2,197.2 / 2,145.5 | 2,237.3 / 2,178.7 |
| 64 motions x 128 joints, synthetic | 3,102.5 / 3,082.0 | 2,245.8 / 2,143.8 | 2,745.3 / 2,495.3 |

With no independent work, the medium barrier applies **zero worker jobs** in both
runs and falls back for all 4,096 motion updates. Its apparent run-2 advantage
cannot be credited to useful parallel sampling. The larger barrier applies only
6/37 jobs out of 1,024, while wasting 17,408/300,032 sampled joint rows. Complete
waiting round trips take about 2.0 ms median for the medium case and 2.18/2.24 ms
for the larger case, worse than their direct samples. Moving these jobs to the
existing queue immediately before consumption is rejected.

With synthetic overlap, the medium barrier applies 511/512 jobs out of 512 and
the larger one applies 987/976 out of 1,024. The larger case still wastes
36,864/47,104 sampled joint rows and falls back for 296/384 motion updates. Both
runs demonstrate potential main-thread latency savings when a sufficient
independent window exists. They do not prove that window exists in the viewer,
that total CPU usage improves, or that FPS/animation quality is preserved.
Plan 14 audits the real update/dependency boundary before integration.

Cumulative GetThreadTimes main/worker CPU is retained, including final drain and
private-pool close; frame wall times exclude that final cleanup. Short samples
quantize to 0 or 15.625 ms increments, sometimes larger than a short trial's wall
interval. A zero worker reading is not evidence of no worker CPU. These counters
do not support precise per-frame or whole-viewer CPU-efficiency claims.

The longer run (`results/sampling-cost-cpu.csv`, **32,768 measured frames**)
reduces quantization relative to aggregate workload. Its accounting checks pass.
Selected synthetic-overlap results over 4,096 frames per row:

| Workload / path | Median frame us | Main CPU ms | Worker CPU ms | Sum CPU ms |
| --- | --- | --- | --- | --- |
| 32 x 64, direct | 2,380.6 | 9,765.625 | 0 | 9,765.625 |
| 32 x 64, barrier | 2,176.8 | 8,921.875 | 906.250 | 9,828.125 |
| 64 x 128, direct | 3,072.1 | 12,687.500 | 0 | 12,687.500 |
| 64 x 128, barrier | 2,248.6 | 9,468.750 | 4,390.625 | 13,859.375 |

In this generated workload, main-thread latency falls while total measured CPU
is roughly unchanged for the medium case and about 9% higher for the larger one.
The larger barrier wastes 1,320,960 sampled rows and falls back for 10,664 motion
updates. With no independent overlap, measured total CPU rises from 875.000 to
906.250 ms for the medium case and 3,687.500 to 3,796.875 ms for the larger one.
This is a latency/headroom tradeoff, not free CPU efficiency. Residual desktop,
frequency and scheduling effects remain uncontrolled; these figures cannot set
a default worker budget for one or multiple real viewer processes. Q3 and matched
crowd captures still govern that decision.

### Allocations and bounds

`avatar_sampling_cost_allocations --allocations` is compiled with replacement
C++ new/delete accounting. Its executable refuses timing modes; the timing build
contains no replacement allocator. The probe self-checks ordinary/aligned routes
and frees before collecting data. It uses a fixed 1 MiB registry with no recursive
allocation. Counts represent allocations routed through these C++ replacements,
not all malloc/DLL/OS allocations, allocator overhead, worker stack reservation or
resident memory. Existing assets, fixture states and fixed owned observation
windows are excluded, as is the fixture's preallocated current-query descriptor
array (capacity times sizeof SamplingRequest). Its per-query mask allocations
during preparation are counted. A complete application budget must add that
descriptor storage or supply it from an existing owner-side structure.
Pool/slot/request/result setup and warm-up inside the scope are included;
post-warm counts cover 32 frames plus final drain/close. Baseline tracked bytes
account for library allocations retained across trials.

Retained output is `results/sampling-cost-allocations.csv`. Direct and serial owned
paths perform zero post-warm new calls in the tested cases. For 32 x 64 work,
serial owned storage requests 113,760 bytes; the two-worker complete round trip
peaks at 212,072 bytes above its tracked baseline and makes 547 post-warm new calls
(47,104 requested bytes), including queue/callback/wait instrumentation. The
64 x 128, batch-32 barrier peaks at 301,168 incremental tracked bytes and requests
1,154,048 bytes across 2,015 post-warm calls as cancelled/abandoned envelopes are
rebuilt. Timing from this probe is deliberately suppressed. Its scheduling and
counts are not a substitute for the uninstrumented timing runs.

At most the configured slot count of submitted payloads/callbacks is admitted;
that count never grows across missed barriers. Fixture configuration bounds
prepared/current motion and joint counts, and actual result capacities are
reported per batch (maximum 203,264 bytes in the retained allocation cases).
Owner input/output storage and in-flight slot storage are separate; both must be
budgeted. Observation records use a fixed trial-length window, retained by shared
ownership until workers drain; they are measurement infrastructure, not a
proposed live frame-buffer allocation strategy. No production byte-budget or
admission-policy API is introduced by this fixture.

### Build handoff and next work

This checkpoint changes test targets only; a new installer is optional. With
LL_TESTS, build/run `avatar_sampling_batch` in both curve modes. Build the timing
and allocation executables separately and opt in explicitly as above. Use normal
x64 ReleaseFS_open_AVX2/Nightly/NSIS defaults for any combined viewer build and
retain the exact head separately, with contiguous keys explicitly OFF for the
map control. Do not add newer feature heads implicitly. The shared build cache
was read only throughout this work.

Plan 14 investigates safe same-frame overlap in actual motion-controller/avatar
ordering. Default dispatch, scheduler changes and whole-viewer performance
claims remain gated on real-scene evidence and visual/lifecycle acceptance.

### Initial source audit and implementation decisions

- The actual serial oracle is `JointMotion::update` on each motion's own joint
  states. It reads usage, then applies scale/rotation/position gated by mNumKeys.
  Preserve that motion/joint order for result application and direct serial
  fallback. A fallback should not silently pay for an extra owned-sampler round
  trip when the original direct update suffices.
- A batch must own all its request/result vectors. Moving the outer request
  vector into a callback and destroying it after sampling would introduce one
  hidden container allocation per resubmission. Retain its moved-from request
  envelope with the batch output, then return/repopulate it from each result's
  takeRequest when recycling on the owner thread. Measure initial warm-up and
  later capacity growth separately rather than assuming reuse worked.
- The bounded executor clears discarded outputs on worker completion. A batch's
  discard operation must release leases without allocating or depending on its
  owner/executor; restoring reusable envelopes belongs to the owner after take.
  A partially failed batch must not expose earlier rows as a successful batch.
- Keep authoritative current masks/versions independently on the owner thread.
  Result-owned copies cannot serve as the stale-result check. Count the copy or
  rebuild cost, reference-count operations and per-row validation in total time.
- WorkQueue tryPush uses a try-lock as well as capacity/closed checks; a false
  enqueue can mean contention even below capacity. Count it as admission failure
  and use serial fallback rather than retrying in a main-thread loop. Outstanding
  callbacks remain limited by executor slots across repeated barrier misses.
- GetThreadTimes reports cumulative quantized CPU time. Aggregate long enough
  and report its resolution/zero readings honestly; per-frame wall-stage timing
  is a different metric. Allocation probes must run separately from timing,
  with requested bytes distinguished from allocator/OS resident-memory costs.
