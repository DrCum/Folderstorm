# Host palette v1

`host/palette.json` is a UTF-8 JSON object, at most4096 bytes, with exactly
`format_version: 1` and `colors`. The latter defines all seven roles:
`background`, `panel`, `input`, `text`, `muted`, `accent`, `border`.
Each value is `#RRGGBB`; hexadecimal case is accepted. Unknown/missing roles,
extra properties, duplicate keys, unsupported versions and alpha are rejected.
Names and roles are case-sensitive.

All seven roles are required because a partial dark palette over the host's
Daylight settings could mix incompatible background and text defaults. RGB is
opaque because the production host uses GDI solid brushes. Authoring input can
use the shared hex/RGBA parser, but must have opacity1 before normalization to
RGB. Equivalent RGB input is a no-op. A caller must preserve imported bytes
until an actual edit; parsing alone is not a rewrite.

Midnight/Daylight defaults match `tools/session-host/theme.h`. Native high
contrast and the user's explicit high-contrast preference retain precedence.
Custom selection will be host-specific, capture an immutable library revision,
and preserve the normal built-in choice for fallback. Viewer-profile selection
is independent. Copying viewer roles to host roles will be an explicit snapshot,
with no inferred links in a compiled import.

This checkpoint defines the managed data contract only. Both package validators
still reject host-palette activation. Shared managed/native cases, native format
capability negotiation, host project/history compatibility, authoring UI,
selection/rollback and the host consumer must land before enabling that payload.
An older editor must open projects containing unsupported host edits/history
read-only, preserving their original bytes. A matching native build is required
before retaining an enabled host candidate.
