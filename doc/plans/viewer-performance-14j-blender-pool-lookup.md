# Checkpoint 14j: reuse blender-pool lookups and secure cold insertion

Plan written before implementation. Branch from the published 14i collection
checkpoint and keep this change separately testable.

## Evidence and objective

After removing active-list scans and nodes, `LLPoseBlender::addMotion` still
searches `mJointStateBlenderPool` twice for each already-pooled joint: `find`,
then `operator[]`. Reuse the first iterator on the warm path. Preserve the
existing pointer-key map, joint-state admission order and pool/collection
lifetimes.

The cold path also constructs a raw blender before map insertion. First test
allocation failure at each construction/publication point; establish whether
an insertion failure loses ownership. Use a temporary owner until successful
publication if needed. Do not change the numerical blender or active storage.

## Work sequence

1. Freeze 14i source/executable identity and its current collection/controller
   fixtures. Attribute remaining pool lookup work with the existing shared and
   disjoint joint cases; keep allocation instrumentation separate from timing.
2. Reuse a successful `find` result. For a missing joint, construct under local
   RAII ownership and publish safely; handle an existing entry without leaking
   or substituting a second canonical blender. Avoid changing container type,
   eager allocation, pointer lifetime or default animation behavior.
3. Exercise cold constructor/node insertion failures and subsequent reuse.
   Account for custom aligned allocation separately from ordinary new. Confirm
   live-state references and owned pool objects are conserved after failure and
   destruction, and that retry cannot encounter a published null pool entry.
4. Run the frozen outer-collection and controller trace comparisons, warm/cold
   allocation reports and focused phase/shadow/purge/mask regressions. Preserve
   cache/interpolation and priority/additive behavior exactly.
5. Take paired native component/full-controller measurements only while the
   independent builder is idle. Record complete provenance and cold-memory
   tradeoffs; do not infer scene FPS from these generated components.
6. Publish a separate PR, then choose the next step from measured remaining
   costs and available real shadow/crowd evidence. The 14g runtime window and
   quality gates still apply before production worker dispatch.

## Results

Implemented on `codex/perf-blender-pool-lookup`, from
published PR 113 at `c7ce8ec6d7769c13a1decba496260feb5dd8bdd7`.

The real cold-failure probe reproduces a pre-existing ownership defect in both
that head and frozen PR 112. Failing the pool-node allocation after two
successful joint-cache constructor allocations leaves 112 ordinary-new bytes
retained after container destruction, with zero published pool entries. Joint
state references are restored, demonstrating why reference checks alone miss
this lost owner. The custom aligned blender allocation is outside that byte
counter. Constructor and active-collection failures clean up normally.

The probe source is `indra/llcharacter/tests/blender_pool_failure_test.cpp`;
eight-row reports and source/executable identity are in
`results/blender-pool-failure-*`. Baseline runs explicitly disable the future
zero-retention assertion with `FS_EXPECT_SAFE_BLENDER_POOL=0`. The default
assertion must pass after the fix. Preserve these baselines, add retry/reuse
coverage, and keep this ownership correction with the single-lookup change.

The warm path reuses its first successful iterator. Cold creation/publication
uses a temporary `unique_ptr` in a file-local no-inline helper. It releases the
candidate only when insertion succeeds and otherwise returns the existing
canonical entry. The pool type, object layout, every state admission and active
collection behavior remain unchanged.

Native optimized MSVC x64 AVX2 validation, assertions enabled:

- 79 checks in each curve mode exercise four injected constructor/node/active
  allocation failures, retry, subsequent reuse, pose application and teardown.
  The failed node-insertion retention falls from 112 counted ordinary-new bytes
  to zero. Original baseline rows and a same-source parent control are retained.
- Controlled single-thread allocation reentry publishes the same key through a
  different pose during construction. The parent overwrites the canonical
  owner, creates two active entries, retains 112 counted bytes and changes the
  resulting pose. The candidate preserves one canonical/active entry, the
  expected weighted pose, state reference counts and zero counted retention.
- The reentry test first verifies that construction itself performs ordinary
  allocations, so the hook fires before map insertion. Libraries without that
  behavior report an explicit skip for this subcase. Global failure injection
  is skipped with Tracy enabled; use Tracy OFF for these probes. Native
  other-platform execution remains unproved.
- The 544-byte aligned blender uses its existing class allocation/deallocation
  pair. Temporary ownership invokes that class delete on failed publication.
  Its aligned block and allocator/RSS overhead are excluded from ordinary-new
  counters; the counted 112-byte retention is its constructor's two map
  allocations. The source ownership guarantee is not a claim that the probe
  counts every underlying heap byte.
- Each curve mode passes 26,748 frozen-outer checks / 46 cases and reproduces
  all 216 PR 113 controller frames word-for-word, with 4,054 controller checks.
  Final source regressions pass: 5,956 phase checks / 1,474 frozen updates,
  5,063 shadow checks, 5,838 purge checks, 241 mask checks and 26 separate
  diagnostic allocation/failure checks. Tracy/contiguous source checks pass.
- Normal cold/warm/live/peak/teardown accounting matches PR 113's eight memory
  rows. Warm collection and full three-joint controller updates still allocate
  zero ordinary-new bytes; object sizes and retained pointer capacity are
  unchanged. All native controller probes compile fresh pose/core sources and
  use immutable internal libraries plus read-only external dependencies.

The initial inline-cleanup prototype regressed the smallest full-controller
fixture. Its reports are preserved in
`performance-validation/blender-pool-before-cold-helper14j`. Separating cold
cleanup from the hot loop removed that observed full-controller regression.
Final paired parent/current/current/parent measurements ran with state/worker
idle and the full native/package process inventory checked around every run.
There are 504 rows / 504,000 timed invocations, 18 samples per table entry.

Medians on the Ryzen 5 3600, nanoseconds per invocation:

| Component | Joints | Motions | Parent | Candidate |
| --- | ---: | ---: | ---: | ---: |
| Admit/clear collection | 3 | 1 | 65.05 | 69.90 |
| Admit/clear collection | 3 | 8 | 415.55 | 397.90 |
| Admit/clear collection | 32 | 1 | 1,003.95 | 697.75 |
| Admit/clear collection | 32 | 8 | 7,181.15 | 4,728.95 |
| Admit/clear collection | 128 | 1 | 4,462.50 | 3,069.15 |
| Admit/clear collection | 128 | 8 | 54,136.15 | 38,256.50 |
| Admit/clear collection | 256 | 1 | 11,022.15 | 7,055.80 |
| Admit/clear collection | 256 | 8 | 185,268.30 | 121,560.60 |
| Full direct controller | 3 | 1 | 553.90 | 540.15 |
| Full direct controller | 32 | 1 | 3,984.40 | 3,773.00 |
| Full direct controller | 128 | 1 | 16,248.55 | 14,883.15 |

The smallest isolated collection case is about 5 ns slower; it is retained in
the report. These fixed-time, warmed generated components establish neither
whole-viewer FPS nor a real worker window. Complete controller files also retain
enabled-shadow rows; the table uses direct rows. Real scene quality/frame-time,
outfit/skeleton/cache churn and other-platform acceptance remain pending.

The next plan is `viewer-performance-14k-pool-index.md`, measuring index locality
and cold/memory costs before considering another storage change.
