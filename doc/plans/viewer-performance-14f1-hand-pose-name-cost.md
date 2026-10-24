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

Implemented. `LLCharacter` keeps stable function-local key names, publishes its
existing character-owned pose/priority through the existing map, and provides a
typed const priority lookup that performs a fresh map lookup each time.
`LLKeyframeMotion::publishHandPose` uses that lookup. Generic animation-data API
signatures, map/value ownership and priority comparison/order are unchanged.
No iterator or external producer value is cached. This benefits ordinary serial
playback as well as the experimental application path.

### Correctness and allocation results

- Expanded actual character/keyframe lifetime suite: 171 checks pass in each
  key-storage mode. Covers no/lower/equal/higher priorities, null/generic
  overrides, independent removal, repeated republication, character separation,
  cache/motion destruction and typed lookup agreement with the generic map.
  A real `LLHandMotion` consumes/clears and the next publication restores entries
  across eight cycles. Its relaxed-pose fixture needs no mesh; it does not prove
  rendered morph blending or full viewer behavior.
- Existing 5,956 phase checks / 1,474 frozen updates and 1,203 current-context
  checks pass in both modes. Fresh native character/controller/motion/keyframe
  sources are compiled, with fresh hand/custom motion sources where used.
  Independent Tracy/contiguous source checks pass for both changed production
  translation units and both changed test translation units.
- For 3/32/128 joints in each mode, 32 warmed direct updates and 32 owned
  applications now make zero replacement C++ `new` calls / requested bytes,
  versus 64 calls / 2,048 bytes for each path in the parent. Warm owner preparation
  stays at zero; cold preparation still makes two calls. The allocation probe
  asserts zero repeated calls, not merely equal direct/owned counts.
- Map removal remains authoritative. Separately, 32 publications after clearing
  both entries make 96 calls / 5,632 requested bytes for each path: two new map
  nodes and one owned long key string per publication on this MSVC build. These
  legitimate insertions are retained. Removal calls are outside that scope;
  generic by-value API calls can still create their own name temporaries.
  Stable names initialize once per process; fixtures warm them before counting.
  The probe counts replacement C++ `new`, not every possible allocator/OS call.

Raw allocation data: [map](results/hand-pose-name-alloc-map.csv) and
[contiguous](results/hand-pose-name-alloc-contiguous.csv). Parent control reports
remain in `results/current-motion-alloc-*.csv`.

### Paired component timings

Fresh control sources for `llcharacter.cpp` and `llkeyframemotion.cpp` were
extracted from parent `e08cf82050215fd0ab335ba7611c52ff3f03b528`; byte hashes are
verified against Git. Control/candidate use the same timing fixture and native
MSVC x64 AVX2 `/O2 /fp:precise` setup with assertions. Allocation instrumentation
is a separate binary. Shared build state was idle and a full native-process
inventory was empty before measurement. No shared cache writes/builds occurred.

Run order is before/after/after/before, each run nine alternating direct/apply
samples of 1,000 calls per stage and joint count. All 324 rows / 324,000 calls
have valid accounting and exact paired final poses. Combined medians in ns/call:

| Joints | Direct before | Direct after | Owned apply before | Owned apply after | Prepare before | Prepare after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 3 | 316.80 | 212.75 | 226.70 | 122.80 | 40.55 | 40.75 |
| 32 | 1,792.20 | 1,696.80 | 354.70 | 272.05 | 89.90 | 88.20 |
| 128 | 6,819.75 | 6,749.15 | 822.00 | 756.35 | 277.00 | 272.85 |

Raw timing runs: [before 1](results/hand-pose-name-cost-before-map-r1.csv),
[after 1](results/hand-pose-name-cost-after-map-r1.csv),
[after 2](results/hand-pose-name-cost-after-map-r2.csv),
[before 2](results/hand-pose-name-cost-before-map-r2.csv).
[Source/executable provenance](results/hand-pose-name-cost-provenance.json).

The generated four-key curves are warmed, use fixed time, have no constraints or
emote and run on controlled characters. Owned application reuses computed
results. Worker math, queues/waits, full avatars, generic removal and real crowd
workload are outside these timings. Noise and joint-count-dependent cache/math
costs prevent treating a component ratio as a whole-viewer gain. No FPS claim.

### Build, acceptance and next plan

Dependency is PR 109 at `e08cf82050215fd0ab335ba7611c52ff3f03b528`, including
the frozen feature stack through PR 88. Its automated review completed without
findings; its full build is owned by the independent building agent. This new
checkpoint needs its own retained exact-head private build: x64 AVX2 OpenSim
Nightly Velopack, Tracy OFF and contiguous keys OFF for the normal control.

With LL_TESTS, run `avatar_hand_pose_lifetime`, `avatar_keyframe_phase` and
`avatar_current_motion_context`. The separate
`avatar_current_motion_allocations --allocations` and
`avatar_current_motion_cost --benchmark` probes are opt-in. Check visible
competing/equal-priority hand animations, hand cleanup/republication, preview/
poser, outfit/character changes and ordinary crowd playback. Full rendered,
lifecycle, other-platform and crowd performance acceptance remains pending.

Next: [14g's shadow query prediction](viewer-performance-14g-shadow-query-prediction.md),
whose audit now records the concrete controller branches before implementation.
Normal playback remains serial, with no scheduling or callback-order change.
