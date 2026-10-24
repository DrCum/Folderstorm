# Checkpoint 14r: performance-stat read results

Implemented at `1330a9d0c921561d5a7affa7fd683a11c820bcd8`, independently from the
unfinished worker experiment, based on PR117 exact22ea4cee92. No worker policy,
producer mathematics, frame selection, tuning settings or quality rate changed.

The public getters now use a const lookup and return zero for missing keys.
Previously they returned zero by inserting a zero array into the read map. The
scene getter delegates to the same lookup with the original null key. Existing
data/signatures/indexing are preserved; original upstream expressions remain
in FS comments. A named test friend adds no runtime fields or initialization.

## Native before and after

The same native fixture fails six old-code read/mutation/allocation checks and
passes all 312 checks with the change. Common numerical comparisons reproduce
the frozen old expressions for all valid object/stat indices and both read
buffers, including nonzero data, null, absent, erased, replaced and reset IDs.
Maps and summary arrays remain unchanged across fixed reads. Allocation pressure
no longer prevents a missing read from returning zero; that path completes one
additional value check compared with the old executable's311. Real doUpd
insertion still fails under injected allocation failure and publishes 77 after
retry, demonstrating that the allocation hook is active and writes remain real.

In 1,538 reads on initially absent data across empty reused maps, replacement-new
requests drop from 522 / 115,148 bytes to zero. The old getters materialize 514
nodes; the new getters create none. Earlier value fixtures leave some bucket
capacity, so this is not a process-start allocator-footprint measurement.
The fixture's replacement allocator is isolated from the viewer executable.

The fixture includes the actual inline getters and doUpd and defines the
existing static buffer storage. It does not instantiate the singleton/window,
execute the full frame-publication path or exercise live autotuning. Full viewer
UI/lifecycle acceptance remains required. Native counts do not prove a material
crowded-scene FPS improvement; many reads already hit recorded entries, and the
per-avatar callers are in the performance floater.

Normal standalone compilation passed for llperfstats.cpp, the performance UI
consumer and fixture. Tracy compilation passed for llperfstats.cpp and fixture;
both modes also compiled the fixture with its actual allocation-test definition.
The new LL_TESTS target performance_stats_read_test uses the existing viewer
include context and related library targets. Its full CMake integration is not
claimed verified by a standalone compiler run. Exact executable/source/response/
log hashes and the 32 reverified immutable library identities are in
`results/stats-read-provenance14r.json`, with short raw results adjacent.

## Next sequence

Preserve this branch and finish the pending14p2 idle-only matrix on its own
branch first. Publishing another build target immediately would consume that
idle window. Then publish each completed checkpoint as its own PR with explicit
base/head and separate native/full-viewer acceptance. Write the next worker
trial plan only after the complete CPU evidence supports it. Keep the user's
performance UI/autotuning/crowd findings deferred while they are working.
