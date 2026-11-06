# 14s controller boundary audit

Parent PR120 is published at 55f66ec851701fdc6b9bfd2c81794abc5ee80ccf,
based on PR119. It remains immutable for its separate build.

The earlier batch cost fixture constructs synthetic current queries and applies
joint values directly. It does not execute complete current controller callbacks,
mask admission, fade/stop decisions, pose blending or hand publication. The current
motion fixture tests real application but below that controller boundary. Merely
changing the old pool policy would leave that evidence gap.

The new private target therefore generates a translation unit from the exact
current llmotioncontroller.cpp. It inserts a preparation call at updateMotions
entry and redirects only the existing invokeMotionUpdate callback return. The
generator requires each boundary exactly once and records both hashes. The
viewer continues compiling the unchanged original file; headers, class layout,
callback order and controller arithmetic are unchanged. Both baseline and
candidate include the same test trampoline. This is a private experiment, not
a production dispatcher or a measurement of uninstrumented viewer performance.

Preparation uses the actual controller's protected time/mask methods through
member pointers supplied at that generated boundary. Requests contain immutable
asset leases, current numeric/identity/usage values and owned observation data.
Workers receive the bounded executor's owned slot only. Controller/motion pointers
remain in the owner frame. All actual application occurs through
updateWithOwnedSampling at the controller's original callback, so its independent
current-context validation and direct fallback remain authoritative. Readiness is
checked once at first consumption; late work is cancelled without a frame wait.

The generated cohort has 1, 8 or 32 ordinary fixture characters, each with four
overlapping motions, 32 joints and 32 keys. Exact current mask admission can avoid
fully covered motions. The single pending batch is limited to 32 motions. Private
pools use actual production selection, fallback 3 overridden to width 1 and General
capacity 1,048,576. They never use the registered General queue. Busy capacity-1
executor and closed-queue controls exercise bounded rejection, without pretending
the real General queue is full. Synthetic owner gaps are labelled explicitly.

Correctness controls wait for a ready result only outside performance trials;
timed consumption never waits. Compare full controller/active motion order,
joint values and hand publication with direct control, including stale frame/
skeleton cases and busy/closed fallback. Existing current-context and bounded
executor suites remain the broader publication/cancellation/lifetime oracles.
Record capture/submission/gap/consume/application/fallback/cleanup and total owner
intervals, raw owner/worker cycles and a separate allocator instrument. Include
the cost of releasing per-frame owner bindings. No live avatar, quality change,
real deadline or FPS acceptance is implied by this generated dataset.
