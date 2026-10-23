# Checkpoint 11: owned animation sampling requests and results

Plan written before implementation. Parent: canonical animation asset leases.

## Objective

Define a pure sampling boundary that future jobs can use without reading a live
motion or character. Own the asset lease, time/duration, per-joint usage and
identity/version fields in request data. Own sampled outputs and enough request
identity to reject stale results. Keep viewer dispatch and normal playback paths
unchanged until batching/queue overhead and same-frame application are measured.

## Implementation sequence

1. Add narrowly scoped request/result types and a pure sampling function in
   llcharacter. Read only immutable keys/counts/interpolation from an owned const
   asset lease. Preserve scale/rotation/position order, masks, defaults and exact
   interpolation math. Do not call JointMotion::update, joint setters, skeleton
   lookup, character metadata, constraints or cache APIs from sampling.
2. Tag requests/results with motion-instance, frame, skeleton and relevant edit
   versions plus asset identity and query inputs. A retained old asset is safe
   to read but its result can be stale. Validate shape and reject incomplete or
   mismatched data before any application. Define reset/failure behavior so a
   reused result cannot appear ready with old data.
3. Use move ownership and reusable buffer capacity where appropriate. Avoid
   reference-count operations per key and avoid promising allocation-free frame
   behavior before a bounded buffer pool exists. Document which payload owns each
   buffer during preparation, execution, completion and discard.
4. Compare pure outputs with the actual original JointMotion::update applied to
   test LLJointStates, using real vector/quaternion math. Cover usage masks,
   empty/single/long/duplicate curves, interpolation modes, changing inputs,
   asset eviction/replacement, moved/reused buffers, malformed shapes and stale
   identity combinations. Keep list metadata writes out of the sampler.
5. Test request lifetime across a background sampling call, then compile both
   curve configurations. Publish the contract separately with explicit limits:
   it supplies no viewer jobs, live joint application, extra animation latency,
   runtime performance claim or default threading change.
6. Write the next plan for a bounded execution prototype and benchmark complete
   preparation/sample/validation/application cost, queue saturation, cancellation,
   shutdown, fallback and tail latency on the Ryzen 5 3600. Compare against direct
   serial updates and account for multi-viewer CPU contention before integration.

## Integration gates after this contract

Future jobs own their inputs; they never borrow masks/output storage that the
main thread can reuse while work is in flight. Bounded slots/queues prevent
unbounded memory or old-frame work. Main-thread validation precedes joint-state
writes, hand-pose priority and constraints in the original order and same frame.
Reject stale results after stop/restart, edits, skeleton/asset replacement or a
new frame, with deterministic serial fallback and no duplicate live writes.

Subclasses and embedded poser data need explicit eligibility; possession of a
lease alone does not prove an overridden motion update can be split safely.
Use existing thread infrastructure only after auditing nonblocking admission and
shutdown behavior. Measurement must show useful overlap and acceptable tail
latency; thread count alone is not an optimization.

## Results

Initial application-oracle audit started; no request/result code or dispatch has
been added yet. Plan 10b provides owned lifetime but does not make live metadata
or whole motion methods safe to run on workers.

- `JointMotion::update` returns immediately for a null joint-state pointer. For a
  real state it reads usage once, then updates scale, rotation and position in
  that order, gated by each usage bit and the curve's mNumKeys value. Null states
  can be represented by zero usage in a prepared request.
- LLJointState setters assert their corresponding usage bit and assign values;
  they do not enable usage or modify weight/priority. Seed all three transforms
  with known values before reducing masks in the oracle, and verify unrequested
  transforms, usage, weight and priority stay unchanged.
- Honor mNumKeys independently of map size, as the original method does. Include
  empty/default behavior and duplicate count/unique-size differences; do not
  invent a new gate based only on container emptiness.
- Prefer a static sampler taking an owned request value, with move submission.
  The result can retain the original request (lease, masks and identity) by move,
  avoiding a second mask copy. Mark a reused result invalid before shape checks
  or sampling. Exact current-input/version comparison must precede application.
- Worker sampling must never read list duration/loop/priority/hand-pose metadata
  opportunistically. The query owns the already computed time/duration. The
  const joint accessor and curve samplers provide the selected read boundary.
