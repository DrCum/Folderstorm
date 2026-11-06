# Checkpoint 14p1: recompute scheduled-queue wait deadlines

Plan written before the fix. Branch from completed audit db9be862ab, independently
of the unfinished private scheduler experiment. Retain the latter at
`ccad4c341cb10ca33e8f0a8fc20a0d340f548335` without publishing its failing harness.

## Reproduced problem and source cause

The initial 14p scheduled test reached its bounded watchdog. A smaller private
actual-WorkSchedule reproduction then posted one item due in 20 ms to a warmed,
empty queue. Both current and stock pools failed to deliver it during 500 ms of
observation. A later immediate post rescued it and allowed clean close/join.
The raw result, source and executable identities are preserved in the probe
commit's `doc/plans/results/scheduled-wait-repro14p*` and failure record.

`ThreadSafeSchedule::tryPopUntil_` chooses an adjusted deadline once, then calls
the base wait loop. That loop can wake for a new future head and wait again using
the old deadline, without returning to the subclass to recompute it. Starting
from an empty queue can therefore retain the bootstrap 24-hour wait. The outer
WAITING loop can also retry an expired caller limit until a future item is due.
These are queue-contract concerns independent of the pool's wait backend.

## Work sequence

1. Add a narrow actual queue regression fixture before changing the header.
   Reproduce empty-to-future delivery and earlier future-head replacement with
   a later diagnostic poke to allow clean old-code termination. Separately
   verify that a timed pop does not consume an item scheduled beyond its caller
   limit. Preserve ready-item/expired-limit and normal timed-item behavior.
2. Replace fixed-deadline delegation with an owner-of-the-lock loop: attempt the
   existing base pop; return ready/done; compute the earlier caller/head deadline
   under the lock; wait once; recheck/recompute after every signal or head timer.
   Return the existing empty/waiting status when the caller's wait expires.
   Preserve priority, queue capacity, admission, payload ownership, close/drain
   semantics and the initial immediate pop attempt. Add no fields or scheduler
   policy changes. Preserve the old upstream code in the FS comment block.
3. Test actual WorkSchedule delivery and timed close/drain with 1/3/8 consumers,
   plus exactly-once/non-early callbacks and release after join. Include direct
   value-queue timeouts and expired ready attempts. Keep test failures bounded;
   do not let a known old close stall hide in a destructor or affect a viewer.
4. Compile fresh workqueue.cpp for native checks so old cached inline template
   instantiations cannot silently supply WorkSchedule::pop_. Preserve old and
   fixed executables/source identities. Rerun the unchanged frozen 14p harness
   against the fix, including its formerly stalled scheduled case. Inspect
   standalone normal/Tracy compilation. No timing/FPS claim follows from these
   functional observation bounds.
5. Publish this queue correctness change as its own exact-head PR based on the
   completed audit, with the full preceding performance/feature stack. The
   independent builder owns full native builds/retention. Keep default Windows
   polling, thread counts, graphics and scene callback methods unchanged.
6. Integrate the tested prerequisite into the preserved private probe branch,
   then continue 14p's sustained scheduler acceptance experiment. Keep its real
   CPU/coroutine/shutdown and full viewer gates; do not weaken the failing test.

## Acceptance and limits

Future callbacks should run from their own due time without another submission,
an earlier head must update the wait, and caller deadlines must remain bounded.
OS scheduling can still make a ready callback late; no hard-real-time guarantee
or numerical timing threshold is introduced. Close still drains accepted timed
work, including waiting for future timestamps. This does not enable worker
animation sampling or prove a crowded-viewer FPS improvement.
