# Host selection foundation

The session host's custom palette selection is independent of viewer-profile
selection and account settings. It captures a theme ID and a full immutable
revision hash, rather than a theme name or a mutable latest-version pointer.

`fs_host_theme::Selection` uses a strict JSON object, bounded to4096 bytes:

```json
{"format_version":1,"current":null,"previous":null}
```

Each non-null choice has exactly `theme_id` and `revision`. IDs use canonical
lowercase GUID spelling, and revisions use64 lowercase hexadecimal characters.
Unknown fields, duplicate keys, missing fields and unsupported versions fail.
`null` means the host's existing built-in theme choice. Selecting the same choice
preserves the previous slot. Reset retains the previous custom choice; rollback
swaps the two slots, including built-in fallback.

`capture` resolves the selected ID/revision inside the supplied theme library,
rejects links within that boundary and validates the complete package against the
matching installed base. The package must contain a valid host palette and its
required capability. It verifies the exact consumed palette bytes again before
returning all seven opaque colors. Failed capture never changes a selection.

The GDI adapter converts packed RGB explicitly to `COLORREF`. `Theme::set` keeps
system and host-preference high contrast above custom colors, and restores the
existing built-in choice when no custom palette is supplied.

This checkpoint contains pure selection transitions and the package/GDI consumer.
The normal host startup, atomic selection-file persistence, Preferences choice,
owned offline preview and editor launch points are still to be connected. No
normal host selection or viewer profile is written by these helpers or tests.
