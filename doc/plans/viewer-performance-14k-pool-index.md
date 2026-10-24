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

Not implemented yet.
