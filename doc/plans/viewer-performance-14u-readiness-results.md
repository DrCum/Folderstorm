# 14u: later callbacks use work, but the batch still costs more

The controlled experiment is implemented at
6c5012aadc2fac5a5158e0809f705938deda9399, based on PR 122 exact
b7c60446cfdf3f67565690a0b1ffaa3c7f8048be. Only private fixture code changes.
There is no production scheduler, cache, dispatcher, quality or frame-delay change.

## Transition proof

Normal and allocation-instrumented native builds each pass 123,711 checks;
normal/Tracy syntax passes. In addition to the complete controller oracles, 52
deterministic transition cases hold workers using owned promise/future data,
then release at callback 2, the middle callback or the last callback. They verify
early direct execution followed by later applications, first-consumption discard,
no readiness, rejection, unsupported-context failure, frame/skeleton/asset/
permission staleness between consumption points and final cancellation/cleanup.
Character/ID/time callback order and direct pose/hand output match.

Only correctness controls wait to establish a known transition. The worker gate
contains no Scene, controller, motion or joint pointer. Timed requests have no
gate, callback hook, forced readiness or synthetic gap. Correctness admission and
startup use post to avoid treating legitimate tryPost contention rejection as a
failed ready oracle; timed submissions keep the original nonblocking tryPost.

The original unlabelled failed control and later diagnostic result are retained.
Source proves that nonblocking admission can reject a failed try_lock, but the
initial failure has no captured case context, so its exact cause remains uncertain.
The completed controlled evidence does not rely on a passing rerun explaining it.

## Complete costs

All 16 fixed paired/reversed timing cases and eight separate allocation cases
pass exact program/argument/frame, resource/cleanup, callback, useful/wasted-work
and nonoverlap validation. Timing uses 1,024 frames per case: 16,384 frames and
270,336 controller callbacks. Allocations use 64 frames. Before/after guards
confirm private-builder/full-host compiler/packager/probe idle for every case.

The fixed generated cohort still uses four shared immutable assets, four
overlapping ordinary motions per character, 32 joints and 32 keys. Current mask
prediction admits one motion per character. A private real production pool uses
effective width 1, General-sized capacity and one bounded pending batch. Retention
is limited to the controlled Scene lifetime and released before destruction.
CMake target integration, other platforms, general deprecated/reentrant lifetime
handling, live deadlines and FPS remain separate unproved gates.

Crowded 32-character observations across the two orders:

| Mode | Applied callbacks | Owner cycle window / direct | Combined cycle window / direct |
| --- | ---: | ---: | ---: |
| Event, first callback only | 0% | 0.959 / 1.125 | 0.989 / 1.160 |
| Event, later callbacks | 48.05 / 48.02% | 1.070 / 0.904 | 1.666 / 1.428 |
| Polling, later callbacks | 0% | 1.045 / 1.276 | 1.052 / 1.286 |

The event later mode's median first ready callback is 18. Roughly 52% of its
sampled joints are unused. Its owner p50 is 298.6/195.3us versus direct
231.9/217.1us. Its p99 is 539.5/334.6us versus direct463.8/330.0us. This does
not establish repeatable owner or tail savings. In the one-character control,
later event application is 0/0.10%, with combined cycles 2.019/2.139 times direct.
No useful independent callback span exists in that control.

Cycle windows include bookkeeping, bounds/cleanup checks and final drain. Kernel
cycles are attribution within them, not extra CPU to add again. Raw cycles are
not energy or viewer FPS. Unrelated desktop load, power and timer policy remain
uncontrolled beyond the documented requested1ms timer and build/probe-idle guards.
The apparent first-only owner improvement in one order, despite zero applications,
also shows why isolated timing differences must not be called a viewer gain.

Direct allocates zero. In the crowded allocation run, event later consumption
records 218 replacement-new calls, 140,365 requested bytes and 126,157 peak tracked
bytes; first-only records 2,798 calls and 1,261,072 requested bytes. Readiness and
discard/recycling alter these counts, so this is not a drained warm comparison.
Every cached case holds 16,384 binding bytes and has zero after final cleanup.

PR 121 and PR 122 are independently retained as 7.2.5.82297/7.2.5.82299, each
with 39 focused checks. Their exact heads and installer hashes were reverified.
Build evidence remains distinct from live General/crowd/quality acceptance.

## Decision and next plan

Do not integrate this whole-batch candidate. Later callbacks make some work
usable, but combined CPU rises substantially and owner savings vary by order.
No viewer optimization or FPS improvement is enabled by this checkpoint.

Plan 14v tests a separate premise: publish individual completed motion results
instead of waiting for the whole cohort. The design must preserve bounded owned
buffers, per-row acquire/release publication, cancellation acknowledgement,
exclusive reuse and independent current-context checks. It remains private;
no real-world overlap, mutable asset or Scene lifetime is assumed safe from this
dataset. Q4/Q7 and the user's manual testing stay deferred.
