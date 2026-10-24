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

Not implemented yet.
