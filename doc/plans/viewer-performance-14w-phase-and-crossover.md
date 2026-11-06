# 14w: real owner phase and private cohort crossover

Written after14v's completed24-case comparison and before this work starts.
Start a separate branch from its published exact head. Preserve all prior heads,
raw observations and feature/build-cache boundaries.

The32-character event-row fixture saves31–33% owner cycles in both orders but
costs18% more combined cycles. Its one-character path is worse. A private fixture
can snapshot the whole cohort before the first controller body; that does not
establish the equivalent point or immutable asset lifetime in the viewer.

1. Trace the actual frame-to-avatar-to-controller call path and its owner-thread
   phase boundaries. Record when time, frame count, character permission, motion
   activation/deprecation, skeleton and asset revision can change. Identify the
   work between earliest safe capture and each real callback. Include special
   self, puppet, dummy, paused, hidden/impostor and throttled avatar paths.
   Produce a source-backed feasibility table before considering production code.
2. Check whether owned capture can happen early enough without changing callback
   order, keeping live controller/joint pointers in jobs, snapshotting assets
   while mutable, using an additional frame, or retaining bindings beyond a
   sound owner lifetime. Name concrete unresolved phase/lifetime requirements;
   a synthetic readiness improvement does not satisfy them.
3. If the existing private model remains informative, extend its bounded CLI to
   fixed cohorts2/4/8/16 using the existing direct, whole event batch and event-row
   modes. Keep32 as a same-program control if source changes are required. Freeze
   paired/reversed cases before timing, with no artificial gap. Retain complete
   owner/worker windows, tails, admission, useful/wasted work, resident capacity
   and separate allocations. Do not add a production admission threshold from
   a single host/cohort crossover.
4. Review the result against CPU efficiency and owner latency together. A useful
   owner gain with bounded extra CPU may justify a future explicitly scoped
   default-OFF pilot only after real phase/ownership evidence. Poor crossover or
   unsafe phase keeps worker dispatch private; select a different measured owner
   bottleneck rather than forcing threading.
5. Publish this as an independently reviewable checkpoint, then write the next
   plan. Keep Q4/Q7 and eventual rendered/crowd/lifecycle/General acceptance
   deferred while the user is working. Do not request live operations, new
   profiler variants or any change to build-monitor/source/cache ownership.
