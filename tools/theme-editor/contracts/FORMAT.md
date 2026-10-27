# Theme contract v1

`theme.schema.json` defines the manifest shape. Loaders also enforce the path,
reference, byte-bound and base-resolution rules; schema validation alone does
not make a package valid. C# and native loaders replay the same fixture corpus.

The manifest uses UTF-8 JSON, maximum depth 64 and maximum 4 MiB. Duplicate
property names, including escaped equivalents, fail before schema checks.
Resource IDs are case-sensitive. Filesystem paths cannot collide under Windows
ordinal case-insensitive comparison. Paths are slash-separated, relative and at
most 240 UTF-16 code units, without empty/dot/parent components, backslashes,
control characters, colons/streams, reserved device stems or trailing dot/space.
Links/reparse points are rejected throughout published revisions.

Validation order is JSON syntax/duplicate keys, required field shape, format,
identity, base adapter, capabilities, file list/path/size/kind, font dependency
declarations, coverage/hashes, base dependencies, then payload XML/references.
Failures use stable codes rather than parser-dependent text. A rejected import
does not change projects, immutable revisions or selected profiles.

The `files` list is sorted by ordinal path, excludes `theme.json`, and covers all
other files. The revision is SHA-256 of the exact `theme.json` bytes. The base
fingerprint is SHA-256 of the exact `base-dependencies.json` bytes. No hash includes
itself. Export sorts object keys ordinally, writes indented JSON with one final
LF, orders ZIP entries by ordinal path, and uses ZIP timestamp 2000-01-01 UTC,
zero external attributes and the pinned exporter's compression settings.

Archives are at most 128 MiB, expanded content at most 512 MiB, with at most
10,000 entries including the manifest. Manifest/XML/dependency/license files are
at most 4 MiB, encoded images 32 MiB and embedded fonts 64 MiB. Images must be
dimension-checked at no more than 16 megapixels before activation. Initial
authoring validation handles PNG; legacy formats require a native decoder path
before acceptance and must never be silently converted.

Dependency manifest v1 records `format_version`, `base_skin`, `base_scheme`, the
ordered `roots` and sorted `files`. Each record has `path`, `present`, `size`,
`sha256`, `logical_id` and `layer`. Absent candidates have zero size/empty hash.
Gate A captures English XUI, colors, textures and bundled font dependencies;
additional locale coverage must extend the captured resolver inputs explicitly.
Personal overrides and absolute machine paths do not belong in this manifest.

Editable projects use `project.fsthemeproj`, content-addressed `assets/`, saved
undo/redo edits and original asset references. Unknown project metadata survives
save/reopen; unsupported structural edit operations fail rather than flattening.
Export does not rewrite a project. Importing a runtime package starts a separate
compiled-override project with no invented recipes/originals/undo history.

Project schema 2 adds named `color_links` and metadata snapshots to undo/redo.
Links are case-sensitive color IDs, mutually exclusive with a literal override
for the same ID. Export resolves the affected alias/role closure to numeric RGBA
values in runtime format 1; the native viewer does not need project schema 2.
Reference cycles and chains longer than 128 entries are rejected. Unedited
inherited definitions are not normalized or copied into an export.

`OpenDocument` reads at most 4 MiB and preserves the exact source bytes. Version 1
migrates in memory; only Save writes schema 2, retaining the original document in
the content-addressed checkpoints folder. Old history entries without metadata
remain usable. Future versions, unknown recipe operations, and unknown structural
edit/asset/recipe/history fields open read-only. This applies to undo and redo
snapshots too. Save and Compile refuse those documents, including attempts to
overwrite them with a new project. Arbitrary root metadata remains round-trippable.
Recipe records describe retained authoring steps; compiled assets are the prepared
derivatives. Recognizing a recipe operation does not execute it during export.

Image preview composition is runtime-only: it binds the selected logical image
to the existing native sample button, without adding controls, changing global
widget defaults, or writing source project files/history. Its extra complete XUI
payload and capability are validated like an ordinary runtime package. Export
continues to compile the authored project without this preview-only binding.

Multiple image edits use a `pipeline` recipe with an ordered `steps` array of
recipe records in `parameters`. Appends retain earlier steps without modifying
their records. The current editor supports at most 128 steps and 16 nested
pipeline levels. Unknown nested operations/fields/parameters are read-only,
including steps saved in undo/redo history. Earlier editors that do not recognize
`pipeline` preserve the document through their unknown-operation read-only path.

`pixel-draw` records a `tool` (`pen`, `line`, `circle`), `color` in RGBA hex,
integer brush `size`, and top-origin integer `points` (`x`, `y`). A pen retains
its connected path; a line retains two endpoints; a circle retains center/edge.
Drawing writes exact RGBA pixels. Each gesture has at most 4096 points and a
1–16 pixel brush. The prepared PNG is the runtime asset; export does not replay
the recipe. Earlier editors preserve this operation read-only. Cancelled or
pixel-identical gestures do not create project edits.

Static PNG/TGA/BMP/JPEG imports use the existing `replace` operation. The exact
original bytes and extension remain in content-addressed project assets. PNG
imports retain their runtime bytes; other supported imports prepare a PNG with
the decoded pixels. Format mismatches are refused. Explicitly resetting invalid
legacy guides uses the existing `nine-slice` operation, full-image clipping and
zero margins. It changes registration attributes without touching artwork.

Palette batches use the existing literal/link dictionaries. Preview validates
the complete edited color closure without changing the project. Apply prepares
all changes and checks cycles before one checkpoint; repeated direct values are
no-ops. Reset removes only selected literals/links, retaining opaque inherited
values for native resolution. Presets are starting overrides, not a new project
or runtime schema. Other palette edits and non-color assets remain preserved.

Saved picker swatches are local preferences, not project or runtime payloads.
`swatches.json` uses version 1 and at most 32 unique RGBA hex strings, bounded to
16 KiB. Concurrent writers use an exclusive local lease and atomically replace
the JSON after rereading it. Duplicates are no-ops and full lists are not evicted
automatically. Unknown/malformed files stay untouched and disable swatch writes
without disabling ordinary color selection. Saving a swatch does not checkpoint
the theme. Screen-pick output is an ordinary selected RGBA value; it is not
applied to the theme until the existing field's Apply/Set action.

Family edits retain the existing per-image recipe operations. All checked
states are prepared before the document changes, then committed in one
checkpoint. Each logical ID keeps a separate scoped PNG and original reference.
Duplicate IDs, oversized batches and invalid outputs are rejected before
commit. Failed preparation can leave unreferenced content-addressed assets for
recovery; it never changes project edits or history. Byte/geometry-identical
outputs do not create a checkpoint. No runtime-format change is required.

`external-edit` records the `sha256` of the PNG explicitly reloaded from a local
working copy. It retains prior recipe steps and imported originals. Reload is
one image checkpoint; unchanged data creates no checkpoint. Runtime packages
contain the prepared PNG, never the external executable path or session files.
Working-copy records live outside the project and match its canonical directory,
project ID, logical image ID and source fingerprint. Stale reload is refused;
old copies remain available for recovery. Earlier editors preserve this operation
through their unknown-recipe read-only path.

Save As stages a complete project copy in the new folder's parent and exposes it
with a single rename. It retains imported originals, derivatives, history,
checkpoints and extra plain project files, then saves current in-memory edits.
It refuses existing destinations, overlapping source/destination trees, links,
more than 10000 entries or more than 512 MiB of source files. The source is never
written. A failed stage is retained with its path in the error for recovery.

`xui-edit-rules.json` is the adapter's single source for the initial file and
attribute allowlist. Complete replacements retain the base's ordered control
tree and protected bindings/callbacks. Future catalog expansion must preserve
these invariants. Login, consent, update and security UI are excluded.
Changed geometry is bounded integer input; changed color/image references must
resolve in the selected base. Font sizes and offsets must be finite and bounded.
Scale and clip edges cannot be reversed. These rules are checked independently
by both implementations before the viewer loads an override.

Structured XUI edits address a control by its stable preorder index and an
approved property name. A null change restores the recorded base property;
`#text` is available only for text elements. Batches contain at most1024 distinct
properties and compile a staged project before one history checkpoint. Equal XML
ignores document-edge whitespace only; meaningful text, comments and protected
structure stay significant. Attribute values use the native4096-byte UTF-8 limit,
not a UTF-16 character count. Text-node edits use that same UTF-8 budget.
Attribute comparison distinguishes absence from an empty value: removing an
editable width is allowed, while adding an empty protected callback is rejected.
`check-xui-boundaries.py` replays these cases through both production validators.
This stage retains the three existing approved paths. Wider file/type-specific
coverage requires the explicit native allowlist audit.

Font authoring writes the existing runtime font configuration and dependencies;
it introduces no new package or project version. Role/style/platform structure,
unmodified attributes and conditional character filters are preserved. Each font
transaction validates a staged project before one history checkpoint. Failed
validation leaves project/history unchanged; prepared unreferenced assets remain
available for recovery. Embedded files use `font-<sha256>.<extension>` names;
optional license text uses `licenses/font-<sha256>.txt`. Reset removes these font
assets and licenses, retaining unrelated licenses and edits.

Metadata inspection accepts OpenType SFNT and TTC/OTC collections up to64 MiB,
128 faces and128 MiB of retained metadata. It reads Unicode names, OS/2 embedding
flags, fvar axes and cmap4/12/13 mappings with bounded offsets. Unsupported maps
are reported without claiming coverage. Inspection neither installs fonts nor
changes the runtime loader. The native loader controls only `wght`, and collection
loading enumerates its faces rather than selecting an exported face index.
The editor's selected inspection face is not serialized into the project or
runtime package. License edits use the embedded font's actual SHA-256 to locate
`licenses/font-<sha256>.txt`, including legacy embedded filenames. UTF-8 text and
line endings are preserved exactly. License changes stage and validate the whole
project before one checkpoint; identical text creates none. Removing a license
keeps its asset bytes and history available for Undo. Other license payloads are
retained. Unsupported text encoding remains unchanged until an explicit new edit.
See Microsoft's [OpenType file structure](https://learn.microsoft.com/en-us/typography/opentype/spec/otff),
[character maps](https://learn.microsoft.com/en-us/typography/opentype/spec/cmap),
[variable axes](https://learn.microsoft.com/en-us/typography/opentype/spec/fvar)
and [embedding flags](https://learn.microsoft.com/en-us/typography/opentype/spec/os2#fstype).
