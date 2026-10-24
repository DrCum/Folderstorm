# Checkpoint 14n: hand-animation cleanup allocation

Plan written after implementing 14m, before changing the hand consumer. Use a
new performance branch from the completed 14m evidence head and a separate PR.

## Hypothesis and boundary

14f1 eliminated long key temporaries in keyframe publication/priority lookup.
Source inspection still finds `LLHandMotion::onUpdate` calling by-value
`removeAnimationData("Hand Pose Priority")` every callback, including when the
entry is absent. The 18-character key can allocate independently of the useful
map-node insertion when a later motion publishes again. Existing 14f1 reports
explicitly measured removal outside their allocation scope.

Measure the actual consumer. Prefer a narrow typed character cleanup using the
already stable key names over changing every generic API signature or caching
raw map values/iterators. Preserve removal of both hand entries in its current
order, fresh generic-map authority, character-owned values, morph updates,
priority ties and motion ordering. No workers, new persistent cache or quality
change belongs in this step.

## Work sequence

1. Freeze the parent source identity and record actual `LLHandMotion::onUpdate`
   allocations after warm-up. Cover absent entries, present owned entries and
   generic overrides. Separate consumer-only cleanup from cleanup-plus-next-
   publication; preserve legitimate map-node/key creation costs in the latter.
2. If a repeated key temporary is reproduced, add a typed character cleanup
   helper reusing its existing stable key names. Invoke it at the same point in
   the real hand callback. Keep generic API signatures/storage unchanged and
   make no layout, raw-value caching or lifetime changes.
3. Extend the existing actual hand lifetime fixture with generic overrides,
   independent character maps, absent/partial entries and cleanup/republication.
   Compare actual callback returns and morph/visual-param call order to the
   frozen parent for relaxed, changing and invalid requested poses. An empty
   relaxed fixture alone does not establish morph behavior.
4. Run focused native correctness and separate allocation/failure probes in
   both curve modes with fresh character/hand/core sources. Confirm cleanup is
   nonallocating after warm-up and the next publication still owns its keys.
   Inspect Tracy/normal caller compilation. Do not use replacement allocators
   in timings or shared mutable library/cache sources in native validation.
5. Publish only a demonstrated, reviewable improvement as its own exact-head PR.
   Runtime hand-morph/rendered/crowd acceptance remains deferred. Timing controls
   have varied; report counts first and retain any inconclusive timing evidence.
   Write the following plan after implementation or a documented rejection.

## Deferred acceptance

The user has not tested PR 114 yet and is working. Keep Q7 and visible hand/
outfit/priority/lifecycle checks in the existing question queue; no new answer
is required for this independent work. Do not operate a logged-in viewer or the
separate builder's source, monitor, cache or archives.
