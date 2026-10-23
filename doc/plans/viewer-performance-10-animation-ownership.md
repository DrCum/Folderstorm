# Checkpoint 10: owned animation assets for future worker sampling

Plan written before implementation. Parent: experimental contiguous build variant.

## Objective

Give a sampling job a stable owner for key data without changing animation
timing or dispatching work yet. Audit whether the existing canonical motion list
can support an owned immutable key view. Keys, interpolation modes and counts
appear to be populated before publication; loop/preview metadata has separate
main-thread writers. Prove those read/write boundaries before relying on them.

Avoid repeated per-frame curve copies. An ownership lease should keep the
canonical data alive through cache removal/replacement while a future job reads
only proven immutable fields. If the audit finds post-publication key mutation,
design versioned immutable replacement or a snapshot at that boundary first.
Const methods or reference counting alone do not establish thread safety.

## Work sequence

1. Re-audit every key/map/count/interpolation writer and JointMotionList vector
   mutation, including load failures, BVH preview, posing, cache replacement and
   shutdown. List exactly which fields sampling reads and which metadata fields
   main-thread setters may change. Inspect all cache callers and raw list aliases.
2. Design shared ownership for cached lists and ordinary LLKeyframeMotion
   instances while preserving existing raw views where required by subclasses.
   `FSPosingMotion` points at an embedded dummy list; it must not be given a
   deleting owner or admitted to workers through a missing/borrowed lease.
3. If the audit supports it, make cache/motion ownership explicit and provide an
   owned const sampling lease. Keep cache operations on their current thread.
   A worker must receive the lease in its request, never look up the shared cache
   or retain only a raw curve/avatar pointer. Destruction on a worker must be
   audited to contain no character, UI, renderer or GL operations.
4. Add focused actual-ownership lifetime tests: active leases survive removal,
   replacement and clear; old/new assets are distinguishable; final owners free
   data once; failed decoding publishes nothing; embedded dummy views retain
   their original ownership. Compile affected cache/subclass/preview callers.
5. Publish the ownership change as a separate lifecycle checkpoint with Windows
   load/preview/reload/teleport/logout/exit acceptance. Keep both curve build
   variants compiling. No worker pool or new animation frame of latency yet.
6. Write the next plan for owned request/result buffers, motion-instance and
   frame versions, cancellation/admission bounds, deterministic serial fallback
   and a same-frame apply barrier. Measure realistic batching and job overhead
   before enabling concurrent sampling.

## Required worker contract after this checkpoint

Main-thread query preparation owns looped time, duration, per-joint usage masks,
motion instance/frame identity and asset generation. Worker access is restricted
to immutable keys/interpolation/counts and owned query/output buffers. Joint
state writes, constraints, hand-pose priority, skeleton lookups and avatar/cache
lifecycle calls remain on the main thread in their original application order.

Reject results after a motion stop/restart, asset replacement, changed skeleton
or relevant edit. Holding old data alive makes dereferencing safe; it does not
make old results current. Multi-viewer CPU admission and crowded-scene speedup
remain measurement gates on the Ryzen 5 3600.

## Results

Not implemented yet. Initial evidence from plan 6 identifies raw pointer ownership
as a blocker for borrowing curves on workers; this plan resolves that boundary.
