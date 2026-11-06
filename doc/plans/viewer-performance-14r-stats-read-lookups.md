# Checkpoint 14r: nonallocating performance-stat reads

Plan written before implementation. This independent branch starts from exact
PR117 head `22ea4cee92ae72d315ecc4355241abd6bbe78ff3`. The unfinished worker wait
experiment stays pushed at `6987af2ac2bbee2667723f5d774556b4371cee65` on
`codex/perf-worker-wait-probe`: 38/48 accepted cases, with ten idle-only cases
still pending during the independent build. Do not mix its test-only changes
into this branch or run its retainer from here.

## Observed source cost

StatsRecorder::get and getSceneStat use unordered_map::operator[] on the read
buffer. Asking for an absent avatar or scene returns zero but also inserts a
zero array, potentially allocating a node and buckets. Actual callers include
the performance floater's avatar rows and viewer/autotuning scene consumers.
Current aggregation is synchronous. Missing data need not be materialized by a
reader to preserve its returned zero. This is a concrete avoidable allocation,
not evidence that it is a significant current crowded-scene hotspot.

## Work sequence and acceptance

1. Build a narrow native fixture around the actual inline public read methods.
   Give a named test friend access to seed/inspect the private static buffers;
   add no fields, runtime initialization or viewer hooks. Define the existing
   static read storage in the fixture, without constructing a viewer/window or
   calling the StatsRecorder singleton constructor. This validates read methods,
   not full autotuning, frame publication or application lifecycle.
2. Before changing the getter bodies, preserve actual native old-code results:
   populated/absent values, both read-buffer selections, map contents, allocation
   counts/bytes and injected allocation failure. Use the existing replacement-
   new probe only in its separate allocation executable. A frozen old lookup on
   a separate map provides value comparison; do not use it as a new-state oracle.
3. Use a const map lookup and return zero when the key is missing. Delegate the
   scene null-key read to the same lookup. Preserve signatures, valid-stat
   indexing, buffer selection, existing values and all producer/summary/tuning
   methods. Preserve the old upstream bodies in FS comments. Do not cache live
   entry pointers or introduce any worker or frame delay.
4. Native checks must compare all valid object/stat types, known nonzero arrays,
   absent/erased/reset/replaced IDs, both buffers and summary arrays. Verify reads
   leave the seeded maps unchanged and add zero replacement-new calls, including
   cold misses. Missing reads must work under injected allocation failure; later
   real writes must still allocate/publish normally. Do not weaken existing
   numeric-value comparisons to obtain an allocation result.
5. Add an LL_TESTS regression target with the viewer's required header include
   context, linked without starting a viewer. Inspect normal/Tracy compilation
   and focused native behavior. Use immutable internal libraries/read-only
   packages; no shared source/cache writes. Timings, if useful, use a separate
   uninstrumented executable and only a verified build-idle window. Allocation
   counts and native values do not establish FPS or rendered acceptance.
6. Retain exact before/after source/executable/dependency evidence. Finish the
   pending worker matrix on its own branch when the builder releases ownership;
   avoid publishing another head that would immediately consume that idle
   window. Publish each completed checkpoint as its own PR and keep dependencies
   explicit. Do not merge or operate accounts, caches, uploads or viewer settings.

Later viewer checks include correct scene/individual-avatar values in the
performance UI and unchanged normal autotuning behavior. Keep those deferred
while the user is working. The objective remains stable, quality-preserving
whole-viewer performance; this read-only result must not be described as a
threading implementation or a measured whole-frame improvement.
