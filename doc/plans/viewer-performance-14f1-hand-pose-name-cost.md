# Checkpoint 14f1: remove repeated hand-pose key-name allocations

Plan written before implementation. Discovered while measuring actual owner
preparation/application for 14f, before any live worker dispatch.

## Evidence and scope

After warm-up, 32 direct updates and 32 owned applications each make 64 C++
replacement-new calls / 2,048 requested bytes for 3, 32 and 128 joints. Preparation
makes no warm allocations and current validation adds no calls. Source shows
the repeated long `Hand Pose Priority` string temporaries in keyframe lookup and
character publication. Preserve the allocation report as the before control.

## Work sequence

1. Verify the exact lookup/publication routes with the focused actual-character
   allocation fixture. Preserve generic animation-data API signatures and the
   map as authority for overrides/removals; do not replace it with a cached raw
   value/iterator that can go stale when LLHandMotion clears entries.
2. Add a typed character priority lookup using a stable cached key name, and
   reuse stable names in existing hand-pose publication. Keep storage character-
   owned, priority ties/current winners unchanged, and generic set/remove/read
   behavior intact. Ordinary serial playback should benefit too.
3. Test absent/lower/equal/higher priorities, generic external overrides,
   removal/republication, actual hand-pose lifetime and frozen-update equivalence.
   Keep serialization and current-context behavior unchanged.
4. Repeat the actual direct/owned allocation probe in both curve modes. Expect
   zero repeated key-name allocations while entries exist; separately account
   for legitimate node/key allocation after removal and first publication.
   Confirm no retained iterator/raw-pointer lifetime dependency.
5. Repeat paired owner timing outside allocation instrumentation, report raw
   data and scope limits, and publish this small performance change separately.
   Full rendered hand-morph/priority and crowd acceptance remains pending.
6. Continue the already-written 14g shadow-prediction plan, updating it from the
   measured owner costs. Do not enable workers or change controller ordering.

## Results

Not implemented yet.
