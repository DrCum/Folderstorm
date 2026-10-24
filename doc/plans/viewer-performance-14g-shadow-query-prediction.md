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

Not implemented yet. Complete 14f1's measured serial allocation removal first.
