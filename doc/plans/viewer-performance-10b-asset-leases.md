# Checkpoint 10b: canonical animation asset leases

Plan written before implementation, following character-owned hand-pose data.

## Scope

Complete the ownership portion of plan 10. Retain raw motion-list views for
existing main-thread sampling and embedded poser data. Add a shared owner for
ordinary loaded assets and return an owned const lease to future sampling code.
Do not copy complete curves per frame, change playback scheduling or dispatch
worker tasks in this checkpoint.

## Implementation sequence

1. Change the keyframe cache's values and get/add APIs to shared owners. Only
   llkeyframemotion.cpp gets/adds entries; preview/upload/shutdown callers retain
   remove/clear APIs. Remove manual deletes. Old owners survive replacement or
   eviction until their final motion/request owner releases them.
2. Add a shared owner beside LLKeyframeMotion's existing raw view. Centralize
   owned adoption. When changing lists, delete obsolete per-motion constraints
   before releasing the old owner; setupPose currently appends new constraints.
   Do not deactivate old constraints against newly decoded joint states.
3. Convert a successfully decoded unique_ptr once, at the existing publication
   boundary, then attach/cache the same shared owner. Cache hits attach that
   owner. No shared owner is ever constructed from an already owned raw pointer.
4. A protected sampling-lease accessor returns a const owner only when its address
   matches the raw view. Embedded FSPosingMotion dummy data stays borrowed and
   produces no lease. Do not give workers cache access or whole live motions.
5. Retain only proven immutable key/count/mode reads on future workers. Loop,
   priority, emote and hand-pose metadata remains main-thread owned; required
   query scalars will be copied later. A const shared list is not a blanket claim
   that every field or pointer reachable from it is immutable.

## Verification

Add an actual llcharacter-linked ownership test with a derived probe for the
protected hooks. Test cache get/remove/clear/replacement, distinct old/new assets,
last-owner destruction, borrowed dummy views, no lease on view/owner mismatch,
and obsolete constraints released before old shared data. Exercise failure paths
without publishing partial assets and lifetime across an owned background lease;
no worker may call cache/character/UI methods. Count payload destruction once.

Retain the hand-pose lifetime checks from 10a, adapting their fixture to the owned
cache API. Compile affected full units, fall-motion/base-field consumers, posing,
preview and upload callers with both curve storage configurations as appropriate.
An isolated native test may compile fresh character/controller/keyframe sources
and cached unchanged dependencies, but must not exercise stale-layout cached
subclasses. Full viewer/PCH rebuilding and runtime acceptance remain separate.

## Runtime and next plan

Publish separately for animation loading, preview/reload, active cache eviction,
outfit/skeleton changes, animesh, teleport/logout and exit. Check that live assets
remain valid after cache removal and that memory releases when motions/leases end.
Character-owned hand-pose data removes the former asset-pointer alias, but live
character state and constraints still require normal main-thread ordering.

Then plan owned request/result buffers, motion/frame/asset versions, bounded job
admission, cancellation and deterministic same-frame application/fallback. Measure
job overhead and tail latency before enabling workers. Source ownership work
alone does not establish a crowded-scene FPS gain.

## Results

Implemented. Cache entries and ordinary motions share a canonical owner; cache
hits attach that same owner, and successful deserialization converts its unique
owner once at the existing publication boundary. Remove/clear drop cache owners
without deleting assets still held by motions or leases. Replacement can release
obsolete data when its final owner ends rather than losing its raw pointer.

The existing raw list view remains the main-thread sampling path. Protected
adoption clears obsolete per-motion constraints before old-owner release.
Adopting the same list preserves its constraints. The protected const lease is
empty for embedded/borrowed views or owner/view mismatch; no owner is constructed
from a borrowed pointer. Const list accessors now return const joints for leases.
This protects lifetime and ordinary const access, not every mutable field reachable
through the list's existing public raw-pointer containers.

No worker dispatch, per-key reference counting, curve copying, animation timing
change or main-thread application relocation is included. Ownership adds a shared
pointer per ordinary motion and an asset control block. Sampling leases must be
created on the owning thread; cache operations and live motion access remain on
their existing thread. Future jobs receive their own lease copies.

### Verification completed

`avatar_keyframe_asset_ownership` links actual llcharacter code when LL_TESTS is
enabled. **34** checks pass with both map and contiguous keys:

- Canonical pointer/control-block identity across cache hits and publication;
  distinct old/new assets on replacement, eviction/clear with active owners,
  missing-entry removal and final destruction once.
- Obsolete per-motion constraints are gone before old shared data is destroyed;
  same-asset adoption retains valid constraints. An isolated native negative
  variant omitting the cleanup fails that exact ordering check.
- Borrowed embedded data is not deleted by motion destruction and gets no lease;
  a mismatched raw view also gets no lease.
- Actual LLDataPacker decoding of a truncated packet after a rotation key publishes
  nothing. Complete and repeated packets publish shared owners while an old lease
  remains readable. These fixtures use a missing joint explicitly allowed by the
  existing decoder; they are not full visual/skeleton/legacy-format acceptance.
- A read-only lease samples actual curve data after all cache/motion owners are
  dropped, then performs the final asset-data destruction on a test thread.
  That thread calls no cache, character, UI or renderer API. It is not viewer
  scheduling integration or a stress/race-detector result.

The retained hand-pose regression passes **36** checks in both storage modes
after adapting to the shared cache API. A shared minimal character fixture keeps
these focused tests independent of avatar/renderer setup.

Isolated validation compiles fresh complete character/controller/keyframe sources
with these tests and links cached unchanged dependencies read only. It avoids
instantiating cached derived classes with stale base layouts. Complete fall-motion
field access, preview/reload and upload-validation callers pass native syntax
checks in map mode; keyframe ownership and posing callers pass with Tracy and
contiguous keys. The standalone pose check supplies the existing chrono header
dependency. MSVC 19.51.36244, x64 AVX2; no full viewer build/cache configuration.

### Acceptance and follow-up

Build this lifecycle checkpoint separately with x64 ReleaseFS_open_AVX2, Nightly,
NSIS and the contiguous option explicitly OFF for its primary comparison. ON
remains supported. Let CMake rebuild every dependent object/PCH after the base
class layout/API change. Retain installers/packages separately.

Check active animation/preview removal/reload, repeated asset changes, gestures,
hand poses, constraints/sitting, animesh, outfit/skeleton changes, teleport/logout
and exit. Assets retained by active motions/leases intentionally outlive cache
eviction; check memory releases when those owners end. Full linking, live visual
acceptance, crowd frame times, ARM/macOS and sanitizer coverage remain pending.

Plan 11 adds the owned sampling request/result contract. Default curve storage
remains maps, and real-scene evidence is still required before enabling major
threading changes or claiming FPS gains.
