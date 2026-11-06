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

Implemented on `codex/perf-active-blenders`, from PR 112 at
`b8c82e686def15937fdfad1620845d557e995a89`. The active collection is a reused
pointer vector, appended once per pooled blender and consumed in reverse order.
Private membership persists through cache/interpolation and resets after apply
or clear. It is published only after insertion succeeds. Pool lookup, numerical
joint blending, priority/admission and joint ownership are unchanged.

An initial isolated native probe against the unchanged
actual `llpose.cpp` confirms one 24-byte list-node allocation per admitted joint
per warm frame on this MSVC target. Counts 3/32/128/256 and 1/8 overlapping
motions make respectively 96/1,024/4,096/8,192 calls over 32 frames; repeating
overlapping admissions adds searches without adding nodes. Every case returns
to its live allocation baseline. The source
`indra/llcharacter/tests/active_blender_cost_benchmark.cpp` and report
`results/active-blender-before-alloc-map.csv` now retain the baseline in this
checkpoint's branch, with `active-blender-baseline-provenance.json`. A fresh
binary using the copied source reproduces all eight rows. The uninstrumented
parent executable is prepared. Final cost fixtures assign valid joint numbers
before motion admission, keeping warning/logging initialization out of cold
allocation accounting. Preliminary executables/reports remain preserved.

The source audit confirms that `blendAndCache` and `interpolate` retain active
entries and joint-state references. Only `blendAndApply` and `clearBlenders`
consume the collection. A membership flag must follow that distinction and
must be set only after pointer insertion succeeds. Preserve reverse traversal
for all four operations. Measure the aligned blender's size and collection
capacity rather than assuming the extra flag/storage is free.

Native optimized MSVC x64 AVX2 validation, with assertions enabled:

- 26,748 checks / 46 cases pass in each curve-storage mode against the frozen
  outer collection. They cover shared/disjoint states, zero weights/usages,
  normal/additive priorities, slot saturation and duplicate admission, four
  cache/apply/clear/re-add cycles, empty frames, local/world/cache transforms,
  state reference counts and destruction. Aliased targets and cache references
  make consumer order observable; an intentionally forward cache traversal is
  rejected at case 44. Only the outer collection is frozen; the numerical
  joint-blender source remains byte-identical to the parent.
- Each candidate storage mode reproduces all 216 frozen map-controller frames
  word-for-word. The fixture checks 4,054 invariants across ordinary, paused,
  forced, hidden/minimal, same/new quantum, stop/restart, deactivation, explicit
  clear and over-cap purge paths, with three joint counts and six motions.
  Raw traces remain in the parent workspace's `performance-validation` folder.
- 18 separate insertion-failure checks pass. Warm pool admission is interrupted
  at active collection allocation, then retried and compared with the frozen
  list behavior. Failed insertion cannot leave membership published.
- Focused map regressions pass: 5,956 phase checks / 1,474 frozen updates,
  5,063 shadow checks, 5,838 purge checks / 45 cases, and 241 mask checks / 15
  poisoned cases. All affected sources plus the avatar caller pass independent
  Tracy/contiguous source checks. Every native controller wrapper now compiles
  fresh `llpose.cpp` as well as fresh controller/character/keyframe sources;
  old-layout cached pose implementations are not used with new-layout headers.
- Eight cold/warm collection cases return ordinary-new live accounting to
  baseline on destruction. Warm 32-frame collection calls fall from
  96/1,024/4,096/8,192 to zero for 3/32/128/256 joints, with either 1 or 8 shared
  motions. The warmed three-joint full controller likewise drops from three
  allocations / 72 requested bytes per update to zero, including diagnostics
  disabled. Enabled diagnostics retain three preparation allocations and no
  live diagnostic bytes; 26 failure/cleanup checks still pass.

Measured Windows object sizes: the aligned joint blender remains 544 bytes;
the pose blender grows from 72 to 80 and its containing controller from 680 to
688. Warm pointer capacity is retained rather than freed each frame:

| Admitted joints | Retained pointer capacity, bytes |
| ---: | ---: |
| 3 | 24 |
| 32 | 336 |
| 128 | 1,128 |
| 256 | 2,528 |

The pool already retains the corresponding joint-blender objects. The memory
reports include ordinary-new cold/live/peak/requested bytes and complete
teardown, excluding the custom aligned object allocator and allocator/RSS
overhead. At 256 joints / one motion, candidate cold requested bytes grow from
123,312 to 124,760, while the ordinary-new peak falls from 118,224 to 114,584.
These are generated fixture values, not a viewer memory budget.

Paired uninstrumented measurements ran after the independent #112 builder
released ownership, checking state/worker idle and the full native/package
process inventory before and after every run. Each component ran in
parent/current/current/parent order, with 18 samples per table entry, 504 raw
rows / 504,000 timed invocations. Parent and candidate both compile fresh pose
and core sources with matching flags. The full parent is exported from exact
PR 112; cached old-layout implementations are not substituted into the final
control. An initial series with a cached parent pose object was excluded and
preserved byte-for-byte in `performance-validation/active-blender-before-fresh-control14i`.

Medians on the Ryzen 5 3600, nanoseconds per invocation:

| Component | Joints | Motions | Parent | Candidate |
| --- | ---: | ---: | ---: | ---: |
| Admit/clear collection | 3 | 1 | 172.60 | 72.45 |
| Admit/clear collection | 3 | 8 | 565.30 | 417.90 |
| Admit/clear collection | 32 | 1 | 2,690.70 | 976.80 |
| Admit/clear collection | 32 | 8 | 10,642.25 | 7,061.35 |
| Admit/clear collection | 128 | 1 | 19,851.25 | 4,435.65 |
| Admit/clear collection | 128 | 8 | 124,467.50 | 55,477.85 |
| Admit/clear collection | 256 | 1 | 60,271.90 | 11,305.95 |
| Admit/clear collection | 256 | 8 | 430,856.55 | 182,515.40 |
| Full direct controller | 3 | 1 | 682.35 | 544.75 |
| Full direct controller | 32 | 1 | 5,356.10 | 3,894.00 |
| Full direct controller | 128 | 1 | 31,498.40 | 16,086.35 |

Full-controller reports also preserve enabled-shadow rows; the table uses only
direct rows. These are generated fixed-time, warmed four-key curves without
real avatar preparation/rendering, constraints, useful worker overlap or a
crowded scene. They establish lower component costs in these fixtures, not an
FPS improvement. Raw samples and complete identity are recorded in
`results/active-blender-provenance.json`. The implementation commit is
`85255dd8172bc34462fc2d6d7b744531cab58a06`; its source and canonical report hashes
are checked against the committed files. No shared source/cache was configured,
built or written by this chat.
Whole-viewer frame times, visible quality, outfit/skeleton/cache churn and other-platform
acceptance remain pending. The next plan is
`viewer-performance-14j-blender-pool-lookup.md`.
