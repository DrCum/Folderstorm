# Checkpoint 14i: remove active-blender allocation and search churn

Plan written before implementation. Start a new performance branch from the
published 14h purge checkpoint. Keep this structural change separately testable.

## Evidence and objective

The warmed three-joint controller makes three active-blender list-node
allocations per update, independent of shadow collection. `LLPoseBlender::addMotion`
also scans the accumulated list for every joint state. Crowded avatars multiply
this per-controller work; a component saving alone does not establish frame-time
gain in a real scene.

Replace repeated node allocation and linear membership searches with reusable
contiguous pointer storage and explicit membership owned by each pooled blender.
Preserve first-admission order: the old list pushes new entries at its front,
so append storage must be consumed in reverse order. Pool lookup and joint-state
priority/admission decisions must retain their current behavior.

## Work sequence

1. Attribute list allocations and repeated duplicate searches separately, with
   representative joint/motion overlap counts and opt-in native timing. Freeze
   the preceding real pose/blender implementation as an independent reference.
2. Audit `addMotion`, `clearBlenders`, `blendAndApply`, `blendAndCache`,
   `interpolate`, controller quantum transitions, motion purge and pool lifetime.
   Membership must persist while cached/interpolated entries remain active and
   reset exactly when the active collection is consumed or cleared. Do not
   overload joint-state clearing with collection membership changes.
3. Implement the storage/membership change without changing the number/order of
   joint-state admissions, priority/additive evaluation, pointer ownership,
   cached transforms, interpolation or reset semantics. Avoid per-avatar threads
   and per-frame hash/set allocation. Bound retained capacity by actual admitted
   pooled blenders; quantify object size, cold growth and warm retained memory.
4. Compare actual joint transforms, cached transforms, callback/application
   ordering and state reference lifetimes against the frozen implementation.
   Cover shared/disjoint joints, repeated admissions, priority saturation,
   additive/normal mixing, reset/clear/re-add, empty frames, multiple cache and
   interpolation calls, purge, normal/minimal/paused/quantized controller paths
   and destruction. Run the focused purge, phase/mask and shadow regressions.
5. Separate replacement-new allocation accounting from uninstrumented timing.
   Record full warmed controller costs as well as isolated collection costs.
   Native timings require the independent full builder to be idle. Keep raw
   samples and source/executable/dependency provenance; describe any regressions
   or memory tradeoffs before proposing a default change.
6. Publish its own PR with exact parent/build instructions and write the next
   plan based on evidence. Full viewer build and crowded-scene visual/frame-time
   acceptance remain required. Real shadow window evidence from 14g continues
   to gate a production worker trial.

## Results

Not implemented yet. An initial isolated native probe against the unchanged
actual `llpose.cpp` confirms one 24-byte list-node allocation per admitted joint
per warm frame on this MSVC target. Counts 3/32/128/256 and 1/8 overlapping
motions make respectively 96/1,024/4,096/8,192 calls over 32 frames; repeating
overlapping admissions adds searches without adding nodes. Every case returns
to its live allocation baseline. The scratch source/report are
`performance-validation/active-blender-cost-fixture14i.cpp` and
`active-blender-before-alloc14i.log` in the parent workspace; retain them when
starting this checkpoint's branch. Timing has not been run during the builder.

The source audit confirms that `blendAndCache` and `interpolate` retain active
entries and joint-state references. Only `blendAndApply` and `clearBlenders`
consume the collection. A membership flag must follow that distinction and
must be set only after pointer insertion succeeds. Preserve reverse traversal
for all four operations. Measure the aligned blender's size and collection
capacity rather than assuming the extra flag/storage is free.
