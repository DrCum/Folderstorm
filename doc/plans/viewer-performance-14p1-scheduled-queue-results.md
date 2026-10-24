# Checkpoint 14p1: scheduled queue deadline results

Implemented at `8720509deb393c05e51bdef94235945ccd0a8c7b` from audit
`db9be862ab`. This is an independent correctness prerequisite for the unfinished
14p worker experiment, preserved at `ccad4c341c` on its own branch.

## Change and reproduced failures

The previous scheduled queue selected its wait deadline once, then delegated to
a base loop that retained that deadline across notifications. A future task
posted to an empty waiting queue could retain the bootstrap 24-hour deadline.
A newly inserted earlier future head could retain the later head's deadline.
The surrounding WAITING retry could also consume a future item beyond a timed
pop caller's deadline.

The replacement attempts the existing pop under the lock, returns ready/done,
then waits once until the earlier of the current head timestamp and caller
limit. It rechecks and recomputes after a signal or head timer, and returns the
existing empty/waiting status at the caller limit. The base pop retains capacity
notification and payload handling. The original upstream body is preserved in
an FS comment. There are no new fields, pool policies or thread counts.

The public timed mutex may reject a call whose lock deadline has already
expired. The test permits that existing behavior and checks that the ready item
remains available. The initial pop attempt after acquiring the lock is intact.

## Native evidence

The same fixture, compiled before the header change, reports four failures in
22 checks: later work was consumed across the caller limit, and both future
delivery cases needed a diagnostic immediate post. That diagnostic allows the
old workers to finish normally. Its mode skips the known old timed-close stall;
the original watchdog failure remains preserved in the private probe commit.

After the change, 340 checks pass, including direct value-queue deadlines,
empty-to-future delivery, replacement by an earlier future head, and timed
close/drain with 1/3/8 workers. Captured payloads release after join, all accepted
callbacks run exactly once and none runs before its timestamp. Closed queues
reject further work. The unchanged frozen 14p fixture also passes all 6,686
current/stock queue, fiber, lifetime and shutdown checks, including its formerly
stalled scheduled case.

Both executables compile fresh `workqueue.cpp`, avoiding old inline scheduled
queue instantiations in cached libraries. The 32 immutable internal dependency
hashes were reverified. Normal and Tracy standalone compilation passed for
fresh `workqueue.cpp` and the regression test. Native checks use MSVC x64 AVX2,
optimized precise floating point and read-only Boost 1.90.0 packages. Source,
executable, command and log hashes are in
`results/scheduled-queue-provenance14p1.json`; short raw results are adjacent.

The new LL_TESTS target `scheduled_queue_deadline_test` runs after build. These
are functional observation bounds, not measurements of scheduler latency or an
FPS improvement. No viewer login, rendering or full application lifecycle test
was performed here. Independent exact-head full builds remain necessary.

## Separate testing and next work

Build this complete head independently of the unfinished private scheduler
probe. Check normal startup, asset loading, media, leaving regions, idle and
shutdown in the retained viewer. Keep scheduling and quality settings at their
existing defaults. Do not merge based on native fixture results alone.

Next, integrate this tested prerequisite into the private probe branch and
follow the separately written 14p2 continuation plan. Sustained idle CPU, finite
fiber waits, active work and paired wake observations must all inform a future
proposal. Production Windows polling remains unchanged at this checkpoint.
