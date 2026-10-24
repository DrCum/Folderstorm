# Checkpoint 14f: explicit ordinary-motion eligibility and current context

Plan written before implementation. Parent: per-job numeric controls, owned
sampling and the base-motion phase contract. No live dispatch is enabled.

## Objective

Replace caller-invented test identities with independently obtained owner-thread
identity for ordinary keyframe motions. Define which actual factories may opt in,
prove sampled key immutability, and reject stale work at the original application
point. Do not treat a base pointer or generation number as proof of eligibility,
object lifetime, current inputs or freedom from a concurrent writer.

## Work sequence

1. Audit all construction/registry paths, subclasses and virtual update/activation
   overrides. Define an explicit capability granted by the known ordinary
   LLKeyframeMotion factory; inherited/custom walk, stand, fall, preview, poser
   and borrowed-data paths default to ineligible unless separately proved.
   Keep the protected base phase test API distinct from production eligibility.
2. Define owner-thread unique instance and activation generations, including
   deprecation, reactivation, stop, removal/replacement and counter overflow.
   Establish which live methods maintain them before adding fields. Worker jobs
   retain owned payloads only; a token must never authorize dereferencing an
   expired live pointer. Dispatcher/lifetime lookup remains main-thread work.
3. Capture actual current skeleton/publication/edit context and numeric controls
   independently at preparation/application. Audit every normal live writer of
   array layout, curve keys/counts/interpolation and the metadata read by time
   resolution. Seal, replace or copy-on-write sampled storage if any writer can
   overlap a worker; version increments alone do not make that race safe.
4. Represent actual frame-clock context without lossy hashing. Include the real
   frame timer time as well as count, or an audited epoch covering every update
   site, including modal/re-entrant work that updates time without frame count.
   Retain bit-exact sample/duration/usage/current-owner checks at application.
5. Exercise actual factories and lifecycle transitions with the real controller,
   deterministic clock and frozen update oracle. Cover same-ID new instances,
   stop/reactivate, cache removal/replacement, skeleton/binding/usage changes,
   edits, modal time changes, destruction, unsupported contexts and direct
   fallback. Verify normal virtual subclass behavior is never rerouted.
6. Keep ordinary playback serial. Do not introduce a speculative global avatar
   traversal, queue or changed callback order in this identity checkpoint.
   Measure actual owner preparation and current-input validation costs: the phase
   bridge currently builds a temporary usage vector, whereas the complete-cost
   fixture reuses current descriptors. Establish bounded reuse or a nonallocating
   current view before any live dispatcher; do not infer bridge costs from that
   fixture or from the isolated numeric guard benchmark.
   Full viewer builds and rendered/crowd acceptance remain separate evidence.
7. Publish a separately testable PR with exact dependency/head/build instructions.
   Write the next plan for a main-thread shadow prediction experiment: compare
   proposed prepass queries with the real controller's eventual query and reject
   mismatches without applying extra results. Only source-proven context and a
   measured same-frame overlap window can justify a live bounded dispatch trial.

## Acceptance limits

No inferred subclass eligibility, stale/live pointer use, token copied from the
result as current authority, silent mutable-key sharing, changed motion ordering,
additional animation frame, or default worker enablement. Context proof and
numeric equivalence do not establish a crowd FPS gain.

## Results

Implemented; normal playback remains serial. This checkpoint adds explicit
owner preparation/application APIs without adding a production caller, queue,
avatar prepass or worker dispatch.

### Audited boundary and implementation

- The registry's default and explicitly registered ordinary paths call
  `LLKeyframeMotion::create`. Raw upload construction and walk/stand/fall/poser
  constructors do not. The capability is granted only inside that ordinary factory.
  Poser also has a base-copy constructor, so its inherited flag cannot be trusted:
  the private authority member's copy/assignment resets identity and eligibility,
  without changing existing base-copy pose behavior. IDs are issued only by the
  ordinary factory; zero means no permission.
- Preview uses the ordinary registry too (`LLFloaterBvhPreview`), so factory
  stamping alone cannot exclude it. An explicit character permission is required:
  generic characters deny it, and LLVOAvatar initially permits ordinary remote
  scene avatars only (self/UI/control avatars remain serial). This is only
  eligibility for a later trial, not scheduling. Controlled native fixtures
  explicitly opt in. LLUIAvatar sets the UI role used to exclude preview paths.
- Unique nonzero instance IDs and a lifecycle epoch are maintained at the actual
  base keyframe activation/deactivation/stop/adoption boundaries. Exhaustion
  disables new identity/context rather than wrapping. Identity includes actual stopped
  state/stop timestamp in current identity because the generic controller also
  uses the nonvirtual base `setStopped` setter for loading-state transitions.
- Binding snapshots stay on the owner only, lazily bounded by the existing joint
  limit, retaining LLJointState references to prevent state-pointer ABA while
  comparing. Refresh the lifecycle epoch when state/target bindings or character/
  skeleton context changes. Jobs never receive these live pointers/references.
- Shared asset revision covers the supported metadata editors, including edits
  through another motion sharing that asset. The sampled array/curve fields are
  built in the unpublished deserialize list and only destroyed after its last
  owner. The only non-test list users outside this file are fall and poser:
  fall reads duration, poser borrows its own dummy list or reads curves.
  `setLoopIn/Out` writes legacy anchor fields that the current getters do not
  read; priority/emote/hand/ease setters do not modify sampled keys/counts/modes.
  Serialization's former key writer was fixed in 14a. Versioning does not make
  any future live key writer safe; such a writer requires replacement/COW.
- Frame count and exact F64 frame-time bits are captured independently at both
  boundaries, with skeleton, appearance serial, asset revision, lifecycle and
  numeric controls. Count/time are not collapsed into a hash, and current
  authority is never read back from the result.
- Owned request/result data and explicit owner APIs are exposed, while keeping the
  low-level test phase methods protected. Ineligible/invalid current context
  falls back through virtual onUpdate so custom subclass behavior is preserved.
  As with the normal callback, the owner must already hold a valid live motion
  at its original update point; an identity never authorizes pointer access.
- The current usage prefix is validated in a fixed bounded stack view, sharing
  existing scalar/owner/result comparisons. The temporary application request
  vector is removed. Actual prepare/apply allocation and timing probes are
  separate from the earlier complete-cost fixture's reused descriptors.

### Validation

- Actual ordinary/raw/custom factories, copied/assigned base motions, real
  controller stop/reactivation, destroyed characters and same-ID replacements:
  1,203 checks pass in each of map and contiguous modes. The mutation matrix
  accepts three current-owner cases and rejects 25 stale/invalid cases while
  preserving exact direct poses and continuation. Cases include independent
  frame count/time changes, skeleton/appearance/binding/usage changes, character
  replacement, shared metadata edits, stopped state, numeric mode, consumed
  results, role permission, cache eviction and cache-entry replacement.
- Successful real binary deserialization publishes a new canonical asset and
  invalidates the old context; the old result falls back and a newly prepared
  result applies. This proves publication/context handling, not rendered active
  reload or pose-map transactionality. Those remain runtime acceptance items.
- Counter tests issue 1,024 unique IDs on four threads and check permanent
  exhaustion behavior. Factory copying never copies permission or live bindings.
  Jobs contain no live character, motion, joint-state or joint pointers.
- Existing suites pass in both modes: 5,956 phase checks / 1,474 frozen updates;
  109,806 owned checks / 24,193 exact poses; 1,734 batch checks; 829 actual numeric
  checks with 116 unguarded mismatches reproduced. Native runs use fresh
  character/controller/keyframe sources and fresh custom motion sources where
  required, MSVC 19.51.36244 x64 AVX2, optimized precise floating point and
  assertions enabled. Cached dependencies were read only; scratch executables
  and logs are in the sibling `performance-validation` directory (`*14f*`).
- Independent Tracy/contiguous syntax checks pass for the affected keyframe,
  test, avatar and poser translation units. PCH-free avatar/poser checks explicitly
  include `chrono` for an existing transitive header requirement. No shared
  viewer cache was configured/built here. Other-platform execution and
  sanitizers remain pending.

### Owner costs and remaining allocation work

The separate allocation executables count replacement C++ `new` calls, not all
possible allocator/OS activity. For 3, 32 and 128 joints in both storage modes,
cold preparation makes two calls (binding storage and request usage). Thirty-two
warm preparations make zero calls. Thirty-two direct updates and thirty-two
owned applications each make 64 calls / 2,048 requested bytes: current validation
adds none. The remaining two calls per update come from the long hand-pose
priority key-name temporaries in the lookup/publication path. Plan 14f1 removes
those separately, preserving generic animation-data overrides/removals.

Raw reports: [map allocations](results/current-motion-alloc-map.csv),
[contiguous allocations](results/current-motion-alloc-contiguous.csv), and
[map owner timings](results/current-motion-cost-map.csv).

Median nanoseconds per call on the Ryzen 5 3600, nine alternating direct/apply
samples of 1,000 calls per stage and size:

| Joints | Direct update | Owned application | Warm owner preparation |
| --- | ---: | ---: | ---: |
| 3 | 319.4 | 254.8 | 41.3 |
| 32 | 1,852.9 | 366.3 | 92.7 |
| 128 | 7,427.5 | 847.7 | 297.5 |

All 81 rows / 81,000 calls have valid accounting and exact paired final poses.
The generated motions have four keys per scale/rotation/position curve, no
constraints or emote, warmed assets, fixed time and a controlled character.
Application reuses an already computed result; worker math, dispatch, waits,
allocation instrumentation, the full avatar update and real overlap are outside
these timings. The figures are component costs, not a total speedup or FPS claim.

### Build and next step

Parent is PR 108 at `eea3c55cd0a93ab93dd0b84a90d1fba1ed85fcc1`, preserving the
frozen feature stack through PR 88. Build this checkpoint's exact head separately
with the accepted private x64 AVX2 OpenSim Nightly configuration, Tracy OFF and
contiguous keys OFF. The building agent owns full builds/retention and monitoring.
Its PR 108 build and focused checks passed; that does not validate this new head.

With LL_TESTS enabled, `avatar_current_motion_context` runs the context regression;
`avatar_current_motion_cost --benchmark` and
`avatar_current_motion_allocations --allocations` are separate opt-in probes.
Full rendered hand/animation/preview, arrival/departure, outfit, sit/stand,
animesh, teleport/logout and shutdown acceptance and matched crowd captures
remain pending. Initial scene eligibility is ordinary remote avatars only;
normal callbacks are unchanged. A future dispatcher must still establish valid
owner-thread lookup/lifetime at the original callback and bound its retained
owner binding storage as well as job memory.

Next: implement [14f1's measured hand-pose allocation fix](viewer-performance-14f1-hand-pose-name-cost.md)
as a separate PR, then [14g's shadow query prediction](viewer-performance-14g-shadow-query-prediction.md).
No useful real same-frame overlap window has been established yet.
