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

Implemented as protected move-only request/result types and a static sampler.
No normal playback path calls it and no viewer jobs are scheduled. Requests own
the const asset lease, masks, finite time/duration and five identity fields:
instance, activation, frame, skeleton and edit. Results retain the original
request by move, plus sampled values and applied-channel bits. Only flagged
channels are meaningful; unrequested values may remain in reusable storage.

The sampler reads selected curve fields under the publication/immutability
contract, preserves the original scale, rotation and position order and mNumKeys
gates, and touches no live character, joint state,
constraints or cache. One avatar-category scope attributes the whole request,
without per-key profiler events. Empty owned assets produce empty ready results;
null assets/joints, non-finite query scalars, over-limit joint counts and mask
shape mismatches fail with no ready output. Readiness is invalidated before
allocation/computation; allocation/curve exceptions still propagate to a future
executor and must be contained there.

The later checkpoint-14 mutation audit found an existing const serializer writing
stored position keys. Checkpoint 14a removes that writer, including size queries;
include that prerequisite before live sampling jobs. A const shared lease alone
does not enforce the absence of other writers. The fixtures in this checkpoint
were immutable and no production caller dispatched them.

Matching requires readiness, the same asset pointer/control block, all identity
fields, exact time/duration representations (including signed zero), full mask
equality and output shape. Current expectations must be prepared independently
from main-thread state; using the result's saved request only tests consistency,
not freshness. Instance/activation/edit counters are caller responsibilities and
are not wired to playback yet. No stale-result rejection is claimed integrated
into the viewer's existing main-thread application.

`takeRequest` consumes readiness and returns owned inputs/mask capacity for reuse;
sample capacity remains with the result. Moves transfer readiness to the
destination, and a moved-from result is not ready. Exclusive access is required
during sampling, result consumption and buffer recycling. A bounded pool and
complete allocation budget belong to plan 12; this API makes no allocation-free
frame claim.

### Verification completed

`avatar_owned_sampling`, enabled with LL_TESTS, links actual llcharacter code.
Both map and contiguous builds pass **24,193** byte-exact pose comparisons against
actual `JointMotion::update`/LLJointState setters and **109,806** total checks.
Systematic cases cover 36 assets with one through six joints, 0/1/2/4/15/32/65
keys, repeated timestamps, all three interpolation modes, eight channel masks
plus unknown bits, null-state encoding, key-count/container-size differences,
range/exact/interpolated queries, three durations and changed list metadata.
Unrequested transforms, usage, weight and priority remain unchanged.

Additional checks cover moved/recycled inputs and outputs, retained buffer
addresses/capacity, no extra retained asset references on submission, every stale
identity field, same-pointer/different-control-block ownership, exact query bits,
changed masks/shapes, malformed requests, empty requests and failed-result reuse.
An owned request is moved to a test thread after cache eviction, its moved-from
source is reused, and output still matches the serial oracle. The result keeps
the asset alive until its returned lease is released. That test thread calls no
live character/joint-state/cache/renderer APIs.

Isolated native validation compiles fresh complete character/controller/keyframe
sources with the test, linking cached unchanged dependencies read only. It does
not instantiate stale-layout cached avatar subclasses or run a viewer. Full
keyframe/poser syntax checks pass with Tracy and contiguous keys; standalone test
translation-unit checks separately verify includes rather than relying on the
combined native harness. The pose check supplies its existing chrono dependency.
MSVC 19.51.36244, x64 AVX2, `/O2 /fp:precise`. Full viewer linking, runtime/visual
acceptance, ARM/macOS and sanitizer execution remain pending.

### Handoff and next work

This is a contract/test checkpoint; a new installer is optional because existing
playback is unchanged. A native LL_TESTS build of `avatar_owned_sampling` is the
focused integration check. If building the combined viewer head, retain it as
usual and set `FS_CONTIGUOUS_ANIMATION_KEYS=OFF` explicitly for the map control.
The preceding ownership and hand-pose runtime checkpoints still need acceptance.
Plan 12 implements bounded execution and benchmarks full costs before dispatch
or application can be connected to the viewer.

### Application-oracle audit retained from planning

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
