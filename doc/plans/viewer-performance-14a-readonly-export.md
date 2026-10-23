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

Implemented: serialization now quantizes a local LLVector3 and encodes that copy.
Serialized joint/curve-entry/key views are const, as are constraint metadata
views. The normal sampling path and ownership model are unchanged. No worker
dispatch or default threading change is introduced.

### Reproduction and compatibility

Before editing production code, the native baseline emitted five packet fixtures
from the actual serializer at `556481e3fa14556d65ee911ebdfff1bdca4a818b`, with no
sizing call before each first serialization. Generated assets cover empty data,
empty curves, fractional/clipped positions, exact quaternion cases, zero duration
and body-constraint metadata. Constraint names use all 15 characters plus NUL so
uninitialized padding in the old implementation cannot pollute the frozen byte
oracle. No user assets are included. Fixture capture checks that the old source
mutation still exists before emitting; do not regenerate these against the fix.

The baseline independently reported a getFileSize source mutation and failed the
new source-preservation regression check. The fixed method matches all five
frozen first packets byte for byte in both curve-storage modes. Repeated sizing
and export now produce those same packets without changing source data.

Compatibility means the wire schema and first-serialization bytes are preserved.
The old repeated-call sequence could re-quantize already changed source values,
so preserving that incidental drift is not an objective. Live positions now keep
their pre-export values instead of being changed by a size query or export.

### Validation

`avatar_readonly_animation_serialization` passes **896 checks and 10,000 concurrent
owned samples in each storage mode**. Checks cover exact frozen bytes, live
keys/counts/modes/loop anchors and scalar metadata, actual JointMotion pose bytes
before/after export, getFileSize, repeated operations, a deliberately rejected
pack operation and canonical cache owner identity. The worker owns its request
and expected pose data, samples through the real pure sampler, and never calls
the character/cache/serializer APIs. Main-thread export runs alongside it after
an explicit start gate. Source immutability is established by the code change
and before/after checks; this stress test is not a sanitizer/race-detector result.

Native execution compiles fresh complete character/controller/keyframe sources
and links cached dependencies read only. Both the complete keyframe source and
the test pass independent syntax checks with map/Tracy off and contiguous/Tracy
on. MSVC 19.51.36244, x64 AVX2, `/O2 /fp:precise`, assertions enabled. The shared
test-character helper is now derivable so this fixture can provide a collision
volume without an avatar/renderer. Full viewer linking, preview/export workflows,
sanitizers and ARM/macOS execution remain pending.

### Build handoff and continuation

This changes real export/size behavior, so retain it as a separate runtime
checkpoint. Use normal x64 ReleaseFS_open_AVX2/Nightly/NSIS defaults, the exact PR
head and explicit contiguous keys OFF for the map control. With LL_TESTS, the
focused target is `avatar_readonly_animation_serialization`; the alternative
storage mode is checked separately. Do not add newer feature heads implicitly
or overwrite prior installers/packages. No shared-cache build was run here.

In-world acceptance: export or size a loaded animation repeatedly, verify its
played/previewed pose stays stable, and re-open the exported packet to check the
expected quantized pose and constraints. This unit test does not replace those
workflow checks.

Continue checkpoint 14's base-motion prepare/apply contract with this prerequisite
included. The export audit also identified short constraint-name padding as a
separate follow-up; its packet determinism is outside this curve-immutability fix.
