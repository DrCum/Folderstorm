# Host preferences with custom palettes

The normal host stores its palette reference in the existing atomic preferences
record. It does not add a separate selection file or write viewer profile settings.

Built-in-only preferences retain the original `FHP1` header and four existing
length-prefixed records, byte for byte. A custom current or previous reference
uses `FHP2`, with the same four records plus a fifth selection record. The existing
host-experience theme indexes keep their meanings: 0 Midnight, 1 Daylight,
2 Follow Windows. They remain the fallback choice.

The fifth record is bounded to 205 bytes: `FHT1`, one flags byte, and optional
current/previous identities in that order. Flag bits 0/1 indicate their presence.
Each identity is exactly a 36-byte canonical lowercase GUID plus a 64-byte
lowercase hexadecimal revision. Other flags, lengths, versions or spellings fail.
The combined preferences limit remains 256 KiB. Decoders stage all parts before
replacing their output, and reject trailing or truncated bytes.

New hosts read FHP1 with no custom selection. Older hosts reject FHP2; their
existing invalid-file/backup path retains unsupported bytes. This does not claim
older hosts can activate a new custom selection. Reset keeps the previous
reference, so rollback data remains represented by FHP2.

On normal startup the host captures the selected immutable package from the
actual user's known roaming library against the matching native base. A missing
or invalid package keeps both saved identities and uses the existing built-in
theme with a notice. Unrelated Preferences changes do not discard that reference.

Appearance offers Custom host palette, Restore previous palette and Theme lab.
Choosing a palette prepares an in-memory preview. Apply validates the new package
and includes its reference in the same preferences transaction; Cancel clears
only the visual draft. The existing debounced autosave and atomic replacement
apply to the full record. Reset/rollback change only the host selection and
preserve existing account, layout, sound and built-in choices.

Library discovery reads bounded headers for labels, then validates the complete
package before a new selection can apply. Captured colors remain fixed until an
explicit new selection or restart. Native high contrast retains precedence.

The host and viewer launch the optional self-contained editor from the packaged
theme-editor directory, with retained development layouts as fallbacks. Missing
editor files produce a visible message. Windows argument quoting preserves base,
viewer and host paths; `--library` opens the companion library view when requested.

Portable serialization/state checks and compilation are separate from normal
host Preferences interaction, full native packaging and desktop acceptance.
