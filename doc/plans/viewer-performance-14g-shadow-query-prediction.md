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

Prediction/observation is not implemented yet. The preceding 14f1 serial
allocation fix is implemented and measured; current owner preparation/application
costs are recorded there. Begin with the following source audit, then complete
the mutation/independence analysis before selecting an observation boundary.

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

These are implementation tasks, not completed results. The initial source audit
is complete enough to begin the clock/query contract; the complete live
observation boundary, global memory accounting and runtime measurements still
need implementation/validation. Parent is PR 110 at
`4adf6a948e32927afbafea78f5e8511ba552297f`.
