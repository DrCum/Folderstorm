# Private worker wait observations, in progress

38 of the planned 48 cases completed before the independent builder entered
`NORMAL_PERFORMANCE_QUEUE` at the next case's precheck, 19:35:30 UTC October 24.
No probe was launched for that case. Treat the current summary as partial.

The first pass has all 24 current/stock cases across widths 1/3/8, default and
scoped requested-1ms timers, and empty queues or finite coroutine waits. The
reversed pass has all eight-worker cases and six three-worker cases. Remaining:
three-worker default/empty stock then current, and all eight one-worker cases
in stock/current order. The preblocked three-worker stock case needs Attempt2;
its original precheck record stays intact. The current counterpart never ran.

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
summary.json/csv and listing remaining cases. The run/retention scripts are
preserved as text alongside it. The benchmark compiles fresh workqueue.cpp.

Host: Ryzen5 3600, 12 logical processors, x64 Windows, recorded in host.json.
Power/frequency state, desktop activity and global timer policy are uncontrolled.
Worker CPU ticks are quantized; raw cycles are not elapsed time or power. The
percentage column is total pool use as a fraction of one core, not whole-machine
CPU. Wake and active phases run before the finite-wait coroutines; the wait
label describes the subsequent idle phase. Active wall time includes producer
admission/queue delivery and does not isolate arithmetic cost.

Provisional pattern: stock empty queues sleep without accumulating worker ticks
or cycles in accepted samples. Requested-1ms wake p50 is around2ms for current
polling versus tens of microseconds for stock. Repeated eight-worker requested-
timer finite waits use fewer raw cycles with stock. Default-timer finite-wait
cycle totals are closer and CPU tick readings vary. No resource cap triggered,
and all accepted generated active checksums match. Finish the remaining cells
and assess uncertainty before proposing any production trial. This is not an
FPS result, a power measurement, or full viewer lifecycle acceptance.
