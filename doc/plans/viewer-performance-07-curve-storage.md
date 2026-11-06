# Checkpoint 7: measure contiguous animation-key storage

Plan written before implementation. Parent: read-only animation sampling.

## Objective

Measure whether contiguous key storage reduces repeated sampling cost in crowded
scenes without changing values, load behavior or ownership. Start with an opt-in
native experiment. Publish a production storage change separately only if the
measurements and compatibility audit justify it. No worker dispatch in this
experiment: shared motion-list lifetime and edit invalidation are still gates.

## Candidate and constraints

Inspect the bundled Boost version and existing `boost::container::flat_map` use.
It offers the existing ordered-map interface and contiguous entries, but moves
entries during insertions and has different reference/iterator invalidation.
Check every production key-map consumer before selecting it. Keep curve names
and the complete sampling methods unchanged in experiments; run them with both
containers and actual viewer vector/quaternion math.

Preserve timestamp sorting and last-write-wins duplicates. Test increasing,
decreasing and shuffled insertion as well as ordinary serialized input. Do not
reserve serialized counts before validating the available data, and do not
silently change `mNumKeys` behavior. Measure allocation count/bytes and loading
cost in addition to sample latency; record any poor reverse-insertion behavior.

## Work sequence

1. Complete container/iterator audit, including serialization, pose sampling,
   loop edits, preview reload and any motion copy paths. Confirm supported Boost
   API and cross-platform headers from repository inputs.
2. Extend the focused native harness with an explicit benchmark mode. Generate
   production method bodies for both map and candidate storage in isolated
   fixture types; keep timing disabled in ordinary regression runs.
3. Compare byte-exact values, key ordering, duplicate replacement and loop edits
   after ordered/reverse/shuffled loading. Use the same seeded keys and time
   queries for both candidates, without reusing production interpolation as the
   only expected-result oracle.
4. Benchmark short and long curves, multiple independently allocated animation
   instances, forward playback, seeks and wraps. Alternate candidate order;
   prewarm and report sample count, compiler/flags, CPU, dispersion and units.
   Prevent dead-code elimination without counting output consumption as sampling.
5. Record allocation/loading results and discuss workload limits. If a candidate
   wins repeatably with acceptable load cost and safe invalidation behavior,
   write the production-change plan and publish it in its own PR. Otherwise
   retain the experiment and move to another measured hotspot.

## Acceptance and next thread boundary

Bit-exact finite-input results and sorted/duplicate semantics must pass first.
Microbenchmark results support only the tested kernel, not viewer FPS or a
whole-crowd speedup. Windows full linking and live animation acceptance remain
separate. ARM/macOS execution is pending local availability.

For workers, first design owned immutable per-asset snapshots and versioned
query/result storage. Keep joint-state writes, constraints and hand-pose priority
on the main thread, with bounded queues and no extra animation frame of latency.
Measure snapshot memory, job overhead and barrier cost before enabling dispatch.

## Results

Implemented as an opt-in native experiment; production curve storage is
unchanged. `tests/test_curve_storage.py` reuses the actual six production sampler
methods in separate map/flat fixture namespaces with real viewer math. Each
container passes the prior **56,232** sampling cases. Another **146,502** cases
compare both containers with the frozen map oracle after sorted, reverse and
shuffled insertion, including duplicate replacement, exact/neighboring key
queries, interpolation modes, empty curves and edited loop metadata. Key fields
remain unchanged. Native MSVC compile/link/run passes.

The only external production curve reader found is `FSPosingMotion`; loading
writes and serialization iteration remain within `llkeyframemotion.cpp`. No key
iterator/reference escapes an audited call. The cached package is Boost 1.90;
the tested candidate is `boost::container::flat_map`. This audit establishes no
concurrent lifetime guarantee.

With `LL_TESTS` and Python, build `avatar_curve_storage_experiment`. Its default
post-build run performs correctness checks only. Run the executable with
`--benchmark` explicitly for timings. No viewer build or installer is required
to evaluate this test-only checkpoint; a local full target link remains useful.

### Native measurement method

Ryzen 5 3600, Windows x64, MSVC 19.51.36244, `/O2 /arch:AVX2 /fp:precise`, real
cached math/common libraries, 2026-10-23. Two complete invocations, each with
nine alternating map/flat samples per case. Desktop load/clocks are uncontrolled.
Sampling uses 32 independently allocated curves and 65,536 queries per sample,
prewarmed, default linear interpolation. Patterns cover forward looping playback,
random seeks and playback starting near a wrap. Output writes are timed equally;
byte checks/hashing happen outside the timed region. Timed methods use release
assertion behavior; correctness fixtures retain assertions.

Loading times include curve-object construction and container insertion into
fresh objects, excluding destruction, source decoding and I/O. Batch size grows
for short curves to reduce timer quantization (4,096/512/64 instances according
to input size). Repeated timestamps are present. Timings use default allocators;
allocation probes use separate counted types and report one complete curve,
including the MSVC map sentinel, capacity slack and transient growth overlap.
Requested bytes exclude heap bookkeeping/RSS. Curve object size is reported
separately. Do not compare allocation counts as if they were timings.

Raw outputs retain medians and min/max for every case:
`results/curve-storage-ryzen3600-run1.txt` and
`results/curve-storage-ryzen3600-run2.txt`.

### Results and decision

Forward-playback median ranges across the two invocations, ns/sample:

| Curve | Unique keys | Map | Flat |
| --- | ---: | ---: | ---: |
| Position | 1 | 5.067–5.334 | 5.746–6.206 |
| Position | 32 | 26.607–27.428 | 13.766–13.779 |
| Position | 128 | 45.189–45.560 | 17.709–17.892 |
| Rotation | 1 | 3.224–3.337 | 6.160–6.203 |
| Rotation | 32 | 57.597–58.122 | 43.654–43.770 |
| Rotation | 128 | 77.267–78.094 | 48.950–50.121 |

At 1,024 rotation keys, reverse-order load medians are 72.8–76.4 us/curve for
map versus 253.1–262.4 us for flat; shuffled load is 107.5–114.5 us versus
240.0–247.8 us. Sorted loading favors flat in this experiment. Naive flat
insertion does not satisfy the loading-quality requirement for arbitrary input.

The same rotation curve retains 57,400 requested key bytes for map versus
35,616 for flat, with 1,025 versus 17 allocations. Flat's total allocated bytes
across growth are higher (95,160), and its transient peak is 57,888 versus map's
57,400. Fixture curve objects are 64 versus 72 bytes. Lower retained bytes do not imply
lower loading peaks or smaller objects in every workload.

**Do not change production storage to this candidate as-is.** Longer-curve
sampling gains and lower retained memory justify a follow-up, but single-key
sampling and unordered insertion regress. Plan 8 measures bulk conversion from
the existing map-loading semantics and a direct one-key sampling path. It must
account for scratch memory and loading time before any production proposal.
These are kernel measurements, with no crowd/FPS acceptance or worker dispatch.
