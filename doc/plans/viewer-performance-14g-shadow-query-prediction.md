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
