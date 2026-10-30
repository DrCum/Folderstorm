# 14s1: correct result lifetime before accepting timings

Plan written before this correction. No14s timing has run; the sole attempted
case stopped at its build-idle precheck. The previous goal turn made progress:
published PR120, then saved the native-checked14s experiment. PR120's build is
currently confirmed live by owner PID32580 and compiler/MSBuild activity.

Source review at14s head8b2b248 found that Frame owned a new Result each frame.
Recycling returned the input envelope, but the sampled row capacities were freed
when Frame left scope. The previous batch prototype retained its output storage.
This would benchmark avoidable recurring allocations and also leave that result
destructor outside the recorded owner interval. Correct it before measurement,
without changing the candidate's admission, first-consumption cancellation,
numeric checks, controller ordering or fallback behavior.

Keep one reusable owner Result alongside the reusable Request for the entire
trial. Frame borrows it only on the owner thread; the executor still transfers
exclusive completed buffers by swap. At most the one bounded slot and one owner
result retain row capacity. Workers receive no frame/scene pointer. Rejection and
late cancellation keep the existing acknowledgement rules. The setup/retention
cost remains visible to the separate allocator instrument.

Measure each complete Frame lifetime, including its owner setup and cleanup.
Also add a raw owner-cycle window covering the complete loop and final drain,
matching the existing worker-cycle window. Keep per-frame and per-job counters
for attribution and label final-drain/bookkeeping overhead. Preserve the old
executable and proof, create uniquely named corrected executables, rerun the
same meaningful correctness controls, then update identities and the fixed
prewritten matrix runner. No production source or published PR head changes.

Only after the independent builder is idle may the unchanged planned matrix
start. Do not interpret this instrumentation correction as a viewer speedup.
