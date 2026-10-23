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

Not implemented yet. The source audit and prerequisite are documented in plans
10 and 10a. Keep ordinary builds on map storage by default.
