# 14v primitive: ownership before controller integration

Started from published PR 123 exact6daba8aa6619e678f19fbada673c8295922f867b
on codex/perf-owned-row-publication. All previous published heads remain stable.

The private primitive owns two fixed groups of at most32 cells, each containing
only a request/result and an atomic publication state. The worker writes a cell
once, then publishes Ready/Failed with release. An active owner lease acquires
the state before borrowing a const result. The worker never changes a published
cell in that epoch. Correctness gates own only promise/future data.

| Event | Cell data | Reuse condition |
| --- | --- | --- |
| Worker samples a row | Worker exclusively owns unpublished request/result | No owner read before acquire-ready |
| Ready/Failed publication | Immutable for the remainder of the epoch | Owner lease keeps its read epoch active |
| Executor discard/takeRequest | Releases group reference only; does not clear rows | Pool still owns the fixed group |
| Owner cancel | Signals cancellation and cancels ticket; published rows remain immutable | Owner may finish its current reads |
| Owner finish | Ends reads; cancels remaining work without waiting | Group still unavailable until exact ticket is Expired |
| Acquire next epoch | Clears identity/context/readiness, preserving vector capacity | Owner finished AND ticket acknowledged, or no callback retained |

The pool uses the executor's generation/acquire acknowledgement; it never uses
shared_ptr use_count as exclusivity. An active owner also prevents reuse after
worker completion. Pending queued cancellation must acknowledge delivery before
reuse. Epoch wrap is refused. Empty and pre-cancelled groups retain no callback.
Submitted request storage cannot be extended or have its gate changed.
Leases must finish before the pool/executor; borrowed results must not escape
their lease. Controller integration must preserve that scope explicitly.

The first primitive program exposed the failure control's reused numeric context:
the queued-cancelled request still had supported metadata in the next epoch.
Reset now clears identity/context/time/usage size while retaining capacity, and
the failure control explicitly supplies unsupported context. The original
executable/response/log are preserved. This was private premeasurement work.

The current native primitive passes73 focused checks: partial visibility with
a real worker held before row1, completed-row stability during cancellation,
no reuse before queued/running acknowledgement, no reuse while owner reads are
active, epoch reset, per-row failure with valid neighbors, stopping unstarted
rows, fixed group/row limits and empty/pre-cancelled rejection. These tests use
owned generated queries and real numerical sampling, with independent query
matching. They are not full controller/hand/Scene integration or performance
acceptance. No14v timing case exists.

Next integrate these leases into the private current-controller fixture with
matching whole-batch/direct controls. Keep every borrow scoped to its real
callback; finish ownership before signalling frame completion. Add current
frame/skeleton/asset/permission stale and character/ID/time/pose/hand oracles,
queued/running cancellation, lifetime/allocator coverage and bounded resident
capacity accounting. Only then freeze/measure complete CPU/useful-waste costs
with exact identities and full-host/private-builder idle. No production worker,
general cache/deprecated/reentrant lifetime design or real deadline is enabled.
