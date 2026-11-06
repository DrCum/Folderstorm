# Checkpoint 14p: sustained private worker-scheduler acceptance probe

Plan written after the 14o source/ownership audit, before implementation. Use a
new performance branch from its committed findings. This is a test-only step;
the viewer's existing scheduler, queues, thread counts and settings stay intact.

## Evidence and hypothesis

The current Windows pool's `sleepy_robin` polls with `Sleep(1)` and an empty
`notify`. Plan 12 measured approximately 2 ms wake medians in its requested-1ms
context versus tens of microseconds for the bundled stock `round_robin`.
Historical commit e98091e2 introduced polling because condition-variable waits
appeared too busy on Windows. Short quantized idle samples did not resolve that
concern. Plan 13 and 14e show that this delivery cost can overwhelm generated
sampling work; real same-frame animation windows still remain unproved.

Investigate the bundled implementation first. Test its idle CPU and real queue/
fiber lifecycle behavior before building a new wait backend or changing any
production pool. A successful probe supports a later proposal, not default
enablement, an FPS claim or live scene dispatch.

## Work sequence

1. Inspect the actual bundled Boost scheduler/condition/scheduled-queue
   interfaces and preserve the current production source identity. Reuse private
   named pools as in plan 12; never borrow, resize or close General/ImageDecode/
   ImageGL/mainloop. Test requested widths 1/3/8 and reject unexpected overrides.
2. Build a correctness-only harness that compares current and stock scheduling
   through the actual WorkQueue API. Cover exactly-once work, a burst, queued
   shutdown/drain, delayed work, a new immediate item while waiting for a later
   deadline, producer/close races and rejected admission without retry spinning.
   Include cooperating fibers/timers and progress/fairness checks on workers.
   All task-owned data must outlive callbacks; verify release after close/join.
3. Keep failures bounded and diagnosable in this private executable. Catch worker
   test exceptions before they leave callbacks; do not depend on a live LLApp or
   crash reporter. An actual deadlock/timeout must produce a terminal test
   result, not leave an invisible blocked destructor. Do not kill or restart
   viewer/build processes to recover a probe.
4. Add explicit opt-in measurements, separate from correctness/build-time pass
   criteria. Retain post-to-callback distributions, admissions, sustained idle
   and active worker kernel/user CPU and raw cycles where supported. Use longer
   aggregate idle intervals than plan 12, warm before counting, keep default and
   scoped requested-1ms timer contexts separate, and pair/reverse controls.
   Do not convert raw cycles to elapsed time or zero quantized CPU to zero power.
5. Run timing only with private worker state idle and a full host native-build/
   package process inventory empty. Verify before/after each bounded case, keep
   partial/interrupted data and do not restart solely on observation timeout.
   Other desktop load and timer policy remain uncontrolled and must be reported.
   Do not change affinity, priority, process/power settings or logged-in viewers.
6. Inspect idle CPU, callback correctness, timed/fiber progress and shutdown
   together. If stock regresses, retain/reject that candidate with the failure
   and preserve production polling. If it passes, write the next scoped proposal
   with the remaining full viewer startup/loading/media/idle/shutdown acceptance
   gates. Do not remove the historical workaround based on latency alone.
7. Commit the test harness, data and exact provenance separately, and open its
   own PR when independently reviewable. Clearly state that its installer has
   unchanged default scheduling. Full viewer builds/retention remain the other
   chat's responsibility; no extra profiling/storage variants are requested.

## Limits and deferred questions

This does not prove the viewer has spare cores, benefit from more workers or a
useful animation deadline. It does not replace Q4/Q7 or rendered/lifecycle tests.
The original performance objective remains incomplete. Keep those existing
questions deferred while this independent, source-supported experiment proceeds.

## Initial implementation and prerequisite finding

The new correctness harness reached its scheduled-queue case and exited through
its bounded watchdog. A smaller private reproduction then demonstrated the
problem with both current and stock scheduling: one future item posted after
the queue had become empty did not run within 500 ms, despite its 20 ms due
time. A later immediate post woke it and allowed a clean drain/join. Raw rows
and exact initial source/executable identities are retained in
`results/scheduler-initial-failure14p.json` and `scheduled-wait-repro14p*`.

This is not a stock-scheduler rejection. The source's scheduled-pop wait fixes
its adjusted deadline before delegating to a loop that can be notified by new
head items without recomputing that deadline. Its WAITING retry can also retain
an expired limit. Investigate and fix that prerequisite as separate plan14p1,
with explicit deadline/close regression coverage, before resuming this probe.
Do not remove the failing case or change production scheduler policy to obtain
a passing experiment. The current private prototype has not been published as
a PR or used for timing.
