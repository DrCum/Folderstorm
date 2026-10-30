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

## Baseline results

Recorded before implementation from PR115 head
`2df5f5bdaf402c3fd3b64be860eaa4fdf55b411c`. Actual relaxed hand callbacks make
32 allocation calls / 1,024 requested bytes over 32 updates in each of five
cases: absent, owned, generic override, pose-only and priority-only entries.
These scopes exclude producer costs and end with zero counted live bytes.
Cleanup followed by owned publication makes 128 calls / 6,656 bytes over 32
cycles; its final 176 counted bytes are map-owned and return to zero on cleanup.
Thus 96 legitimate following-publication allocations must remain separately
accounted for rather than being mistaken for consumer cleanup waste.

The scratch-only allocation executable compiles pose, character, controller,
motion, keyframe, shadow and hand sources fresh and links immutable internal
libraries. Raw rows, probe and exact identities are in
`results/hand-consumer-*14n*`. This first fixture uses relaxed poses/no mesh; it
does not cover changing morphs or provide timings. It predates the production
cleanup change and the visual-param fixture described below.

## Implementation and final evidence

Implemented at `7fe77b5aa945e4ad8a21ba6f7308e20253dba3fe`.
`LLCharacter::clearHandPoseAnimationData` erases the two map entries in their
original order with the existing stable key names. The real hand callback calls
it at exactly the old cleanup boundary. Generic APIs, value ownership, priority
lookup, morph math, callback ordering, class fields and virtual APIs are
unchanged. No cache, worker, persistent setting or frame delay is added.

The frozen pre-change callback is retained as a test-only reference. Before
implementation, both actual parent modes matched that reference across 448
frames and 55 callback republications. The final implementation reproduces
those same frame streams in each key-storage mode. Each run passes 17,103
checks covering real visual-param storage/application, all hand poses,
interrupted blends, repeated/backward times, invalid/null requests, partial
entries, unrelated data and callback republishing before/after cleanup. These
use dummy morph parameters, not rendered meshes.

The actual character/keyframe lifetime suite now passes 216 checks per mode,
including generic/null/partial cleanup, idempotence, independent character maps,
priority ties, publication addresses and motion/cache destruction. Separate
allocation executables pass 1,019 warm allocation/pressure checks and eight cold
failure/retry checks per mode. Normal allocation TU and standalone Tracy/
contiguous production/test TU syntax checks pass.

| 32 actual callbacks | Before calls / bytes | After calls / bytes |
| --- | ---: | ---: |
| Absent entries | 32 / 1,024 | 0 / 0 |
| Owned entries, producer excluded | 32 / 1,024 | 0 / 0 |
| Generic override, producer excluded | 32 / 1,024 | 0 / 0 |
| Pose-only or priority-only, producer excluded | 32 / 1,024 | 0 / 0 |
| Cleanup plus following owned publication | 128 / 6,656 | 96 / 5,632 |

The following publication still creates its two map nodes and owned long key.
Its final 176 counted bytes return to zero on cleanup. Changing-morph callbacks
also make zero counted allocations after warm-up in the focused 64-update probe.
Ordinary-new accounting excludes custom aligned blocks and RSS.

The existing stable long key can initialize first in the consumer. Its injected
allocation failure preserves the original pose-first cleanup boundary, and
retry succeeds. On this MSVC target, successful retry retains 32 bytes once for
that existing static name until process exit. Warm cleanup succeeds with the
allocation failure counter set to zero. This is shared name initialization,
not retained per-character map entries.

Final source, reference, frame, probe, executable and dependency identities are
in `results/hand-consumer-provenance14n.json`; raw before/after rows and probes
are retained alongside it. The isolated correctness runs overlap the private
PR115 build and link immutable internal libraries with read-only external
packages. The parent idle guard reported active before an already-queued
validation command ran; that is recorded in the parent provenance. No timing
or idle-desktop evidence is claimed. Shared source/cache and monitor were not
changed. Native sessions 96164, 95277 and 31851 are terminal.

## Separate build and remaining acceptance

Build this exact PR head separately from PR115, including its complete preceding
stack. Use the accepted private normal x64 AVX2 OpenSim Nightly Velopack setup,
Tracy OFF and contiguous keys OFF; preserve completed archives. No extra full
profiling/storage variants are requested. Full viewer integration, rendered
hand morphs, competing animations, appearance changes, travel/lifecycle and
crowded FPS acceptance remain pending. Native counts do not establish FPS gain.

With LL_TESTS, `avatar_hand_consumer` and `avatar_hand_pose_lifetime` run their
correctness checks. Run `avatar_hand_consumer_allocations --allocations` and
`avatar_hand_consumer_allocations --cold` as separate fresh processes; the latter
requires uninitialized production key names. Allocation injection skips with
Tracy enabled. The allocation executable provides no timings.

Plan 14o returns to the whole viewer's main-frame phase/worker ownership audit
to choose the next kernel from evidence. Real crowd/query-window and visual
acceptance remain open, and no production worker dispatch is enabled.
