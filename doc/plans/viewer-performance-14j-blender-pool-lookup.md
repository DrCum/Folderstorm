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

Not implemented yet. The new branch is `codex/perf-blender-pool-lookup`, from
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
