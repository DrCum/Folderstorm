# Checkpoint 14k: measure a contiguous blender-pool index

Plan written before implementation. Start from the published 14j head on a new
performance branch. Keep its review and build independent.

## Evidence and objective

The active collection is contiguous and warm pool lookup now searches once.
Each joint-state admission still traverses a pointer-key tree. Investigate a
sorted contiguous pointer index with the same `std::less<LLJoint*>` ordering.
Measure warm locality, cold insertion, growth and retention before changing
production storage. This is a candidate, not an assumed improvement.

## Work sequence

1. Preserve the 14j source, fresh binaries and fixture hashes. Compare the
   current tree with a sorted contiguous index over real joint/blender pointers
   in a test-only fixture. Include hits/misses, 0/1/3/32/128/256/1024 keys,
   sequential/reverse/shuffled insertion and repeated/shared queries. Record
   cold work, warm lookup cost, retained capacity and allocation failures.
2. Require identical lookup/publication/duplicate results and pointer-key order.
   Preserve heap-owned blender addresses while index storage grows. Existing
   entries must remain canonical if they appear during construction; failed
   publication must release the candidate and leave the index valid.
3. If the candidate has a useful cost/memory balance, integrate it with the
   actual pose/controller path. Keep numerical blending, every state admission,
   active reverse order and cache/interpolation lifetimes unchanged. Preserve
   ascending pointer-key destruction without adding teardown allocations.
   If the experiment loses, retain that evidence and select another measured
   target instead of changing default storage.
4. Compare actual outer operations and full controller traces with the frozen
   parent in both curve modes. Exercise growth, clear/re-add, shared/disjoint
   states, constructor/publication failure and retry, duplicate publication,
   skeleton-like joint replacement and destruction. Keep warm updates free of
   new allocation and account for capacity/object-size changes explicitly.
5. Take paired complete collection/controller costs with the independent builder
   idle. A lookup-only result cannot establish full-path improvement. Inspect
   small-case regressions and cold insertion costs rather than reporting only
   favorable large inputs. Preserve complete source/executable/report identity.
6. Publish a separate implementation PR if justified, with exact parent and
   normal private build instructions. Record experimental results and the next
   plan either way. Viewer crowd/visual/memory acceptance and the real shadow
   window remain open gates; this does not authorize worker dispatch.

## Results

The test-only experiment is implemented on `codex/perf-blender-pool-index`,
from PR 114 at `514dc0acd4f12e5413718dfafa1c046c778e843e`. Production remains
the tree. The sorted-array candidate is rejected for default adoption.

`indra/llcharacter/tests/blender_pool_index_experiment.cpp` compares actual joint
and heap-owned blender pointers. Both indexes use `std::less<LLJoint*>`; absent
keys are interleaved real joint addresses. Ascending, descending and fixed-seed
shuffled insertion cover 0/1/3/32/128/256/1024 entries. Lookups cover all admitted
keys, missing keys and a shared three-key workload with 25% misses. Six samples
per order alternate backend order; two complete runs retain 2,016 raw rows.
Each matched backend pair has the same result checksum. No replacement
allocator is compiled into the timing executable; batch calls remain no-inline
and consume their results. Index build/destruction excludes joint/blender
construction, pose application and the rest of the controller.

Optimized MSVC x64 AVX2 native checks pass: 47,517 ordering/canonical/value/miss
checks, plus 21,931 allocation/growth/retry checks. Failure injection exercises
32 tree-node and 10 array-growth failures; every partial index remains usable,
retry admits all 32 keys, and teardown returns counted live bytes to zero.
These tests cover index storage; the production constructor/publication reentry
tests remain in the preceding checkpoint. No array pool was integrated.

Selected medians on Ryzen 5 3600, from the shuffled order (12 samples per entry):

| Component | Entries | Tree | Sorted array |
| --- | ---: | ---: | ---: |
| Hit lookup, ns/key | 3 | 1.98 | 2.91 |
| Hit lookup, ns/key | 32 | 6.30 | 7.60 |
| Hit lookup, ns/key | 128 | 9.58 | 10.54 |
| Hit lookup, ns/key | 1,024 | 42.60 | 39.83 |
| Shared/mixed lookup, ns/key | 128 | 8.03 | 9.88 |
| Shared/mixed lookup, ns/key | 1,024 | 13.72 | 15.78 |
| Cold index build/destruction, us/index | 128 | 6.90 | 4.58 |
| Cold index build/destruction, us/index | 1,024 | 106.24 | 162.15 |

Order matters: cold 1,024-entry ascending array construction takes 12.40 us
against 70.94 us for the tree, but descending takes 269.74 us against 72.27 us.
Preserve both extremes; an append-heavy case alone would misrepresent costs.
At 128 entries counted retained index allocations are smaller in the array;
at 1,024 they are 17,095 bytes versus 49,200 tree bytes. The array object grows
16 to 24 bytes and has 17,056 capacity bytes. Its 39 extra counted bytes are
the MSVC large-vector allocation alignment allowance. These figures exclude
the prebuilt joints/blenders, custom aligned blocks and allocator/RSS overhead.
All 42 memory rows and all lookup/cold timing rows remain in `results/`.

The independent builder was idle and the complete native/package process
inventory was empty before and after each timed run. Immutable internal
libraries and external dependencies were read only. No viewer/source/cache
ownership, account settings or build monitor was changed. Correctness and
memory results do not override the warm lookup regressions. Full-controller
integration and a runtime build of this rejected array candidate are unnecessary.

Optional LL_TESTS targets are excluded from the default build:
`avatar_blender_pool_index_experiment` (no arguments for correctness,
`--benchmark` for timing) and `avatar_blender_pool_index_allocations`
(`--allocations`, Tracy OFF). The native wrapper compiles fresh `llpose.cpp`
and this experiment, not a cached old-layout pose object. Complete source,
report, executable and dependency identity is recorded in
`results/blender-pool-index-provenance14k.json`.

The next plan is `viewer-performance-14l-pool-lookup-hints.md`. It measures a
bounded lookup hint against this unchanged authoritative tree, with explicit
miss/collision/memory limits. If no complete-path improvement survives, stop
pool-container work and select another measured cost. This rejected experiment
does not need a new viewer testing PR; retain its source/evidence on the
isolated branch and carry it forward without changing PR 114.
