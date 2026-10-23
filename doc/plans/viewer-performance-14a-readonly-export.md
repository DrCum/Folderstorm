# Checkpoint 14a: preserve live curve data during animation export

Plan written before implementation. Prerequisite discovered by checkpoint 14's
mutation audit; parent is the complete-cost sampling checkpoint.

## Problem

LLKeyframeMotion::serialize is const but binds mutable position keys and calls
quantize16 on each stored position. getFileSize calls that serializer with a
counting packer, so even a size query can alter shared live animation data.
Repeated sizing/export can also re-quantize already changed values. A const
shared ownership lease does not prevent this concurrent writer; generations
cannot repair a data race if workers read those same positions.

## Work sequence

1. Reproduce the mutation using the actual serializer and getFileSize on generated
   owned assets. Capture frozen first-serialization packet fixtures from the
   unmodified production method before editing it. Include fractional/clamped
   positions, exact quaternion cases, empty curves and constraint metadata where
   deterministic. Keep fixture data free of user assets.
2. Quantize a local position copy and pack from that copy. Make serialized joint,
   curve-entry and key views const so later in-place writes are rejected by the
   compiler. Keep the wire schema and first-serialization bytes unchanged.
3. Test source keys/metadata and actual sampled poses before/after direct export,
   sizing, repeated export and rejected writes. Verify first-packet byte identity
   and stable repeated output. Use both curve-storage modes and the real math,
   cache owners and data packer. A pre-fix negative run must detect mutation.
4. Confirm serialization can coexist with owned read-only sampling without any
   key mutation or live worker character/cache access. Use explicit start gates
   for lifecycle/concurrency checks, not timing as a correctness assertion.
5. Publish a separate PR, update the mutation audit and continue checkpoint 14's
   base-motion phase contract. No worker dispatch/default change is part of this
   prerequisite.

## Acceptance limits

First-serialization encoded bytes and schema stay compatible. Size queries and
serialization preserve the live source and pose. Preserve existing validation,
failure returns, main-thread metadata/constraint handling and cache ownership.
Do not promise full viewer export/preview acceptance from a native unit test.

## Results

Not implemented yet. The hidden position write is an explicit blocker to treating
published ordinary curve data as immutable while sampling.
