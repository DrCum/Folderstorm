# Offline host preview v1

The host dispatches `--fs-theme-host-preview <request.json>` after DPI/common
controls initialization and before update checks, crash reporting, host/profile
construction or controller services. Malformed preview arguments fail without
entering the normal host. The standalone development probe uses the same module.

The capability file must advertise `host_preview_version: 1`,
`host_palette_version: 1` and source provenance. The managed owner checks this
before publishing a package, creating a session or launching a process.

Requests are strict JSON objects, at most 4096 bytes, containing exactly:

```json
{"host_preview_version":1,"request_id":"<64 lowercase hex>","generation":1,"theme_id":"","revision":"","baseline":0}
```

`generation` is a positive signed 64-bit integer. `baseline` is 0 (Midnight) or
1 (Daylight). Both theme identity fields are empty for a built-in sample; a custom
choice requires the canonical lowercase GUID and immutable revision hash.
The request lives at the actual Windows user's known local application-data
folder, `Folderstorm/theme-host-preview/<lowercase GUID N>/request.json`.
Redirected environment folders do not redefine this boundary. Links and paths
outside it are rejected. Extended Windows path prefixes are normalized only for
the path comparison.

Custom packages are captured from the stable known roaming theme library and
validated against the base beside the host executable. Missing or invalid
packages produce a built-in fallback and diagnostic. Captured colors stay in
memory; the preview does not change normal host or viewer selections.

The native window uses production `Theme` brushes, buttons, checkbox, editable
text, owner-drawn combo/list and scrollbar behavior. Its high-contrast checkbox
changes only this preview, preserving system high-contrast precedence.

Atomic `status.json` responses bind protocol version, request nonce, generation,
PID and actual revision. States are `ready`, `fallback_ready`, `failed`, `closed`.
Fallback uses an empty actual revision. Responses include all seven packed RGB
roles, contrast flags, window DPI and text-scale percentage. The managed reader
bounds status to 8192 bytes and rejects mismatched ownership, revisions or colors.
Reported colors establish renderer state, not screenshot or desktop acceptance.

The editor exposes Preview host. A supplied `--host` executable takes precedence;
otherwise it uses the session host beside the configured viewer. Preview compiles
draft details without committing document/history changes. Close preview and
editor shutdown close both owned preview types. PID/start-time guards prevent
closing unrelated processes. Child environment profile folders are isolated.

This module's development checks do not establish the full host build, normal
host Preferences interaction, physical input or numeric system-DPI
acceptance. Those remain separate integration and validation work.
