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

Initial source audit completed; ownership code has not been changed yet.

Implementation sequencing update: a closer audit found two prerequisites for
releasing old assets safely. Character hand-pose/priority entries currently point
inside assets, and setupPose appends per-motion constraints rather than clearing
old ones. Checkpoint 10a makes hand-pose values character-owned in a separate PR;
owned adoption will explicitly clear obsolete constraints before old-owner
release. Do not assume retaining an owner only through setupPose is sufficient.

| Worker-readable field | Writers found | Publication rule |
| --- | --- | --- |
| Curve key maps and values | Constructors/destructors and the two validated builder paths in deserialize | New motion list remains private until all keys/constraints validate. |
| Curve interpolation modes and key counts | Constructors/destructors, deserialize header unpack/mode assignment | No post-publication key/count/mode writers found in production callers. |
| Joint-motion vector and pointers | Construction, deserialize append and list destruction | Stable after publication while the list is alive. |
| Loop keys/points, priority, hand pose, ease and emote data | Existing main-thread setters/setup | Mutable; exclude from worker reads and capture needed scalars on the main thread. |

`JointMotion::update` reads per-instance joint usage, curve counts and sample
results, then writes live joint state. A future query must own the usage bits and
time/duration inputs; it must not invoke that method on a worker. Const sampler
methods read keys and interpolation mode, with no loop-key reads. Serialization
also reads key maps without retaining iterators.

The cache map is only accessed inside llkeyframemotion.cpp. Other production
callers remove entries or clear the cache for upload validation, preview/reload
and shutdown. Only onInitialize gets an entry, and successful deserialize adds
one. Existing replacement assigns a raw pointer without deleting the old entry;
remove/clear directly delete entries. Do not reinterpret these raw pointers as
independent shared owners.

Preferred implementation: retain the existing raw `mJointMotionList` view for
hot accesses and subclass compatibility; add a shared owner for ordinary cached
assets. Centralize owned adoption and provide a protected owned const lease only
when owner.get() matches the current raw view. The poser constructor's embedded
dummy list remains a borrowed view with no sampling lease. Change cache values
to shared owners and remove manual deletes; getters return the same owner.
Reference-count copies belong at attachment/request boundaries, not per key.

List destruction deletes joint-motion objects, key containers and constraint
shared-data objects. The latter only delete an owned index array; no character,
UI or GL action was found in those destructors. Per-motion constraints reference
that shared data and are deleted in the motion destructor before its members.
Retain the previous owner through pose/constraint rebinding on asset replacement
until this lifetime ordering is verified in actual ownership tests.

Hand pose and priority pointers temporarily published through character animation
data are another main-thread alias: LLHandMotion consumes and removes both entries
on update. Inspect motion removal/replacement ordering before releasing an old
list; leases protect key reads, not these character-side raw aliases. There is no
claim that all character metadata is immutable or independently thread safe.

Planned verification uses an LL_TESTS target linked to actual llcharacter cache
and motion code, with a derived probe exposing protected adoption/lease hooks.
Test cache eviction/replacement/clear and final destruction, borrowed dummy views,
and failed publication. Isolated native validation may compile the updated complete
llkeyframemotion.cpp with that test and cached unchanged dependencies; it must
not be reported as a full viewer link or exercise stale-layout cached subclasses.
