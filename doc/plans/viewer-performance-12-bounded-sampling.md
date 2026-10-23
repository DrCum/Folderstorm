# Checkpoint 12: bounded sampling execution and complete-cost measurement

Plan written before implementation. Parent: owned sampling request/result contract.

## Objective

Prototype bounded execution over the owned sampler without connecting it to live
playback yet. Prove exclusive buffer ownership, nonblocking admission, safe
cancellation/late completion and shutdown. Measure preparation, queue/wake time,
sampling, validation, application and fallback against direct serial updates.
Thread count or kernel-only speed is insufficient evidence for integration.

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
3. When work is not ready at an application barrier, preserve deterministic
   same-frame serial behavior. Queued work can be claimed/cancelled safely;
   already running work must retain exclusive buffers and be discarded later.
   Bound duplicate/wasted work and memory rather than waiting without a deadline.
4. Add deterministic state-machine tests using explicit gates/manual delivery,
   including late/duplicate callbacks, saturation, contention/rejection, stale
   generations/identities, exception injection, close with queued/running jobs
   and destruction after the submitter/executor disappears. Use actual WorkQueue
   integration tests as well. No timing assumptions in correctness tests.
5. Add an explicit opt-in native benchmark. Compare direct JointMotion updates,
   serial request/result round-trip and one/two-worker batches. Reuse buffers,
   vary avatars/motions/joints/key lengths, forward/seek patterns and batch sizes,
   and check exact final values outside timing. Include cold/warm wake cases,
   saturation/fallback and background CPU contention where practical.
6. Report actual worker width, CPU/compiler/flags, sample counts and full median/
   tail distributions, stage timings, allocation/memory bounds and discarded
   work. Account for the queue's Windows scheduling behavior and uncontrolled
   desktop load. No whole-viewer/FPS claim from this prototype.
7. If useful overlap and acceptable tail costs are demonstrated, write a separate
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

Not implemented yet. The source audit above identifies admission and worker wake
behavior that the prototype must measure, not assume away.
