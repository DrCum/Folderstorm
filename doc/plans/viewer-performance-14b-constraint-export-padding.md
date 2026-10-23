# Checkpoint 14b: deterministic animation constraint-name fields

Plan written before implementation. Follow-up from the export audit; independent
of curve ownership and live sampling dispatch.

## Problem

Animation serialization packs fixed 16-byte source/target constraint-name arrays.
snprintf terminates short names but leaves the remaining local-array bytes
uninitialized, and packBinaryDataFixed writes all 16 bytes. Ground targets have
the same issue after writing "GROUND". The curve-immutability fixture deliberately
uses 15-character names so its old-byte oracle does not depend on stack padding.

## Work sequence

1. Zero-initialize both fixed arrays before formatting their names. Preserve
   existing truncation, NUL termination, field width and source/body/ground
   selection. Do not change the wire schema or read-only position encoding.
2. Add focused serializer checks for short, exact-width and overlong names, body
   and ground targets, repeated calls and failed writes. Verify every byte after
   the terminator is zero, and compare decoded names/metadata with the intended
   fields rather than trying to preserve indeterminate old padding.
3. Keep checkpoint 14a's frozen full-width packets and source/pose preservation
   tests green in both storage modes. Exercise actual data packing and standalone
   source checks; no full viewer build or account operation here.
4. Publish the format-hardening fix separately, record export/re-open acceptance
   for the building/testing agent, then resume checkpoint 14's motion phase
   contract without a live worker or scheduler change.

## Acceptance limits

Only previously indeterminate trailing bytes become deterministic zero. Intended
names, truncation, wire widths and decoded constraint semantics remain the same.
All existing metadata and curve data remain unchanged in memory. Full preview/
export acceptance remains a separately recorded requirement.

## Results

Implemented by value-initializing the two existing 16-byte arrays. Formatting,
truncation, field order/width and the GROUND branch remain unchanged. No motion
update, worker, settings or scheduler behavior changes.

### Reproduction and validation

The actual pre-fix serializer fails the new test with nonzero trailing bytes.
The fix passes **11,270 padding/decoding/layout/failure checks in each storage
mode**. The fixture covers empty, one-character, short, 15-character,
16-character and overlong names, with independently selected source and body
target names, ground targets, rejected writes and repeated serialization.
Successful name fields are unpacked from the actual binary buffer and checked
byte for byte, including zero tail, decoded/truncated string and field offsets.
Failure returns and live curve/scalar state are preserved.

Checkpoint 14a's **896 export/size/pose checks plus 10,000 concurrent owned samples
also pass in each storage mode**. Its five frozen full-width packets remain
byte-identical. Previously indeterminate bytes for shorter names become zero;
there is no attempt to preserve those old stack-dependent bytes.

Native execution uses fresh complete character/controller/keyframe sources and
cached dependencies read only, MSVC 19.51.36244, x64 AVX2, `/O2 /fp:precise`,
assertions enabled. The complete keyframe source and new test pass independent
Tracy/contiguous syntax checks. The shared serializer fixture accepts LLCharacter
so this test can provide named collision volumes without an avatar or renderer.
Full viewer linking, export/re-open workflows, sanitizers and ARM/macOS remain
pending. No shared-cache configure/build or in-world run was performed.

### Build handoff and continuation

Retain a separately testable exact-head build using x64 ReleaseFS_open_AVX2,
Nightly and NSIS defaults, with contiguous keys explicitly OFF for the map
control. The focused LL_TESTS target is `avatar_animation_constraint_names`;
retain the preceding serialization target as a regression check. Do not add
newer feature heads implicitly or overwrite previous packages/installers.

Workflow acceptance: export/re-open an animation with ordinary short collision
volume names and a ground constraint. Intended names and constraint behavior
should remain unchanged; repeated binary output should have deterministic name
padding. Then continue checkpoint 14c's base-motion prepare/apply contract, with
no live dispatcher or default worker change yet.
