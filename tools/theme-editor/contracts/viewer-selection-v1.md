# Viewer profile selection v1

The viewer's `user_settings/custom-theme.json` contains the accepted choice for
that profile. Library revisions remain immutable in the real user's Known Folder
library, independent of redirected APPDATA. Importing or publishing a revision
does not select it for a viewer or the host.

The bounded record has seven fields: `selection_version` (integer 1), `theme_id`,
`revision`, `base_skin`, `base_scheme`, `colors_snapshot`, and `previous`.
The identity strings are both empty for Built-in, or a canonical lowercase UUID
and SHA-256 revision. `previous` is null or a five-field choice containing the
identity strings, base strings and colors snapshot. The snapshot is empty or a
SHA-256 name, never a local path. Future versions, unknown/missing fields,
duplicates, invalid folders and records over 4 KiB are rejected. Legacy null and
two-field ID/revision records remain readable. The top-level identity strings
also allow older readers to consume a selected custom revision.

Preferences stages a choice in memory. Only explicit OK accepts it; initialization
and the ordinary internal `apply()` calls do not write selection files. Cancel
discards the draft. A custom revision fixes its required base. A saved built-in
base and revision are committed in one record, then mirrored to normal viewer
settings through the existing Preferences path. Activation waits for restart;
the running viewer keeps its captured revision.

Apply revalidates the complete package and base, acquires an exclusive profile
theme lock, compares the accepted file bytes and atomically replaces the record.
Busy profiles, changed packages or external record edits fail before replacement.
An explicit replacement of an unreadable record saves its original bounded bytes
under `custom-theme-selection-backups/<sha256>.json`. Base changes save the
existing personal `colors.xml` under
`custom-theme-color-snapshots/<sha256>.xml`; the previous choice records the hash.
These backups are recoverable source bytes and are not automatically overlaid.

Custom transitions preserve toolbars and explicit personal colors. Equal personal
colors remain in the user layer even when a custom palette happens to match them.
An explicit Reset to default still returns to inheritance; inherited theme colors
are not serialized as personal overrides. Built-in-only legacy save behavior stays
available when no custom layer or recoverable custom selection is involved.

Restore previous stages the saved choice without writing. An incomplete-startup
marker tries the previous valid choice, then a distinct last-good choice, then a
built-in fallback. It never reloads the failed ID/revision through different base
metadata. The failed identity stays recoverable in the previous slot. Invalid
packages and future records remain selected on disk with a visible diagnostic
while the viewer uses its built-in fallback.

`viewer_selection_checks` exercises the production selection model and file
transaction using isolated profiles and a copied revision. The full native build's
optional `fs-theme-color-save-checks` target links the production llui serializer
and tests matching overrides, Reset/Set, live handles, and layer separation. Run
that target only with fixture APPDATA/LOCALAPPDATA directories. Neither check
establishes desktop appearance or physical Preferences interaction.

The theme write lock serializes record writers. Stopped-profile targeting must
additionally honor the host's profile/session ownership lock; that is a separate
integration step. Running profiles use their own Preferences transaction.
