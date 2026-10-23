# Checkpoint 14c: base-keyframe prepare/apply contract

Plan written before implementation. Parent: export immutability and deterministic
constraint-name prerequisites. This is part of checkpoint 14's real motion audit.

## Objective

Define a small protected phase contract that can prepare an owned sample without
advancing a live motion, then consume a provided result at the original keyframe
application point. Keep normal production playback serial. Preserve controller
ordering and all main-thread hand-pose, constraint and continuation effects.

## Work sequence

1. Extract the existing base-motion time resolution without changing arithmetic
   or branches. Return normalized update time and looped sample time separately:
   a zero-duration loop also resets the last-update time to zero. Preserve
   negative/signed-zero, loop boundary, stopped, degenerate range and metadata
   edit behavior. Compare with a frozen pre-refactor actual onUpdate oracle.
2. Add an owner-thread-only protected request builder over the canonical asset
   lease and actual joint-state usage. Preparation must not change motion times,
   character state or pose. Reject borrowed, uninitialized or malformed shapes
   without dispatch. No new production queue, setting or worker is introduced.
3. Add an explicitly invoked protected base-update bridge for a provided result
   and independently prepared current identity. Rebuild current inputs at the
   actual application point, validate before any write, and fall back to the
   original direct sampling on a miss. Do not resolve stopped-loop time a second
   time after mLastLoopedTime has already advanced.
4. Share hand-pose publication between direct and accepted-result paths, reading
   current main-thread metadata. Constraints, last-update state and return value
   stay in their original order after pose application. The ordinary onUpdate
   path remains serial and does not construct requests or poll result state.
5. Test actual motion state/pose bytes against the frozen legacy update for time
   trajectories and edits; test stale/invalid/borrowed data and direct fallback.
   Exercise the bridge through the real controller with test-only factories and
   a deterministic frame clock, including pause/hidden/stop/reactivation where
   practical. Use real classes, not a reimplementation of the controller.
6. Keep subclass eligibility explicit: walk adjusts time, stand/fall postprocess
   poses, and poser uses borrowed data/custom behavior. A protected base contract
   does not authorize a controller to reroute arbitrary virtual onUpdate calls.
7. Document remaining live instance/activation/frame/skeleton/edit generation
   wiring and floating-point context requirements. The provided identity is not
   itself proof that current versions were obtained correctly. No live dispatch
   until those contracts and a measured real overlap window are established.
8. Publish the phase-contract checkpoint separately and write the next plan for
   eligibility/current-context prediction or worker-context handling based on the
   audited call graph. Keep the feature baseline and shared build cache intact.

## Acceptance limits

No extra animation frame, changed motion/constraint/blend order, mutation during
request preparation, worker access to live state, or partial/stale application.
No default threading or scheduler enablement. Native fixtures cannot replace
full viewer/visual/lifecycle and matched crowd acceptance.

## Results

Not implemented yet.
