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

Not implemented yet. Production storage and sampling algorithms remain those of
#95. Worker snapshot ownership and dispatch remain later roadmap gates.
