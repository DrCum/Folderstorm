# Checkpoint 8: refine the contiguous-key candidate

Plan written before implementation. Parent: checkpoint 7 storage experiment.

## Problem and scope

The naive flat-map candidate improves longer-curve sampling but regresses
one-key sampling and unordered loads. Keep the production viewer unchanged
while testing two specific refinements. Publish the experiment separately before
a production container PR. Do not infer overall avatar cost from sample kernels.

## Work sequence

1. Confirm the bundled flat-map ordered-unique-range API. Decode into an ordinary
   temporary map using the original per-timestamp assignment semantics, then
   construct the contiguous keys in one ordered, unique range operation. This
   preserves last-write-wins behavior and avoids quadratic reverse insertion.
   Count all scratch allocations, conversion time and overlapping live bytes.
2. Prototype a direct return for curves containing exactly one key, placed after
   the existing empty-key return. Generate the candidate from production methods
   with a checked insertion point. Compare it against the frozen map oracle for
   all durations/times in the existing fixture, across all three curve kinds.
   Check that multi-key sampling does not lose its measured advantage.
3. Extend the opt-in experiment with staged-map/bulk-flat loading and the refined
   sampler. Keep instrumentation out of timing fixtures. Repeat sorted, reverse,
   shuffled and duplicate input cases, including empty and single-key curves;
   maintain exact serialized order and fields. Keep default tests timing-free.
4. Compare nine alternating samples, at least two invocations if a production
   proposal depends on the measurements. Document retained and transient memory,
   full curve construction/insertion/conversion time, short-curve cost and limits.
5. If the refined candidate is acceptable, write the production plan covering
   loader failure paths, serialization, const iterator use, native callers,
   cross-platform compilation and live animation acceptance. Otherwise record
   the rejection and investigate another hotspot or a different builder.

## Constraints and risks

No reserve based solely on serialized counts. A temporary map remains bounded
by unique timestamps, as in the original loader. A vector of every serialized
entry would grow with duplicates and needs a separate memory justification.
Bulk conversion temporarily holds both representations; explicitly measure that
peak and release scratch data per curve. Do not keep a second permanent cache.

Existing short/failed input handling and `mNumKeys` diagnostics must be audited
before production adoption. No change to loop timing, quaternion interpolation,
key values, hand-pose priority or live joint writes. Shared raw motion-list
ownership still prevents borrowing these containers in asynchronous jobs.

## Results

Implemented in the native experiment with `test_curve_storage.py --refined`.
The production viewer retains #95's map storage and sampling algorithm. The
candidate takes a direct one-key return after empty handling and uses the
bundled ordered-unique-range constructor on a temporary map. The temporary map
uses original per-timestamp assignment, preserving last-write-wins duplicates.
No reservation trusts serialized counts, and no permanent second cache is added.

Build `avatar_curve_bulk_loading_experiment` with `LL_TESTS` and Python. Default
post-build execution performs correctness checks only; `--benchmark` is opt-in.
Three fixture sampling suites each pass **56,232** comparisons and **293,004**
cross-container cases compare naive/refined flat storage with the frozen map
oracle. The refined loader is exercised by those cross-container cases. Key
ordering, duplicate values, metadata and sampled bytes match, including empty,
single/multiple keys and all interpolation modes. Native MSVC linking/run passes.

### Measurement and limits

The method, CPU, compiler and timing boundaries match checkpoint 7. Staged load
timing includes scratch-map construction/insertion, bulk conversion, release of
scratch nodes, and fresh curve objects. It excludes source decoding/I/O and final
curve destruction. Scratch and destination allocations share one counter in
separate untimed probes, so overlapping bytes and retained capacity are measured
together. Timing fixtures use default allocators and release assertion behavior.
Allocation probes check that nonempty inputs produce observed live key storage.

Two finalized nine-sample alternating runs are retained in
`results/curve-bulk-ryzen3600-run1.txt` and
`results/curve-bulk-ryzen3600-run2.txt`. These are synthetic workloads with
uncontrolled desktop activity. In the second run the 128-key position case has a
clear timing disturbance (both candidates much slower); keep that evidence and
do not discard it as if every case had stable absolute timing.

Representative forward-playback medians, ns/sample:

| Curve | Keys | Original map, run 1 / 2 | Refined flat, run 1 / 2 |
| --- | ---: | ---: | ---: |
| Position | 1 | 5.391 / 5.072 | 3.296 / 3.471 |
| Position | 32 | 27.728 / 27.396 | 13.338 / 13.483 |
| Rotation | 1 | 3.154 / 3.378 | 3.589 / 3.786 |
| Rotation | 32 | 56.073 / 57.458 | 43.364 / 44.426 |
| Rotation | 128 | 76.125 / 77.289 | 48.758 / 49.261 |

The one-key shortcut removes most of naive flat rotation's penalty, but it still
costs about 0.4 ns more than the original map in these cases. It improves
one-key position sampling. Longer-curve advantages persist; no claim is made
about the actual distribution of curve lengths in the user's crowded scenes.

At 1,024 rotation keys, reverse loading medians are 65.3–67.3 us for map and
87.7–87.9 us for staged flat; shuffled loads are 103.6–104.9 versus
140.4–140.7 us. This avoids naive flat's 240–262 us unordered-loading behavior,
but still adds conversion and scratch teardown cost. Single-key loading is about
63–64 ns for map versus 154–156 ns for staged flat. The experiment does not
justify claiming faster asset loading.

The 1,024-key rotation fixture retains 57,400 requested key bytes for map and
24,576 for staged flat. Peak key bytes rise to **81,976** (about 43% higher than
map); total allocation calls are 1,025 versus 1,026. Scratch is released at the
end of each curve load. Those bytes exclude allocator bookkeeping and fixture
objects, and do not establish whole-viewer peak memory.

### Decision

Do not enable the candidate in normal builds yet. It is suitable for a separate
experimental full-viewer build because it preserves tested values and offers a
plausible steady-sampling benefit, with explicit loading/peak-memory tradeoffs.
Plan 9 introduces an off-by-default build option so the building agent can retain
matched map/contiguous variants. Real asset distributions, full linking,
animation/pose/preview lifecycle checks, crowd timing and arrival/rez behavior
remain required. Worker snapshot ownership and dispatch remain later gates.
