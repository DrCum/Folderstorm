# Checkpoint 12: bounded sampling execution and queue suitability

Plan written before implementation. Parent: owned sampling request/result contract.

## Objective

Prototype bounded execution over the owned sampler without connecting it to live
playback yet. Prove exclusive buffer ownership, nonblocking admission, safe
cancellation/late completion and shutdown. Measure the queue wake behavior that
governs same-frame overlap. Thread count or kernel-only speed is insufficient
evidence for integration.

The original plan combined the execution protocol with complete-cost sampling
benchmarks. During implementation these were separated into reviewable test
checkpoints: this plan covers the protocol and reproducible wake probe; plan 13
covers complete preparation/queue/sampling/validation/application/fallback costs.
Neither checkpoint authorizes a live playback caller or scheduler change.

## Existing infrastructure audit

- The viewer's General pool defaults to three workers and has a configurable
  width. ThreadPool's default queue capacity is 1,048,576; do not shrink or close
  that shared queue for this experiment. Bound this feature's own slots/in-flight
  work independently.
- Work is `std::function<void()>`, so a move-only request needs an owned job/slot
  envelope. Account for callback allocation/reference-count costs.
- `WorkQueue::post` can block waiting for capacity. `tryPost` uses the queue's
  nonblocking tryPush and returns success only for an actual enqueue. Do not use
  queue.size() as an admission guarantee. Queue allocation failure retains the
  existing application's exceptional handling; do not promise global OOM recovery.
- ThreadPool close closes/drains its queue and joins workers. Own-job shutdown
  must not close another service's queue or wait on a callback that needs the
  main loop. The current close implementation performs joins inside the
  not-already-closed branch: externally closing a pool's queue is not proof its
  workers have joined. Use the pool's close operation for owned test pools and
  audit closed-queue cases without introducing that lifetime error.
- Windows pool workers install sleepy_robin: suspension calls Sleep(1), and
  notify does not wake that sleep. Other platforms use usleep(1) in that helper.
  Measure wake latency and timer context rather than assuming the General pool
  is suitable for same-frame work. A test-only comparison with the bundled stock
  scheduler may be useful; do not alter the viewer-wide scheduler in this step.

## Work sequence

1. Design a small bounded job/slot state machine over an injected nonblocking
   submitter, with an adapter for actual WorkQueue::tryPost. Jobs own requests,
   results and any shared slot lifetime; callbacks capture no live motion,
   character or frame-buffer pointer. No hidden additional viewer threads.
2. Include a generation in atomic claim state so a delayed old callback cannot
   claim reused storage. Define submission failure, queued cancellation,
   running abandonment, ready consumption, exception/failure and close behavior.
   Main-thread reuse is allowed only after exclusive ownership is proven. Keep
   samples invalid until fully published with release/acquire synchronization.
   Bound outstanding callbacks as well as payload slots: cancellation must not
   permit repeated resubmission while old callbacks remain queued/unacknowledged.
   Generation checks prevent stale access but do not alone bound queue growth.
3. When work is not ready at an application barrier, preserve deterministic
   same-frame serial behavior. Queued work can be claimed/cancelled safely;
   already running work must retain exclusive buffers and be discarded later.
   Bound duplicate/wasted work and memory rather than waiting without a deadline.
4. Add deterministic state-machine tests using explicit gates/manual delivery,
   including late/duplicate callbacks, saturation, contention/rejection, stale
   generations/identities, exception injection, close with queued/running jobs
   and destruction after the submitter/executor disappears. Use actual WorkQueue
   integration tests as well. No timing assumptions in correctness tests.
5. Add an explicit opt-in private-pool wake benchmark. Compare the current
   scheduler with the bundled stock round_robin on Windows, both with the
   process's default timer context and a scoped request matching the viewer's
   1 ms request. Measure idle worker CPU as well as latency.
6. Report actual worker width, CPU/compiler/flags, sample counts and median/tail
   distributions. Retain source and raw output. Do not interpret short zero CPU
   readings as proof of zero overhead. Plan 13 measures complete sampling costs,
   reusable memory/allocation bounds, saturation and discarded work next.
7. If that subsequent measurement demonstrates useful overlap and acceptable
   tail costs, write a separate
   opt-in viewer integration plan covering motion preparation order, subclass
   eligibility, live version counters, a same-frame apply barrier and profiling.
   Otherwise record the rejection and measure another boundary or scheduler
   candidate separately. Keep default playback unchanged throughout this step.

## Acceptance limits

No unbounded queue growth, stale application, data races over recycled storage,
main-thread queue-capacity wait or worker UI/GL/cache access. Complete correctness
and actual costs must precede runtime integration. Matched real-scene captures,
multi-viewer admission settings and visual/lifecycle acceptance remain separate
requirements before enabling significant threading by default.

## Results

Implemented as test-only sources. No playback caller, setting, production pool,
scheduler or viewer thread count changes.

### Protocol

`tests/bounded_sampling_executor.h` accepts an injected nonblocking submitter;
the actual WorkQueue adapter uses tryPost and rejects zero-worker, expired and
closed destinations. The adapter and queue lifetime operations belong to their
owner thread: LLInstanceTracker's weak handle detects an already destroyed
queue but cannot extend an explicitly destroyed C++ object's lifetime.

Each preallocated shared slot has one atomic generation/phase word. The owner
publishes a moved request with release ordering; one callback claims it with
acquire/release ordering and exclusively writes output. Only a published Ready
phase permits owner access. Result retrieval transfers ownership, then the owner
must independently validate the current request before applying anything. Slot
identity and generation prevent foreign tickets and delayed/duplicate callbacks
from claiming newer storage. Generation exhaustion retires a slot rather than
wrapping. Request and result moves are required to be nonthrowing.

Rejection restores the original input and its mask allocation. Queued cancellation
retains the slot until callback acknowledgement; repeated submissions cannot grow
the queue past slot capacity. Running cancellation abandons output, retaining its
buffers/lease until the worker clears them. A ready result can be discarded
immediately. Sampling exceptions publish Failed; retrieval clears partial data.
Closing cancels/discards owned jobs without waiting, closing a shared queue or
capturing the executor. Outstanding callbacks retain only their shared slot and
generation. The owner can immediately use an independently prepared serial
request for deterministic same-frame fallback, including while a worker is held
at an explicit gate. No worker reads a live motion, character or joint state.

Resident payload count and accepted, unacknowledged callbacks are bounded by
capacity. This is not a byte-budget API for arbitrary malformed caller vectors;
production admission must also enforce input shape/size and retained capacities.
The existing sampler caps valid jobs at 216 joints. Plan 13 accounts for actual
request/result and batch capacities, allocator costs and transient moves.

### Correctness validation

`avatar_bounded_sampling` passes **1,140 checks in each curve-storage mode**
using the actual sampler and serial JointMotion/LLJointState oracle. Deterministic
manual delivery and explicit worker gates cover capacity/full/rejected submission,
submission exceptions, synchronous delivery, consumed/stale/foreign tickets,
old generations, duplicate callbacks including competing worker delivery,
cancelled-callback accumulation (1,000 attempts), running abandonment, exact serial
fallback, ready cancellation, failed/malformed sampling, sampler exceptions,
close/destruction while queued/running, zero capacity and callback lifetime after
both executor and submitter disappear. Actual WorkQueue checks cover full/closed/
expired/zero-worker destinations; an actual one-worker ThreadPool drains accepted
sampling work and joins through its own close operation.

Two isolated negative variants fail at the intended assertions: prematurely
recycling a cancelled slot triggers the callback-growth check; letting an old
callback claim the current generation triggers the stale-generation check. The
repository contains the correct implementation only.

Native validation compiles fresh complete character/controller/keyframe sources,
links unchanged cached dependencies read only and creates no viewer. Both new
test translation units also pass independent syntax checks with map/Tracy off and
contiguous/Tracy on. MSVC 19.51.36244, x64 AVX2, `/O2 /fp:precise` for executed
tests. Full viewer linking, sanitizer execution, ARM/macOS and runtime acceptance
remain pending. The standalone checks do not rely on includes from the combined
native wrapper.

### Wake probe

The earlier scheduler workaround has a documented CPU rationale: commit
`e98091e29859ee6a784ee6920048837f2b4536c2` (December 2022) introduced sleepy_robin
because stock condition-variable waiting appeared too busy on Windows. Latency
alone is therefore insufficient to choose a replacement.

`avatar_queue_wake_benchmark --benchmark` creates private one-worker pools,
never the viewer's General pool. It requests 5 ms idle before each of 64 callbacks,
reports post-to-callback latency, and separately samples worker kernel+user CPU
over 2 seconds of requested idle. It repeats both schedulers with default timer
context and a scoped, paired timeBeginPeriod/timeEndPeriod request. The viewer's
Windows window thread also requests 1 ms, subject to timer capabilities. Default
process results cannot be projected directly onto an active viewer.

Two final runs on the Ryzen 5 3600 used MSVC 19.51.36244, x64 AVX2,
`/O2 /fp:precise`, with no concurrent compiler/validation process during callbacks.
Desktop load otherwise remained uncontrolled. Raw minimum/median/p95/p99/maximum
and idle CPU output is retained in `results/queue-wake-ryzen3600-run{1,2}.txt`.

| Private pool, timer request | Run 1 median / p95 (microseconds) | Run 2 median / p95 (microseconds) |
| --- | --- | --- |
| Current Windows scheduler, default context | 30.2 / 16,018.3 | 15,214.8 / 16,019.3 |
| Stock round_robin, default context | 16.4 / 31.7 | 17.1 / 35.9 |
| Current Windows scheduler, accepted 1 ms | 1,942.8 / 2,003.8 | 1,993.2 / 2,002.7 |
| Stock round_robin, accepted 1 ms | 12.0 / 16.4 | 13.0 / 26.1 |

Default-context medians vary strongly with polling/timer phase, while long tails
remain. The matching-timer context still has about 2 ms current-scheduler wake
cost in these idle cases. That can dominate small sampling jobs. Both final
runs reported zero idle worker CPU at GetThreadTimes' measurement resolution;
an earlier exploratory run recorded one 15.625 ms CPU quantum over 2 seconds for
the current scheduler (about 0.78% of one core). These short, quantized idle
measurements cannot rule out sustained CPU, coroutine/fairness or shutdown
regressions from changing schedulers. No viewer-wide change is justified here.
Plan 13 must measure useful sampling overlap with the existing scheduler first.

### Handoff

The focused LL_TESTS targets are `avatar_bounded_sampling` and
`avatar_queue_wake_benchmark`. The latter does not run as a build-time test;
invoke `--benchmark` explicitly. There is no timing pass/fail threshold. A new
installer is optional for this test-only change; existing playback is unchanged.
Plan 13 must establish full costs and useful overlap before any live integration.
