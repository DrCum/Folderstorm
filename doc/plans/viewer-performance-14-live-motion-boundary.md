# Checkpoint 14: real motion-update ordering and overlap boundary

Plan written before implementation. Parent: complete-cost sampling measurement.

## Objective

Find whether the real viewer offers sufficient safe same-frame work between
owned query preparation and pose application. Plan 13 shows useful latency
overlap in synthetic cases, but immediate dispatch/consumption mostly duplicates
serial work. Preserve animation ordering, quality and stability when deciding
whether and where to integrate.

## Work sequence

1. Trace actual calls through LLVOAvatar/LLCharacter, LLMotionController and
   LLKeyframeMotion. Map timing/loop/activation preparation, keyframe sampling,
   hand-pose publication, constraints, blending, skeleton matrices, attachments
   and rendering. Identify the earliest authoritative query and latest legal
   application point, including cross-motion/avatar dependencies.
2. Audit ordinary keyframes versus walk/stand/fall, editing/posing, preview and
   other subclasses/overrides. Define explicit eligibility and serial fallback;
   do not assume a base-class pointer means its onUpdate behavior can be split.
   Honor adaptive updates, paused motions, priorities, RLV and usage masks.
3. Audit instance/activation/frame/skeleton/edit generations and canonical asset
   ownership at that boundary. Define main-thread invalidation for deactivation,
   removal, cache replacement, edits, skeleton/outfit changes, avatar destruction
   and shutdown. Workers may retain only owned read-only sampling payloads.
   Verify immutability or copy-on-write for every curve field actually read;
   a generation check cannot repair a concurrent write to sampled storage.
   Audit floating-point environment/denormal settings too: a shared worker's
   prior jobs must not silently change math behavior relative to the main thread.
4. Distinguish source-proven independence from measured available time. Use the
   existing profiling checkpoint and focused markers if they resolve a concrete
   ambiguity; do not invent a 2 ms viewer window from the synthetic benchmark.
   Keep matched crowd captures and the deferred user questions visible.
5. If a useful boundary is plausible and source dependencies can be preserved,
   design/implement the smallest separately testable phase contract, initially
   serial or shadow-only, before an off-by-default playback experiment. Test
   actual controller/lifecycle behavior, current-version validation, ordered
   application and exact direct fallback at the application barrier.
6. If the boundary cannot overlap useful work safely, document the rejection and
   choose the next evidence-based experiment. Candidates include a narrowly
   scoped private-pool wake comparison or a different data-only batch; avoid a
   viewer-wide scheduler change based on short idle samples.
7. Publish a separate PR for any runtime/profiling checkpoint and write the next
   implementation plan. Keep the frozen feature baseline and independently
   testable performance stack intact.

## Acceptance limits

No extra animation frame of latency, worker access to live scene/state/UI/GL,
main-thread capacity wait, uncontrolled queue/memory growth, unsafe cancellation
or application from stale generations. No broad dispatch/default scheduler
change without matched real-viewer performance and visual/lifecycle evidence.

## Results

Not implemented yet. Source inspection and eligibility/dependency mapping come
before any live dispatch change.
