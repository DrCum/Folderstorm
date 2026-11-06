# 14v: bounded publication of individual owned motion results

Written after completing 14u and before implementation. Start from its published
exact head on a new branch. Preserve all previous controls, data and PR heads.

14u's event batch is usable at roughly callback18 of32 and wastes about52% of
sampled joints. Combined CPU rises42.8–66.6% versus direct, without repeatable
owner savings. A finished early motion currently waits for the last motion in
the batch. Test whether earlier per-motion publication changes that complete-path
cost. Keep a matching whole-batch control; do not infer a viewer deadline.

1. Design a private fixed-size owned group with at most32 motion cells. Allocate
   cell storage before submission. Each cell holds only a SamplingRequest,
   SamplingResult, observation data and an atomic publication state. Worker
   sampling completes a cell, then publishes success/failure with release;
   the owner acquires that state before any const result access. A published
   result remains immutable for the rest of the group's epoch. No vector resize,
   result clearing or lease mutation may race an owner reading a published cell.
2. Keep one pending callback through the existing bounded executor. Its final
   result/discard hook releases only a group reference; it must not clear cell
   results while the owner may read them. Each real callback either validates
   its current published motion result or immediately runs direct. Preserve
   character/ID/time order, source math and hand publication. No spinning, wait,
   additional frame, Scene/joint pointer in a job or production dispatch.
3. Bound reusable storage to at most two owned groups. A group is reusable only
   after its owner frame has finished all reads and the exact executor ticket is
   acknowledged Expired (or submission retained no callback). Use the existing
   generation/acquire acknowledgement; do not infer exclusivity from shared_ptr
   use_count. Reset publication/requests/results only after this proof. Frame
   cancellation stops future consumption, signals the owned group and cancels
   the ticket; running sampling may finish safely on owned immutable data.
   Published cells stay intact until acknowledged reuse or final destruction.
4. Add deterministic controls for partial publication, early misses/later hits,
   failed and unstarted rows, cancel-before-start and cancel-during-work,
   outstanding leases, generation reuse and no reuse before acknowledgement.
   Verify byte-exact current controller/hand output and character/ID/time order,
   including frame/skeleton/asset-revision/permission staleness. Retention remains
   within the controlled Scene lifetime; general viewer/deprecated/reentrant
   lifetime and mutable live-asset ownership are not established here.
5. Measure capture, publication/check/application, fallback, sampled-but-unused
   work, complete owner/worker cycle windows, allocation calls/bytes and resident
   group capacities. Freeze a small paired/reversed matrix before collection;
   use direct and matching whole-batch controls with no artificial gap. Require
   exact identities, full-host/private-builder idle and preserved exclusions.
   Reject the candidate if complete economics still do not support useful owner
   savings. Do not optimize an isolated pure kernel and hide buffer/queue costs.
6. Publish a separately reviewable test-only checkpoint and write the resulting
   next plan. Any live pilot requires separate phase/ownership/deadline evidence,
   Q4/Q7 and eventual rendered/crowd/lifecycle acceptance. No production change
   or extra profiling variant is part of this experiment.
