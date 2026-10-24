# Checkpoint 14l: measure bounded blender lookup hints

Plan written before implementation. Start from the committed 14k experiment,
which rejects sorted-array replacement and preserves PR 114's production tree.

## Objective

Measure whether a small read-through pointer cache avoids repeated warm tree
searches without changing the pool's ownership, canonical publication or
ascending-key destruction. The pool only inserts until destruction, so entries
remain stable. A cache is a hypothesis, not an assumed improvement. Extra per-
controller memory and misses across many avatars can outweigh a faster hit.

## Work sequence

1. Reuse the real-pointer tree fixture and matched flags. Compare no cache with
   1/16/64/256-entry hints, keeping misses authoritative in the tree. Include
   overlapping and disjoint motions, cyclic/full/shuffled queries, absent keys,
   deliberately colliding addresses, and 0/1/3/32/128/256/1024 pool entries.
   Record actual hit/miss counts, cache bytes and complete lookup costs. Do not
   select a size solely from the favorable three-key workload.
2. Require canonical values, misses and destruction behavior identical to the
   tree. Publish hints only for successful entries. Cold insertion failure,
   construction reentry and retry must leave the authoritative pool valid and
   hints free of unpublished/destroyed candidates. No global cache or live scene
   pointer escapes its owning pose blender.
3. Integrate only a candidate with useful whole-path cost/memory tradeoffs.
   Compare actual collection and controller costs with exact PR 114, compile
   pose/core fresh, preserve reverse admission and every numerical operation,
   and reproduce the frozen frames in both curve-storage modes. Warm updates
   must remain allocation free. Bound object growth explicitly on the six-core
   target, and keep cold/miss regressions in the report.
4. Publish a separate PR only for an implemented, justified viewer change.
   Retain negative evidence if it loses. If no candidate improves the complete
   path, stop pool-container work and choose a different measured preparation
   cost; do not add cache complexity to manufacture progress.

Viewer crowd/frame-time/quality acceptance and the real shadow window remain
open. This plan enables no workers or account operations and does not control
the independent private builder or its cache.

## Results

Lookup-only experiment implemented on `codex/perf-blender-pool-hints`, from the
14k evidence commit `54e250406c88fffa659dbb30fe7a8f71828ec38b`. Conditional
real-controller prototypes are compiled and partially measured, not accepted.

The independent actual-pointer fixture is
`indra/llcharacter/tests/blender_pool_hint_experiment.cpp`. It leaves the prior
experiment source frozen. Hints have 1/16/64/256 slots, mapping pointer bits
above 16-byte alignment to a slot. A hit checks the complete pointer; only a
successful authoritative tree search publishes a cached value. No negative
entry, ownership change or tree erase is introduced. Separate counting template
instantiations exclude counter updates from timing. Constant-slot hashing models
the collision limit using valid real pointers. The shuffled query case is a
fixed-seed permutation/repetition sequence, not a random allocator sample.

442,015 canonical/miss/collision checks pass. The coverage CSV records three
passes including the initial fill, not a measured live-viewer hit rate. Two
complete idle runs retain 3,360 timing rows, six samples per candidate/case/run,
with result checksums matched against each candidate's adjacent tree control.
Each timed call warms one batch first, then consumes 64 no-inline 1,024-key
batches. Objects/tree construction and cache initialization are outside the
lookup timer; complete-path measurements must include their costs separately.

Selected medians, ns/key (tree controls are independently paired per size):

| Entries/workload | Slots | Tree | Hint |
| --- | ---: | ---: | ---: |
| 3 / cyclic | 16 | 1.94 | 0.84 |
| 32 / cyclic | 16 | 6.12 | 6.20 |
| 32 / cyclic | 64 | 6.18 | 2.06 |
| 128 / cyclic | 64 | 8.70 | 7.75 |
| 256 / cyclic | 64 | 10.20 | 11.75 |
| 128 / cyclic | 256 | 8.73 | 3.27 |
| 256 / cyclic | 256 | 10.10 | 6.17 |
| 128 / shared/mixed | 256 | 6.37 | 2.83 |
| 128 / forced collision | 256 | 8.57 | 9.46 |

The 256-slot candidate needs 4,096 cache bytes per pose blender. That is a
substantial addition to an 80-byte pose blender and 688-byte controller before
any pool storage. Small caches lose across larger cyclic pools, while collision
fallback adds work at every size. No size is accepted from these results alone.
The full reports retain absent/empty/single-key and all larger cases as well.

Source inspection finds no production pool erasure or replacement before
destruction. The conditional prototype in `llpose.h/.cpp` uses
`FS_TEST_BLENDER_POOL_HINT_SLOTS`, default **0**, with no CMake/viewer option.
Only fresh native wrappers may enable it while evaluating complete costs.
Never mix a cached old-layout controller implementation with an enabled header.
Successful canonical publication fills a hint; exceptions leave it untouched.
The existing tree still owns entries and destroys them in ascending pointer order.

The independent builder retained PR 114 at exact `514dc0acd4` as version
`7.2.5.82260`, then released ownership. The previously observed build/package
processes were gone and state/worker idle before native compilation resumed.
That build proves PR 114, not these hint prototypes.

Both 64- and 256-slot actual map-mode controllers pass 4,054 checks and reproduce
all 216 frozen PR 113/114 frames. Each also passes 79 constructor/publication/
allocation-failure/retry/reentry checks, preserving canonical ownership and zero
counted retention. Fresh wrappers include pose/core/controller sources before
linking immutable internal libraries. Measured sizes are:

| Object | PR 114 | 64 slots | 256 slots |
| --- | ---: | ---: | ---: |
| Joint-state blender | 544 | 544 | 544 |
| Pose blender | 80 | 1,104 | 4,176 |
| Motion controller | 688 | 1,712 | 4,784 |

The first full-path series retains 756 rows / 756,000 invocations with actual
native-build idleness checked around every run. Per component, order is parent,
64, 256, 256, 64, parent. Parent executables are byte-verified against PR 114's
fresh-source provenance. All raw samples, including wide ranges, remain retained.
Direct one-motion controller medians, ns/update:

| Joints | Parent | 64 slots | 256 slots |
| --- | ---: | ---: | ---: |
| 3 | 561.80 | 546.70 | 667.00 |
| 32 | 3,670.00 | 3,683.00 | 4,737.60 |
| 128 | 14,601.05 | 16,189.20 | 14,473.90 |

These results do not justify adopting either cache. The collection-only fixture
with eight overlapping 128-joint motions improves from 39,901 to 17,196 ns with
256 slots, but the full-controller fixture has only one motion. Before accepting
or rejecting on that narrower fixture, add matched actual-controller cases for
multiple partially weighted overlapping motions, disjoint joint groups and
saturated normal playback. Validate actual poses/callback coverage outside the
timer. Retain cold initialization and memory costs as well. Do not replace or
selectively discard this first series or infer crowd FPS from it.

Contiguous-mode/full-outer/other regressions are still pending for any candidate
selected from complete costs. No PR or default enablement until the complete-
path and quality/stability evidence justifies a change. The conditional
prototype defaults to zero, remains experimental and may be removed entirely.

### Multi-motion fixture and measurement controls

`blender_pool_multimotion_benchmark.cpp` adds 21 generated cases over 3/32/128
joints and 1/4/8 motions (skipping more motions than joints). Inputs and motion
IDs are deterministic. Partial overlap holds a fixed early ease-in phase;
disjoint assets use separate joint groups at full weight; saturated assets fully
overlap at full weight. These are controlled workloads, not a distribution of
real crowd animations. Assets have four keys per curve, no constraints/rendering.

Outside timing, real ordinary factory/controller callbacks are checked with the
existing bounded shadow observer. All cases pass 38,971 checks / 252 changing-
time frames for the exact PR114 parent and both cache sizes. Every candidate
frame matches the frozen parent bit-for-bit, including all motion poses/weights
and local/world joint transforms. At 128 joints / eight motions, partial and
disjoint cases make eight actual callbacks, while saturated overlap makes one
and leaves seven predictions unused. No custom motion wrapper changes the
timed callback path. All measured updates run with the observer disabled.

The first full sweep retains 2,268 wall-time rows in `pool-multi-{steady,cold}-*`.
It is exploratory and **excluded from adoption evidence**: the unchanged
three-joint disjoint parent's per-run medians drifted from 569.4 to 884.5 ns,
and the partial parent's from 497.8 to 754.9 ns. Blending those separated runs
into one median would manufacture apparent gains. The original source copy,
executables and reports remain preserved; do not replace them with a new run.

The revised fixture records Windows `QueryThreadCycleTime` counts alongside
wall nanoseconds and accepts a single selected case to interleave short matched
runs. Raw thread cycles include user/kernel work and are never converted into
elapsed time; the API's timer behavior can depend on CPU implementation/frequency.
See the [Microsoft API contract](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime).
Both metrics and per-run parent drift must be examined before a decision; adding
a counter alone does not establish a stable causal comparison. On other systems
the cycle columns explicitly report unavailable. Native execution there remains
unproved.

Correctness/trace use no arguments / `--trace`. Timing uses `--benchmark` or
`--cold-benchmark`, optionally followed by joints, motions and workload index
(0 partial, 1 disjoint, 2 saturated). The cold phase creates a character and its
motions, performs eight initial updates and destroys it, with assets preloaded;
it is not an isolated measurement of cache zeroing. Optional LL_TESTS target
`avatar_blender_pool_multimotion` is excluded from the default build. Native
candidate layouts still require fresh pose/core wrappers.

The revised, case-interleaved series retains 2,268 rows with both raw thread
cycles and wall time. Steady parent per-run cycle medians differ by up to about
2.9%; cold parent differences still reach about 27.9%, so cold results remain
inconclusive. Keep per-run/min/max values; combined medians alone hide drift.
Selected steady medians, ns/update, with cycle changes independently agreeing:

| Joints / motions / workload | Parent | 64 slots | 256 slots |
| --- | ---: | ---: | ---: |
| 3 / 1 / saturated | 541.15 | 546.95 | 540.80 |
| 32 / 1 / disjoint | 3,683.05 | 3,619.85 | 3,463.60 |
| 32 / 8 / partial | 23,949.10 | 22,644.45 | 22,601.10 |
| 128 / 1 / saturated | 14,608.00 | 14,260.65 | 14,084.10 |
| 128 / 8 / disjoint | 16,950.40 | 16,756.95 | 16,440.25 |
| 128 / 8 / partial | 120,328.50 | 125,269.35 | 109,331.20 |

The large partial case is a fixed transition phase, not a claim about warmed
crowd animation mix. The 256-slot trial saves ~9.1% there, while the 64-slot
trial costs ~4.1% more. Common one-motion/disjoint improvements are smaller;
the smallest saturated case is effectively unchanged. This evidence warrants
the next density check, not default enablement or a viewer FPS claim.

Before accepting the 4 KiB cache, extend this fixture to serial round-robin
updates of 1/16/64 independently owned controllers, sharing parsed assets as the
real cache does. Include 32/128 joints, one/eight motions, all three workloads,
and a fixed total number of character updates per sample. Validate each actor's
pose/weight outside timing. Record actor/controller sizes and per-run parent
drift alongside both metrics. Test the working-set tradeoff before broader
candidate regressions and any PR; reject the design if it is not a useful
complete-path balance. The current prototype remains default-off.
