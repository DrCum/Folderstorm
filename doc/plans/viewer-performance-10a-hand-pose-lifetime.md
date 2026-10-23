# Checkpoint 10a: character-owned hand-pose data

Plan written before implementation, split from the checkpoint 10 lifetime audit.

## Problem

`applyKeyframes` publishes pointers to a cached motion list's hand pose and
maximum priority through the character animation-data map. LLHandMotion normally
consumes/removes both entries each update, but cache removal/replacement and
future worker leases can change when an old list is destroyed. These transient
character pointers must not depend on the asset's final owner or destruction
thread. Clearing them from a keyframe-motion destructor is unsafe: character
animation data is destroyed before its motion-controller member.

## Implementation and verification plan

1. Add typed hand-pose/priority values owned by LLCharacter and a main-thread
   publication method that copies those values and exposes the existing two
   animation-data names. Append fields to preserve existing member offsets.
2. Use that publication method from the existing winning-priority branches of
   LLKeyframeMotion::applyKeyframes. Preserve priority comparisons, equal-priority
   ordering and hand-motion consumption/removal. Character data records the
   values at application; subsequent asset edits are observed on next application.
3. Test actual LLCharacter and LLKeyframeMotion code with a minimal character
   and motion probe. Verify lower/equal/higher priority order, independent
   characters sharing an asset, edits followed by reapplication, entry removal,
   and values remaining valid after cache data/motion destruction. Do not run
   another update through an evicted raw asset until the ownership checkpoint.
4. Compile affected complete translation units and publish separately with hand
   poses/gestures/preview/crowd acceptance instructions. No workers or cache
   ownership changes in this prerequisite PR.

## Next checkpoint

Continue plan 10 with shared canonical asset ownership. Clear obsolete per-motion
constraints during asset adoption, before releasing the previous owner; setupPose
currently appends constraints and must not retain pointers into a released list.
Character-owned hand-pose values remove a separate raw alias from that boundary.

## Results

Implemented. LLCharacter owns typed hand-pose and priority fields and exposes
`setHandPoseAnimationData`. The existing winning branches copy their values into
those fields and publish the same animation-data names. Priority comparison and
equal-priority ordering remain in applyKeyframes. LLHandMotion still consumes
and removes the entries; the editing motion's static legacy producer is unchanged.
Fields are appended, and all dependent sources/PCH must rebuild normally.

`avatar_hand_pose_lifetime`, enabled by LL_TESTS, links the actual character and
keyframe code. It passes **36** checks in both map and contiguous configurations:
lower/equal/higher priority order, stable character-owned addresses, shared asset
requests on independent characters, edits followed by application, legacy raw
producer interoperability, consumption/removal, cache eviction and values staying
valid after motion/cache destruction. It deliberately does not update through an
evicted raw asset; that underlying ownership issue belongs to checkpoint 10b.

The same native check fails against the preceding applyKeyframes implementation
at the explicit borrowed-storage assertion. This proves it detects the original
dependency without relying on freed memory accidentally retaining old values.

Isolated native validation compiles the complete updated llcharacter.cpp and
llkeyframemotion.cpp with the test and links cached unchanged dependencies read
only. It does not run a viewer or instantiate cached viewer/derived-avatar classes
whose base layout differs. Complete hand-morph consumer, editing-motion producer
and pose caller syntax checks pass in normal mode; changed character/keyframe
units and the pose caller pass with Tracy and contiguous keys enabled. The pose
check supplies the existing standalone chrono dependency. MSVC 19.51.36244, x64
AVX2. No full viewer build or shared-cache configuration occurred here.

## Runtime handoff

Build this checkpoint separately with the usual x64 ReleaseFS_open_AVX2, Nightly
and NSIS configuration. Leave the contiguous option OFF for this lifecycle
checkpoint's primary comparison; ON is supported when testing the prior variant.
Compare with the preceding runtime checkpoint using identical settings. Exercise
hand poses, gestures, editing tools, competing/equal-priority animations,
preview edits/close/reload, repeated motion starts/stops, multiple avatars sharing
animations, teleport/logout and exit. Check actual morphs and pose transition
order. No visual, FPS or complete malformed-asset acceptance is claimed.

Plan 10b now specifies canonical asset leases and obsolete-constraint cleanup.
