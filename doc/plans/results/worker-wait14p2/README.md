# Private worker wait observations, complete matrix

38 of the planned 48 cases completed before the independent builder entered
`NORMAL_PERFORMANCE_QUEUE` at the next case's precheck, 19:35:30 UTC October 24.
No probe was launched for that case. After PR117 was retained and ownership
released, the remaining ten cases completed. All 48 cells are now verified.

The first pass has all 24 current/stock cases across widths 1/3/8, default and
scoped requested-1ms timers, and empty queues or finite coroutine waits. The
reversed pass now has all 24 cells. The preblocked three-worker stock case was
completed as Attempt2; its original precheck record stays intact.

Two early cases overlapped because the next command was started before the
previous exec session completed. Both entire cases are explicitly excluded,
their originals retained, and clean sequential Attempt2 replacements completed.
A full-host concurrent-probe guard was added. Every accepted case has a terminal
exit0 plus before/after idle build-state, worker-state and native compiler/
packager observations. No viewer/build process was stopped to get a window.

`progress.json` retains exact source/executable/response/dependency identities,
scratch byte hashes and LF-normalized retained input hashes. Raw CSVs and their
matching JSON observations are retained. LF normalization makes verification
independent of Git checkout newline conversion; scratch originals are intact.
`python analyze.py` verifies those inputs, unique comparison cells, expected
active checksum, 64 wake observations and build-idle boundaries before generating
summary.json/csv and listing remaining cases. It also checks CSV context labels,
unique sample indexes, 30-second idle intervals and nonoverlapping accepted
case windows. The run/retention scripts are
preserved as text alongside it. The benchmark compiles fresh workqueue.cpp.

Host: Ryzen5 3600, 12 logical processors, x64 Windows, recorded in host.json.
Power/frequency state, desktop activity and global timer policy are uncontrolled.
Worker CPU ticks are quantized; raw cycles are not elapsed time or power. The
percentage column is total pool use as a fraction of one core, not whole-machine
CPU. Wake and active phases run before the finite-wait coroutines; the wait
label describes the subsequent idle phase. Active wall time includes producer
admission/queue delivery and does not isolate arithmetic cost.

The final comparison is in `../../viewer-performance-14p2-worker-wait-results.md`.
Polling wake p95 is consistently about16ms with default timers or2ms with the
scoped request; its p50 is phase-dependent and sometimes fast. Stock p95 ranges
31-143us default and15-25us requested. Stock empty pools record zero idle ticks/
cycles, while default finite-wait cycle totals are closer between schedulers.
Stock completes the generated burst faster but uses more active worker cycles
at widths3/8. That tradeoff argues for a limited one-worker General trial, not a
broad adoption. This is not an FPS/power result or full viewer acceptance.
