# Checkpoint 14m: avoid preparing masked animation queries

Plan written before implementation, after declining the 14l hint cache. Start
from its committed decision on a new performance branch. Runtime pose storage
matches PR 114; preserve the rejected experiments and their source identities.

## Evidence and objective

The real-controller 128-joint/eight-motion saturated fixture prepares eight
shadow queries, but only one ordinary callback executes. Seven queries are
unused. Partial overlap and disjoint groups execute all eight callbacks. Avoid
provably unneeded preparation where the controller's existing mask semantics
support it, with an explicit audit for any advisory negative prediction.

This improves preparation/diagnostic efficiency and future worker admission.
Normal playback remains serial, and the nonpersistent diagnostic stays FALSE
by default. No viewer FPS or useful real worker window follows from these
generated cases.

## Work sequence

1. Freeze the current eight-prediction/one-callback baseline. Record preparation
   allocation/buffer/key counts separately from timing. Inspect actual signature
   resets, additive-before-normal order, previous pose weights, signature stride
   boundaries, loading/quantum/pause/LOD and stop behavior before writing a model.
   `updateMotions` resets signatures before additive and again before normal;
   `LLKeyframeMotion::getBlendType()` is NORMAL. These facts alone do not prove
   that earlier callbacks cannot mutate later state.
2. Design an owner-only bounded mask-admission model. Read/copy state without
   modifying the real controller masks or adding virtual calls on unknown
   motion types. Preserve controller callback order, arguments, returns and
   every pose application. Be conservative at uncertain/reentrant boundaries.
   Any advisory prune must be distinguishable from a confirmed callback skip:
   keep bounded instance records and count a pruned motion that actually calls
   back. Actual callbacks always retain direct playback in the same frame.
3. Keep metadata inside the existing bounded scope, account for its bytes, and
   retain zero disabled-path allocations. Do not retain live scene pointers in
   jobs or change factory/role permissions, numeric policy, queueing or schedules.
   Perform prediction arithmetic inside the existing numeric-preservation scope.
4. Validate with the real controller. Cover saturated/partial/disjoint inputs,
   priority ties, signature unions/strides, scale-only signatures, unknown/custom
   getters and callbacks, mutation/reentry, stop/loading, pause/force/quantum,
   destruction and cap/failure cleanup. Track actual callbacks and compare all
   frozen poses/masks/order in both storage modes. Reduced query counts must not
   be mislabeled as missing gameplay work. Exercise and report advisory misses.
5. Measure operation/allocation/storage reductions with separate probes. Take
   short matched timing controls only when native builders are idle, retaining
   wall/cycle samples and drift. If the controls vary, report counts and keep
   timing inconclusive. Generated gaps still do not prove real useful overlap.
6. Publish a separate PR only after the implementation and focused checks are
   complete. Preserve default FALSE/nonpersistent behavior, update aggregate log
   field interpretation and manual capture instructions, then write the next
   plan. The independent builder owns full viewer builds and private retention.

## Deferred acceptance

Real crowded-scene captures (question Q7), rendered/lifecycle acceptance and
other-platform execution remain open. No account/setting operation, production
worker dispatch or frame-order change is authorized by this plan.

## Results

Baseline collected before implementation on `codex/perf-shadow-mask-admission`,
from final 14l decision `ef49e4614faa730d5803edc6dcd4a37f599cca52`. Runtime
pose source/header match PR 114, without the rejected hint macro/storage.

The separate allocation probe records 12 rows: 128 joints, one/eight motions,
partial/disjoint/saturated inputs, diagnostics OFF/ON. Every row requires exact
pre/post poses, zero counted retained bytes and zero owned binding capacity at
exit. All disabled rows allocate zero. Enabled eight-motion baselines:

| Workload | Predicted | Matched | Unused | New calls | Requested/buffer peak | Curve keys retained |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Partial overlap | 8 | 8 | 0 | 17 | 25,392 bytes | 12,288 |
| Disjoint groups | 8 | 8 | 0 | 17 | 7,472 bytes | 1,536 |
| Saturated overlap | 8 | 1 | 7 | 17 | 25,392 bytes | 12,288 |

Raw rows, the scratch-only probe source and complete source/executable identity
are in `results/shadow-mask-preparation-*14m*`. This is allocation/operation
evidence, not a timing or FPS result. The probe reuses the recorded scene via
a renamed benchmark entry point, compiles pose/core fresh and links immutable
internal libraries. It must not be linked into a viewer or used for timing.

Initial source audit confirms signature reset before additive and again before
normal; the culling branch reads previous pose weight and the two position/
rotation signatures before LOD/stop handling. A weight below one bypasses that
signature-union branch. Keyframe blend type is NORMAL. Continue auditing
uncertain callbacks and advisory observation before implementing admission.
No prediction, playback or worker behavior has been changed yet.
