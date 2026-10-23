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

Not implemented yet. Initial audit for checkpoint 6 found no persistent key-map
iterators, and deserialization uses ordered-map `operator[]` replacement. This
does not establish concurrent safety or acceptable flat-container load cost.
