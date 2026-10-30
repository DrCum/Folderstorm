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

Implemented a protected owner-thread phase API. The ordinary virtual `onUpdate`
resolves the same update/sample times, applies direct keyframes, then runs the
same constraints, last-time assignment and continuation test. It constructs no
sampling request, queue or result and does not poll for background work.

`prepareKeyframeSampling` requires a loaded canonical owner and current state
shape, copies actual state usage (null states remain no-ops), and retains the
asset lease without changing motion times, curves, poses or hand-pose data.
It rejects borrowed/uninitialized assets, malformed shape and nonfinite resolved
sample/duration values, clearing any old output request on failure.

`updateWithSample` is explicitly invoked base behavior. It resolves time once,
rebuilds actual current inputs using separately supplied current identity, and
validates the complete result before any pose write. A mismatch calls the
existing direct sampler. Accepted rows apply only their sampled channels, then
publish current main-thread hand pose/priority. Both paths share the original
constraint/last-time/continuation order. This does not establish subclass
eligibility or wire live version counters, prediction, queues or workers.

### Validation and limits

The oracle freezes actual pre-refactor `onUpdate` and `applyKeyframes` from
`dcbc51a058d931722a6db5b4109d859e6b0d6200`; only class/method names differ. Native
tests compare exact float/pose/state bytes and continuation behavior for direct
and prepared updates. Cases include negative/signed-zero times, zero duration,
loop boundaries/reversed/zero ranges, stopped loops, backwards trajectories,
metadata edits, missing/partial/unknown usage, body/ground constraints and masks
that suppress constraints. Preparation snapshots check unchanged live state.

Stale application covers all five independent identity fields, current time,
duration, usage, owner replacement, consumed result, borrowed/unloaded data,
null state, edited keys and changed loop/last-time inputs. Current hand-pose
priority ties/winners and weight metadata remain accepted and read at application.
Owned samples also run on a joined background thread in those tests.

Test-only factories exercise the actual controller's activation, normal update,
pause, forced update, hidden update, stop/ease, deactivation and reactivation,
including real blending and positional constraints. These fixtures run an empty
controller update before activation: the old constructor's uninitialized first
mask was exposed during testing and is tracked as separate checkpoint 14d.

Final optimized native runs pass **5,956 checks and 1,474 frozen-update
comparisons in each storage mode**, including 725 accepted sampling cases and
16 stale/current-input fallback cases (controller/empty cases are additional).
Complete keyframe source, walk/stand/fall callers and the standalone test pass
Tracy/contiguous syntax checks. Native tests use fresh character/controller/
keyframe sources and read cached dependencies only. Full viewer linking,
rendered multi-joint IK/hand morphs, subclass/lifecycle acceptance, sanitizers
and ARM/macOS execution remain pending. Test-only controller callbacks are
synchronous; they do not prove a useful production overlap window or FPS gain.
Rejection does not repair an already-invalid live motion: the explicit bridge
retains the ordinary `onUpdate` precondition that direct sampling is valid.

Build/test handoff: retain this exact head separately with the accepted private
x64 AVX2 OpenSim Nightly configuration and contiguous keys explicitly OFF for
the map control. Focused LL_TESTS target: `avatar_keyframe_phase`. Full viewer
builds and monitoring remain stopped until separately requested. Continue 14d's
initial mask fix before further context/worker integration.
