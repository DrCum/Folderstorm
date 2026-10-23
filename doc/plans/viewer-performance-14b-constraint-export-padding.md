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

Not implemented yet.
