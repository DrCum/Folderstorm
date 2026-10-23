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

Not implemented yet. Full viewer builds and live hand-morph acceptance remain
the local building/testing agent's handoff.
