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
   Add one avatar-category scope around `applyKeyframes` for attribution without
   generating a profiler event per key or joint.
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

Sampling/interpolation methods are now const, with const iterators and key
references. The unreachable inner end branch is preserved as an upstream
comparison comment. Time/duration signatures, lookup ordering, interpolation
math, defaults and clamping behavior are retained. One avatar profiling scope
covers `applyKeyframes`; state application and constraints retain their existing
execution order and ownership. No storage or scheduling changes are included.

`tests/test_curve_sampling.py` extracts the six actual production methods and
generates a native harness linked to the viewer's vector/quaternion math. Both
the unchanged baseline and the const implementation pass **56,232** byte-exact
comparisons against frozen lookup/interpolation behavior. Independent expected
values cover empty defaults, clamping, exact keys, step/linear/spline midpoint
results and identity rotations. Seeded curves add single/multiple keys,
neighboring float queries, duplicate timestamps, signed vectors, quaternion
hemisphere changes and edited loop metadata across four duration arguments.
Serialized field comparisons confirm key/metadata state remains unchanged;
compile-time assertions require const samplers and const interpolation inputs.

With `LL_TESTS` and Python available, build `avatar_curve_sampling` to regenerate,
link and run the focused native regression target. Local validation uses MSVC
19.51.36244, `/O2 /arch:AVX2 /fp:precise` and existing read-only cached math/common
libraries. Full viewer linking and live scene tests remain the building agent's
handoff. This contract checkpoint makes no timing or FPS claim.

The complete `llkeyframemotion.cpp` translation unit passes native syntax checks
with Tracy disabled and enabled. The isolated `fsposingmotion.cpp` check exposed
an existing missing `<chrono>` dependency in `fsjointpose.h`; supply that header
to the standalone check rather than folding an unrelated pose-header edit into
this checkpoint. With `<chrono>` supplied, the complete pose caller also passes
both Tracy configurations. These checks do not replace full viewer/PCH build
acceptance.

- External curve sampling also occurs in `FSPosingMotion`, including loading
  position/rotation/scale values for poses. Include those native callers and
  pose-loading behavior in compilation and local acceptance.
- `JointMotionList` and `LLKeyframeDataCache::sKeyframeDataMap` use raw pointers.
  A future worker cannot extend a curve's lifetime merely by holding a const
  pointer. An owned immutable snapshot or a proven ownership upgrade is required.
- `setLoopIn`, `setLoopOut`, ease/loop setters and hand-pose changes mutate shared
  motion-list metadata. Freeze query inputs and version edit/replacement events
  before any concurrent sampling experiment.
- Existing `onUpdate` computes loop time, applies keyframes, then constraints;
  `applyKeyframes` writes hand-pose metadata. Those application points remain
  on the main thread. No explicit curve member-function-pointer uses were found
  by the initial source search.
- Deserialization is the only production key-map writer found: rotation and
  position entries are assigned by timestamp, so duplicates replace earlier
  values. Serialization reads entries in timestamp order; no key iterator is
  retained between loading, sampling or serialization calls in the audited
  callers. `mNumKeys` can exceed unique timestamps and retains its existing
  diagnostic behavior. No untrusted-count reservation was introduced.
- Cache removal/clear deletes raw motion-list pointers. Upload validation,
  animation preview/reload and shutdown are important future worker lifetime
  boundaries. Preview/loop/hand-pose edits also require result invalidation.

## Local acceptance for this checkpoint

Compare the exact checkpoint head against #94's current head with identical
settings. Exercise ordinary animations, looping/non-looping previews and changed
loop points, gesture playback, poses including position/rotation/scale loading,
sit/stand and animesh. Check crowd arrival/departure and preview close/reload for
crashes or pose changes. For Tracy captures compare `applyKeyframes` alongside
the earlier avatar scopes, with the same profiling configuration on both sides.
Do not treat profiler overhead or this const change as an FPS improvement.

The next plan is `viewer-performance-07-curve-storage.md`. Contiguous-storage
experiments precede any production container change or worker dispatch.
