# Checkpoint 14f: explicit ordinary-motion eligibility and current context

Plan written before implementation. Parent: per-job numeric controls, owned
sampling and the base-motion phase contract. No live dispatch is enabled.

## Objective

Replace caller-invented test identities with independently obtained owner-thread
identity for ordinary keyframe motions. Define which actual factories may opt in,
prove sampled key immutability, and reject stale work at the original application
point. Do not treat a base pointer or generation number as proof of eligibility,
object lifetime, current inputs or freedom from a concurrent writer.

## Work sequence

1. Audit all construction/registry paths, subclasses and virtual update/activation
   overrides. Define an explicit capability granted by the known ordinary
   LLKeyframeMotion factory; inherited/custom walk, stand, fall, preview, poser
   and borrowed-data paths default to ineligible unless separately proved.
   Keep the protected base phase test API distinct from production eligibility.
2. Define owner-thread unique instance and activation generations, including
   deprecation, reactivation, stop, removal/replacement and counter overflow.
   Establish which live methods maintain them before adding fields. Worker jobs
   retain owned payloads only; a token must never authorize dereferencing an
   expired live pointer. Dispatcher/lifetime lookup remains main-thread work.
3. Capture actual current skeleton/publication/edit context and numeric controls
   independently at preparation/application. Audit every normal live writer of
   array layout, curve keys/counts/interpolation and the metadata read by time
   resolution. Seal, replace or copy-on-write sampled storage if any writer can
   overlap a worker; version increments alone do not make that race safe.
4. Represent actual frame-clock context without lossy hashing. Include the real
   frame timer time as well as count, or an audited epoch covering every update
   site, including modal/re-entrant work that updates time without frame count.
   Retain bit-exact sample/duration/usage/current-owner checks at application.
5. Exercise actual factories and lifecycle transitions with the real controller,
   deterministic clock and frozen update oracle. Cover same-ID new instances,
   stop/reactivate, cache removal/replacement, skeleton/binding/usage changes,
   edits, modal time changes, destruction, unsupported contexts and direct
   fallback. Verify normal virtual subclass behavior is never rerouted.
6. Keep ordinary playback serial. Do not introduce a speculative global avatar
   traversal, queue or changed callback order in this identity checkpoint.
   Measure actual owner preparation and current-input validation costs: the phase
   bridge currently builds a temporary usage vector, whereas the complete-cost
   fixture reuses current descriptors. Establish bounded reuse or a nonallocating
   current view before any live dispatcher; do not infer bridge costs from that
   fixture or from the isolated numeric guard benchmark.
   Full viewer builds and rendered/crowd acceptance remain separate evidence.
7. Publish a separately testable PR with exact dependency/head/build instructions.
   Write the next plan for a main-thread shadow prediction experiment: compare
   proposed prepass queries with the real controller's eventual query and reject
   mismatches without applying extra results. Only source-proven context and a
   measured same-frame overlap window can justify a live bounded dispatch trial.

## Acceptance limits

No inferred subclass eligibility, stale/live pointer use, token copied from the
result as current authority, silent mutable-key sharing, changed motion ordering,
additional animation frame, or default worker enablement. Context proof and
numeric equivalence do not establish a crowd FPS gain.

## Results

Not implemented yet.
