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

Not implemented yet. Checkpoint 12 establishes protocol correctness and shows
that worker wake-up can dominate small jobs; the full-cost decision remains open.

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
