# Checkpoint 6: read-only animation sampling boundary

Plan written before implementation. Parent checkpoint: #94, skinning generation.

## Objective

Crowded scenes evaluate animation curves repeatedly for many avatars. Establish
an exact, read-only sampling boundary before changing curve storage or evaluating
samples on workers. Keep character state, constraints, hand-pose metadata and
final joint-state application under their existing main-thread ownership.

This checkpoint supplies a tested contract and source attribution. It makes no
FPS claim. A subsequent, separate checkpoint will compare node-based key maps
with contiguous storage if the ownership/iterator audit supports it. Worker
dispatch still needs immutable asset ownership, versioned results, dependency
barriers and measured task overhead.

## Source findings to verify

- `ScaleCurve`, `RotationCurve` and `PositionCurve` use `std::map` lookups and
  non-const sampling/interpolation methods.
- Their active lookup paths read keys and produce local values. After the
  explicit end/begin/exact-key branches, the interpolation branch cannot have
  `right == mKeys.end()`. It nevertheless contains code assigning the loop-in
  key into the map if that condition were true. Verify and remove this
  unreachable mutation while preserving the existing clamping behavior.
- `JointMotion::update` applies sampled values to live joint states.
  `applyKeyframes` also changes character hand-pose metadata, and constraints
  run afterward. Moving those whole methods to workers would change ownership.
- Curve/loop editing and deserialization can mutate data shared by motion
  instances. Const methods alone do not make concurrent edits safe.

## Work sequence

1. Audit every curve/key-map reader and writer, reference/iterator lifetime,
   motion-list ownership, loop editing, preview and asset replacement. Record
   exact main-thread application points and unresolved lifetime questions.
2. Add an extracted-production sampling harness, using the viewer's actual
   vector/quaternion math where available. Compare against frozen methods and
   independent expected boundary values for empty/single/multiple keys, exact
   keys, before/after range, step/linear/spline modes, loop parameters, repeated
   times, signed positions/scales and quaternion hemisphere cases. Preserve the
   existing numeric operation order and returned defaults.
3. Make sampling and interpolation methods const, and key references/iterators
   const. Remove the proven unreachable key-write branch with an upstream
   comparison comment. Retain public time/duration arguments and existing
   end-of-range/loop semantics. Change no curve container or motion timing in
   this checkpoint.
4. Confirm repeated sampling leaves keys and metadata unchanged. Compile-check
   affected native translation units and audit member-function pointer uses.
   Include a full-build focused native target so local linking can be verified.
5. Publish this checkpoint separately with animation/preview/posing acceptance
   instructions. Write the next implementation plan from the audit and tests.

## Contiguous-storage experiment after this checkpoint

Measure current map sampling against a compatible contiguous container using
identical curve methods and real interpolation math. Include short and long
curves, smooth playback, seeks, wraps and varied animation instances. Measure
loading/allocation behavior and memory as well as sampling. Inspect insertion
order, duplicate timestamp behavior and iterator invalidation before changing a
container. Do not reserve untrusted serialized key counts before validating the
available input. Keep any storage change in its own PR.

## Acceptance and follow-up

Curve results must match the frozen implementation exactly for the supported
finite inputs, and sampling must not mutate keys or metadata. Native linking,
live animation/loop preview, poses, gestures, sit/stand, avatar arrival/departure,
animesh and crowd frame-time acceptance remain local test items. Full scene
workers stay gated by the roadmap's ownership and performance requirements.

## Results

Initial source audit started. No sampling signatures, storage or scheduling
changes implemented yet.
