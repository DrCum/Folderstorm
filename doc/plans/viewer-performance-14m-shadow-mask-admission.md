# Checkpoint 14m: avoid preparing masked animation queries

Plan written before implementation, after declining the 14l hint cache. Start
from its committed decision on a new performance branch. Runtime pose storage
matches PR 114; preserve the rejected experiments and their source identities.

## Evidence and objective

The real-controller 128-joint/eight-motion saturated fixture prepares eight
shadow queries, but only one ordinary callback executes. Seven queries are
unused. Partial overlap and disjoint groups execute all eight callbacks. Avoid
predicted unused preparation using the controller's existing mask semantics,
with an explicit audit for any advisory negative prediction.

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

The source audit confirms signature reset before additive and again before
normal; the culling branch reads previous pose weight and the two position/
rotation signatures before LOD/stop handling. A weight below one bypasses that
signature-union branch. Keyframe blend type is NORMAL. The implementation
models only eligible ordinary motions, reads the base pose/signature fields
without extra virtual getters and clears its local union at excluded motions.
Nonfinite weights conservatively keep preparation. It preserves the existing
scale-only signature behavior rather than repairing it in this checkpoint.

Predictions are advisory: an earlier normal or additive callback can mutate a
later motion's weight, including via reentry. At most 32 negative predictions
retain only nonzero instance tokens. A later actual callback always follows
the original direct path and records an advisory miss once per token. Beyond
the token cap, the original bounded preparation path remains available. The
local union/tokens live only in the existing scope allocation and count toward
its byte cap; controller/pose layout does not grow. Cancellation, destruction
and preparation failure close the accounting and release admitted query data.

At scope completion, `predicted = matched + mismatched + unused` and
`masked = masked_observed + masked_unobserved`. Observed negative estimates also
contribute to `unpredicted`, which can count repeated callbacks. Unobserved
estimates include cancelled/destroyed scopes and are not confirmed masking.
See plan 14g's amended capture interpretation. No callback, pose application,
factory/role permission, worker dispatch or diagnostic default is changed.

### Allocation and correctness evidence

The final model scans signature words with `memcpy`, avoiding aliasing/alignment
assumptions and matching the controller's stride boundary. Both key-storage
modes reproduce the same frozen 216 controller frames and 252 multi-motion
frames. Each mode passes 864 signature-oracle/admission/advisory/lifetime checks,
5,105 existing shadow checks, 4,054 controller checks and 38,971 multi-motion
checks. Separate probes pass 27 existing and 26 post-prune allocation-failure/
cleanup checks per mode. The unsupported-context branch passes 14 direct/
accounting checks. The normal caller and Tracy/contiguous caller/core syntax
checks pass; these isolated native checks do not replace a full viewer build.

The new fixture exercises opaque normal/additive callbacks that change later
weights, callback reentry, a manual post-preparation mutation, the negative
record cap, nested cancellation, destruction without an asset lease and an
allocation failure after negative records exist. Actual direct poses and
playback state remain exact, including when every advisory estimate misses.

The final 12-row allocation probe retains the same scene/exit assertions as
the baseline. All diagnostic-OFF rows still make zero counted allocations.
Enabled eight-motion results are:

| Workload | Prepared | Matched | Unused | Advisory masked | New calls | Requested/buffer peak | Curve keys retained |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Partial overlap | 8 | 8 | 0 | 0 | 17 | 26,088 bytes | 12,288 |
| Disjoint groups | 8 | 8 | 0 | 0 | 17 | 8,168 bytes | 1,536 |
| Saturated overlap | 1 | 1 | 0 | 7 | 3 | 8,168 bytes | 1,536 |

The saturated case removes seven prepared queries, 14 allocation calls and
17,224 requested bytes. The tradeoff is 696 additional bytes per enabled scope
when preparation is otherwise unchanged. Every row ends with zero owned binding
capacity and counted live-byte delta. These are ordinary-new counts, excluding
custom aligned allocation/RSS, not heap-wide memory or viewer FPS measurements.

### CPU cost and limits

Two separate P,C,C,P case comparisons retain 560 rows / 560,000 updates with
raw wall nanoseconds and thread cycles. The first byte-scan candidate and its
source/executable identity are retained in `shadow-mask-byte-candidate14m.json`;
its noisy controls and increased enabled cost motivated the final word scan.
No allocation replacement functions are linked into either timing executable.

Final 128-joint/eight-motion medians, combining the two repeats per version:

| Workload | Diagnostic | Parent ns/update | Final ns/update | Change |
| --- | --- | ---: | ---: | ---: |
| Partial overlap | OFF | 118,833.40 | 119,442.40 | +0.51% |
| Disjoint groups | OFF | 17,048.50 | 16,967.45 | -0.48% |
| Saturated overlap | OFF | 15,250.90 | 15,383.00 | +0.87% |
| Partial overlap | ON | 136,059.55 | 133,215.00 | -2.09% |
| Disjoint groups | ON | 22,336.55 | 23,213.95 | +3.93% |
| Saturated overlap | ON | 30,549.20 | 18,497.35 | -39.45% |

Raw thread-cycle changes for the enabled disjoint/saturated cases are +3.64% /
-39.36%; parent cycle drift for these 128-joint cases is at most 2.69%. The
small partial-overlap difference is not strong improvement evidence. The mask
scan still costs CPU when no preparation can be removed; retain that regression
alongside the allocation reduction. At three joints, unchanged-parent cycle
drift reaches 55.06%, so that small-case result is inconclusive. Do not average
these rows into an FPS claim or reinterpret raw cycles as elapsed time.

The recorder checks private worker state and the full host native builder/
packager process inventory before/after each case. It does not establish an
idle desktop or identify unrelated CPU activity. No priorities, affinities,
other processes, viewer settings or logged-in sessions were changed. Generated
fixed-time curves and exact poses do not close real crowd/window/visual gates.

### Separate build and next step

Build this PR's exact head independently with the accepted private normal
configuration, preserving the preceding retained versions. It includes the
frozen feature stack through PR 88 and the validated production changes through
PR 114. The intervening 14k/14l experiments are declined; final pose files still
match PR 114. PR base `codex/perf-blender-pool-hints` is the committed 14l restore
decision, not an enabled cache. Do not merge current feature work or enable extra
profiling/storage variants automatically. Full viewer build, rendered/lifecycle
acceptance and Q7's deliberate aggregate capture remain pending for this head.

The next independent default-playback opportunity is the long key temporary in
`LLHandMotion::onUpdate` cleanup. Plan 14n measures that actual consumer before
changing its map cleanup, keeping generic animation-data authority intact.
