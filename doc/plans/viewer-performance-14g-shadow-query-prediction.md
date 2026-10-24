# Checkpoint 14g: nonmutating main-thread query prediction

Plan written before implementation. Parents: current owner context and measured
owner/name-allocation costs. This stage performs no worker dispatch or extra pose
application and does not establish a default threading policy.

## Objective

Determine whether an early ordinary-remote-motion query can match the actual
controller callback query in the same frame, and how much independent main work
lies between those points. Reject uncertain cases instead of changing motion,
constraint, blending, skeleton or attachment order to create an artificial gap.

## Work sequence

1. Audit the actual timer/delta/factor/quantum arithmetic, pause/hidden/force
   branches, loading/activation/deprecation callbacks and per-motion stop/ease
   decisions. Start with stable, ordinary remote motions; explicitly reject
   contexts whose real callback/time cannot be predicted without mutation.
2. Implement a nonmutating owner-side predictor retaining exact arithmetic order.
   Use actual current context from 14f, including clock time/count, lifecycle,
   asset revision, bindings/skeleton/appearance and numeric mode. A prediction
   must never read current authority back from its eventual result/observation.
3. Compare predictions at the original real-controller update call point through
   test-only or explicitly opt-in shadow observation. Preserve normal virtual
   callbacks, chronological order, adaptive skips and direct updates. Observe
   unused predictions and mismatches without applying any additional sample.
4. Test real factories/controller clocks across factors, pauses, hidden/forced
   updates, loading and activation, stop/ease/deprecation, quantum branches,
   modal clock changes and owner-context invalidation. Compare actual poses and
   callbacks with the frozen serial reference; cover exclusions explicitly.
5. Establish a viewer observation boundary that preserves LLVOAvatar appearance/
   root/sit updates and subsequent pose consumers, and tolerates active-list
   mutation/removal. Do not retain raw avatar pointers in asynchronous work.
   Any live shadow collection stays opt-in, bounded and aggregate; avoid
   per-avatar logging or silent account/capture operation.
   Account for retained owner-side joint-state bindings as well as owned request/
   result buffers; cap the admitted motion count and bytes, not just queue depth.
6. Measure prediction/preparation/current-validation overhead and the actual
   same-frame gap with the existing profiler/capture protocol. Separate source-
   proven independence from measured available time. The synthetic 2 ms window
   and an accurate unit predictor alone cannot establish useful viewer overlap.
7. Publish the predictor/observation checkpoint separately. Write the next plan
   either for a bounded off-by-default worker trial with independently current
   validation/direct fallback at the same barrier, or for rejecting this boundary
   and selecting another measured data-only candidate. Default enablement still
   requires matched crowd/quality/lifecycle evidence.

## Results

Prediction/observation is implemented and native validation is complete. No
workers are dispatched and no extra poses are applied. The checkpoint still
needs its own full viewer build and manual observation/crowd acceptance.

The observer is a lexical owner-thread scope at the built-avatar boundary,
before visibility/appearance/root preparation. Its thread-local stack allows one
main-scene batch globally, rejects nested avatar-scope opportunities and cancels lookup on
controller destruction. It retains owned queries/tokens only and resolves
cleanup through the controller's current owning containers. Default playback
creates no shadow buffers. `FSAvatarSamplingShadow` is false and nonpersistent.
No setting/account was operated here.

Admission caps are 32 motions, 64 KiB of accounted request/owner-binding storage,
and 32,768 retained logical curve keys. Canonical assets are existing data held
by leases; logical-key counts do not claim an allocator/RSS bound on those
assets or their metadata. One bounded preparation attempt can transiently exceed
the admitted buffer budget before immediate rejection/cleanup. Binding capacities
and all diagnostic leases are released at scope exit. No queue exists.

Preparation, validation, total hook occupancy and cleanup are accounted
separately. All-match gap buckets subtract previous measured hook occupancy;
first-ordinary-callback match buckets are also recorded. Later gaps include
earlier real animation work that a future worker could remove, so they are an
optimistic upper bound. Neither histogram alone proves a useful dispatch window.
The source audit and implementation sequence below remain the design record.

### Initial controller audit

- `LLMotionController::updateMotions` reads the frame timer as F32, subtracts
  `mPrevTimerElapsed`, records `mLastTime`, purges excess motions, then evaluates
  `mAnimTime + delta_time * mTimeFactor * mUpdateFactor` in that order. Guessing
  animation time from wall-clock delta or dropping the update factor is invalid.
- Paused nonforced updates run idle-motion handling, not normal callbacks. Forced
  paused updates retain the old animation time. Minimal/hidden updates refresh
  `mPrevTimerElapsed` without advancing animation time, and can load/deactivate
  motions. `LLCharacter::updateMotions` also applies its pause-request handling.
- The quantum branch can return after interpolation/loading without sampling;
  a new quantum interpolates/clears blenders before loading and callbacks.
  Although setting a nonzero step is currently disabled elsewhere, tests must
  cover the existing branch without enabling it for normal avatars.
- Loading callbacks occur before additive/regular passes and can activate/delete
  motions. Active iteration is chronological, split by blend type, with joint-
  signature coverage, pixel-area fading and early skips. Ordinary factory
  permission alone does not prove that a callback will occur that frame.
- Stop/ease-out/inactive branches may use the stop-clamped timestamp, the current
  animation timestamp, or zero before activation. Network stop callbacks and
  `stopMotionInstance` can change context before a normal callback. The
  inactive/deactivation and first-stop cases need explicit prediction exclusions
  or equivalent source proof. Unused predictions are an expected outcome, not
  permission to force an update.
- `LLVOAvatar::updateCharacter` selects hidden/forced/normal updates after root,
  appearance and animation preparation. Audit that preparation and subsequent
  pose/attachment consumers before choosing an earlier global/owner prepass.
  A source-correct query is not yet evidence for sufficient same-frame overlap.

### First implementation sequence

1. Add a nonmutating controller clock projection that reads the actual timer,
   previous elapsed value, animation time, pause/force state, update/time factors
   and quantum state. Retain the arithmetic order of the existing update. Return
   an explicit no-normal-callback outcome for paused/nonforced and same-quantum
   paths; reject nonfinite/unsupported predictions. Keep the normal update body
   as an independent oracle until exact native comparisons pass. A projected
   animation time alone does not promise any per-motion callback.
2. Prepare bounded candidate queries for currently active ordinary remote
   factory motions, with 14f's independent owner identity. Initially exclude
   stopped/first-stop and imminent timed-stop cases whose callbacks can mutate
   context before sampling; record their exclusion instead of forcing an update.
   Exclude newly loading/custom motions from admission. Store owned requests and
   instance tokens only, never a motion/avatar pointer for later lookup. A later
   callback uses the valid motion already supplied by the existing controller.
3. Add a query-only current-input check using the same comparison contract as
   result application, without computing an extra pose, building a result or
   trusting the prediction's identity as current authority. Exercise mismatch
   and unchanged cases against the 14f context fixtures. Owner-only auxiliary
   binding refresh is allowed; pose/time/stop/lifecycle callbacks are not.
4. Observe actual controller callback time immediately before the original
   virtual `onUpdate`, preserving its arguments, position in the branch and
   return handling. Account for activation/loading callbacks and early returns
   explicitly. Match by independently current instance identity, count unused
   candidates, and discard every batch at the update boundary. Native tests
   compare observed and unobserved actual controllers for exact poses, callback
   order, continuation, stops and state transitions. Do not replace virtual
   callbacks with base-motion phase application in this experiment.
5. Wire an off-by-default diagnostic setting to the owner-side avatar boundary
   after the built check and before visibility/appearance/root preparation.
   Every hidden/forced/normal exit must finish or discard the batch. Validate
   actual character pause-request handling as well as direct controller pause.
   Reject uncertainty from appearance/role/clock/binding mutations rather than
   moving the existing avatar preparation or pose consumers.
6. Keep diagnostics lazy and globally bounded across avatars, including request
   capacities and retained owner bindings. Define explicit motion/byte admission
   limits and distinguish new buffer bytes from canonical assets held alive by
   leases. Default playback must allocate no shadow buffers. Use aggregate
   counters and bounded timing histograms, with no per-avatar identifiers or
   logging. Measure preparation and validation cost separately from time between
   completed preparation and the actual callback. Do not include validation time
   as useful overlap. Disable/discard/logout paths must release retained leases.
7. Publish the complete projection/observer checkpoint with native evidence and
   explicit manual opt-in capture instructions. No setting will be enabled or
   account operated here. Write the following plan from observed coverage/gap
   data; sufficient real same-frame overlap remains a separate acceptance gate
   before a bounded worker trial. If the source boundary cannot provide useful
   independent work, reject it and select another measured kernel.

This sequence was written before implementation. Source/native results and the
remaining runtime gate follow. Parent is PR 110 at
`4adf6a948e32927afbafea78f5e8511ba552297f`.

### Native validation

- 5,063 assertions pass in each map/contiguous mode, including 1,440 clock
  projection cases compared with the actual unchanged controller arithmetic.
  Tests compare controller state before/after projection, exact poses, active
  order, stop state, callback arguments/masks/order and hand publication with an
  unobserved real-controller control.
- Coverage includes factors, hidden/paused/forced updates, pause-handle release,
  quantum interpolation/new-pose branches, loading/activation, pre-activation,
  stop/reactivation, cache eviction, metadata/usage/binding/skeleton/appearance/
  role, live numeric controls and modal clock changes, removal/replacement, and genuine deprecated
  duplicate-ID ownership. A repeated controller callback consumes a prediction
  once and counts the subsequent callback as unpredicted. A nested avatar scope
  cancels the outer remaining opportunity; it does not reorder the controller.
- Motion, buffer and logical-key admission limits are exercised independently.
  Controller destruction drops live lookup authority while the immutable lease
  survives until scope exit, then releases. Owner binding capacities return to
  zero after successful, rejected, cancelled and destroyed-owner batches.
- Separate allocation executables pass 26 checks per mode. Thirty-two warmed
  controller updates make 128 replacement-new calls / 3,840 requested bytes in
  both plain and explicitly disabled-scope paths: zero additional allocations.
  An enabled batch makes seven calls in this three-joint fixture, including
  four existing controller calls and three diagnostic calls, with zero retained
  live bytes. Failure injection at state, binding and usage allocation restores
  the collector, releases partial storage and continues normal playback without
  propagating the diagnostic allocation failure. Timings use different binaries.
- Existing phase regression passes 5,956 checks / 1,474 frozen updates per mode;
  current-owner regression passes 1,203 checks per mode, and initial/current
  masks pass 241 checks across 15 poisoned-storage cases. The owned suite passes
  109,806 checks / 24,193 exact poses and batching passes 1,734 checks per mode.
  Unsupported-context
  branch coverage passes 13 direct-playback/accounting checks; owned-only
  coverage is explicitly skipped. This is Windows branch coverage, not native
  execution on another compiler/platform.
- Owner numeric controls and status are preserved across projection, preparation
  and query checking, including clear/all-set sticky status and inexact clock
  arithmetic. The guarded functions use explicit precise/fenv compiler scopes.
  No floating-point control operation is added to default playback.
- Independent Tracy/contiguous syntax checks pass for affected production/test
  units and the complete avatar caller. The caller also passed normal-map checks;
  PCH-free caller checks explicitly include `chrono` for an existing transitive
  requirement. The new setting is valid XML, unique, Boolean, false and
  nonpersistent.

Native work uses fresh current core sources, optimized MSVC 19.51.36244 x64
AVX2, precise floating point and assertions enabled. During the independent
builder's work, 32 internal libraries were copied read-only to an immutable local
snapshot, each verified before/after copying. External packages/toolchain were
read only. No shared-cache configure/build/write or primary-source edit was
performed here. Scratch executables/logs and the snapshot manifest are in the
sibling `performance-validation` folder. Sanitizers, full viewer integration and
other-platform execution remain pending.

### Component costs

[Map allocation report](results/sampling-shadow-alloc-map.csv),
[contiguous report](results/sampling-shadow-alloc-contiguous.csv), and
[source/executable/dependency provenance](results/sampling-shadow-cost-provenance.json).

The control compiles the original PR 110 controller body against the same fresh
other core sources, class layout, fixture and immutable dependencies. The current
binary additionally includes the collector. Disabled timing constructs no scope,
matching the viewer's default control flow. Order is parent/current/current/
parent, nine samples of 1,000 updates per stage and size in each run. All 162 rows
/ 162,000 measured updates have valid accounting; current paired final poses
match, and all 27,000 shadow queries per current run match their actual callback.

Median ns/update on the verified Ryzen 5 3600:

| Joints | Parent direct | Current direct | Shadow enabled |
| --- | ---: | ---: | ---: |
| 3 | 739.50 | 732.85 | 1,769.05 |
| 32 | 5,416.75 | 5,448.65 | 6,848.15 |
| 128 | 31,239.70 | 31,384.75 | 34,015.90 |

The direct differences are within about 1% in this fixture; do not attribute
small variations to a whole-viewer improvement. Shadow collection intentionally
adds about 1-3 microseconds here. The generated warmed four-key curves use fixed
time, one motion/character and no constraints/emote. Full controller blending is
included; real avatar preparation, caller setting/optional handling, rendering,
worker wake/dispatch and a real overlap window are not. No FPS claim.

Raw runs: [parent 1](results/sampling-shadow-cost-parent-map-r1.csv),
[current 1](results/sampling-shadow-cost-current-map-r1.csv),
[current 2](results/sampling-shadow-cost-current-map-r2.csv),
[parent 2](results/sampling-shadow-cost-parent-map-r2.csv).

### Build and manual observation

Build this checkpoint's exact head separately with the accepted private x64 AVX2
OpenSim Nightly Velopack configuration, Tracy OFF and contiguous keys OFF. The
independent builder retained parent PR 110 as `7.2.5.82249`; that is not evidence
for this newer head. Do not create extra profiling/storage variants automatically.

With LL_TESTS, `avatar_sampling_shadow` runs correctness coverage.
`avatar_sampling_shadow_allocations --allocations` and
`avatar_sampling_shadow_cost --benchmark` are separate opt-in executables; the
allocation binary does not provide timings. Unsupported owned contexts report a
skip and retain direct playback. Existing phase/current-owner/mask tests remain
relevant. No account, setting or logged-in viewer was operated here.

Once a retained build is available, the deferred manual request is:

1. With builds idle, warm assets in a repeatable crowded scene. Record the exact
   build, settings, camera, avatar counts, focus/session count and sample duration.
   Compare normal FPS/frame times with `FSAvatarSamplingShadow` FALSE.
2. Deliberately set `FSAvatarSamplingShadow` TRUE in Debug Settings for a short
   observation interval, then FALSE. It applies no extra poses or workers and
   adds diagnostic CPU cost. Do not use its enabled FPS as an optimization gain.
3. Save only the `AvatarSamplingShadow` aggregate line emitted on disable/next
   avatar update, or shutdown. It contains counts and timing buckets, without
   avatar names, IDs or locations. Other surrounding log content can be private.
4. `scopes` counts enabled built-avatar scopes; `batches` counts eligible remote
   batches. `predicted = matched + mismatched + unused` after all scopes finish.
   `unpredicted` records ordinary callbacks without an unused matching instance
   candidate; it can include repeated callbacks. `nested` counts nested avatar
   observation scopes. No count authorizes forcing a motion callback.
5. Gap bucket i covers up to `2^i` ns; bucket 31 also includes larger values.
   The first histogram covers all matching callbacks; the second only matches
   at the first ordinary callback. Prior measured hook time is subtracted, and
   the current validation is excluded. Later gaps include earlier animation work
   a worker might remove. Wall-time gaps and successful query matching do not
   establish independent CPU time or a useful queue deadline by themselves.
   Preparation, observation and cleanup are separate costs; validation is a
   subset of observation and must not be added twice.
6. Include normal/hidden transitions, appearance/outfit/skeleton changes,
   sit/stand, animesh/preview/poser, arrival/departure and teleport/logout/exit
   checks. Turn the diagnostic off afterward. It does not persist across runs.

No real viewer shadow capture, rendered/lifecycle acceptance or whole-frame
CPU/FPS measurement has been collected. Those gates remain open before a bounded
worker trial or any default enablement. The native generated gap is not a
substitute.

### Next plan

The full-controller probe exposes remaining allocation work beyond keyframe
sampling. [Plan 14h](viewer-performance-14h-purge-allocation.md) separately verifies
and removes the unconditional idle purge collector allocation on MSVC. After
that, write the active-blender collection plan from measured costs, preserving
blend order and quantum behavior. This independent serial work can proceed while
manual shadow/quality evidence is pending; it does not close the threading gate.
