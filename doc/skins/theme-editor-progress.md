# Theme editor implementation

Base: PR #88, `85f620154fe0da09d2a64d7fcc0f9f352f358c49`.

The user authorized implementation through the approved staged plan, preserving
candidates awaiting human testing and continuing on new PRs. Publication and
merge still need authorization.

Gate A is ready for the authoring stage at the observed desktop scale. See
[the validation record](theme-editor-gate-a-validation.md) for exact builds,
evidence, human checks and unavailable coverage. The final feature acceptance
matrix remains open.

Native 7.2.5.82310 (`97c217e00f`) and editor 0.1.1 (`90f8a1882c`) remain retained
under `D:/Folderstorm-theme-builds/gate-a`. The latter includes the exporter alias
fix and corrected sample-button state proof; native source remains unchanged.
Neither candidate was installed or published. Shared performance source was
restored after the locked native builds; unrelated build archives remain intact.

The architectural slice includes strict shared contracts, six AnsaStorm scheme
catalogs, lossless projects/history, deterministic packages/import, immutable
library revisions, native resource/font overlays, isolated offline previews,
selection persistence and recovery. Focused checks pass: eight core groups,
40 shared manifest fixtures, 16 shared boundary probes, six recovery cases and
two-preview ownership. The user physically verified hover/held/release states
and the scoped checkbox/radio fix. The WPF Preview action also launched the
correct isolated native child.

Continue in this order:

1. Complete palette/image tools, recipes/slicing, metadata/history, reset/diff,
   project/library/import/export, comparisons and external-edit refresh.
2. Complete fonts/fallback/licenses, advanced widget/layout tools, host palette,
   Appearance launcher and explicit host preference migration.
3. Integrate self-contained editor and optional build component into NSIS and
   Velopack; complete documentation and full acceptance, including DPI.
4. Add and validate the Folderstorm/Firestorm base adapter.
5. If there is no further human steering, plan and begin customization independent
   of an installed base theme without weakening recovery or compatibility.

Managed authoring checkpoints now include pixel-art tools and shared color
pickers (0.5.1), recoverable external-editor PNG copies (0.6.0), and checked-state
family previews/transactions (0.7.0). The family checkpoint passes 32 core,
10 Windows image/codec and 10 non-visible WPF groups plus two C++ package
validations. Windowless layouts are reviewed; external-app desktop access and
full native/DPI acceptance remain open. Each candidate stays retained separately
while remaining authoring work continues. Native 82310 remains unchanged.

Palette tools in 0.9.0 adds Violet/Mint/Paper starting presets and checked bulk
literal, link and reset edits. Previews list before/after values and dependent
colors; settings changes invalidate Apply. One Apply makes one checkpoint.
Unknown inherited color literals can be restored as native-only values. Core
checks pass 34/34, including all three presets across six AnsaStorm schemes and
text/panel contrast. Thirteen windowless WPF groups cover preview, validation,
Cancel, link-target invalidation and Apply/Undo/Redo. Three preset packages pass
the C++ validator. Dialog renders are reviewed at normal/minimum widths; actual
desktop and full native acceptance remain open.

The 0.10.0 candidate adds local saved swatches, a screen picker and a bounded,
scrollable shared color dialog. Eighteen windowless WPF groups pass, including
concurrent swatch saves, malformed/future-data preservation, RGBA restoration,
injected screen sample/cancel/error paths and queued close completion. Desktop
testing verified saved swatches through reopen/select, alpha retention and
Escape keeping the parent dialog open. It caught a late-deactivation crash;
completion now queues one Close and ignores later callbacks instead of writing
DialogResult after the modal loop has ended.

Desktop RGB verification is **not accepted**: automation sampled `#1F321080`
where the captured lab swatch was `#BFA4FCFF` and expected output `#BFA4FC80`.
The final input path prefers the queued mouse message's clicked pixel, with a
visual-point fallback. Its arithmetic/handler checks pass; a physical-mouse RGB
check remains pending. Actual mixed-monitor and system-DPI acceptance also stays
open. Per-monitor V2 is declared in the editor's manifest; it changes no Windows
settings. Core/export/native code is unchanged from the checked 0.9.0 source.

Implementation references: [mouse-message coordinates](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-lbuttondown),
[GetPixel](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getpixel),
[DwmFlush](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmflush)
and [DPI manifests](https://learn.microsoft.com/en-us/windows/win32/hidpi/setting-the-default-dpi-awareness-for-a-process).
This candidate is retained locally as0.10.0 at32dc9d6281. Automatic approval
review rejected its GitHub push because destination/source-egress authorization
could not be established. An explicit user approval prompt is pending; local
implementation continues without attempting another external route.

The 0.11.0 font workspace adds role/style selection, Windows fallback editing and
ordering within platform groups, native weight/hinting/collection options, size
adjustments and named sizes. Embedded files use content-scoped names and optional
license payloads. Transactions compile a staged project before one checkpoint;
Undo/Redo and Reset retain other edits. Inspection reports collection faces,
variable axes, embedding flags and available license text, with code-point
coverage across the inspected chain. Conditional emoji, shaping and native
system fallback behavior remain explicit limitations.

Initial full runs passed39 core groups and21 windowless WPF groups. Font maps
agree with an independent WPF reader for Inter and DejaVu; a newly exported
embedded-font/config/license revision passes C++ validation. Normal/minimum-width
top/details renders are reviewed, including a corrected foreground inheritance
issue. Actual desktop testing passed readable controls and multilingual sample,
weight400→650, Undo400, Redo650, details scrolling and Save. The isolated saved
fixture retains650 and one checkpoint. The owned editor closed normally and
computer control is off. Final font-only checks also cover later malformed-value,
empty-search, metadata-budget and stale-inspection guards. Native font visual,
real system-DPI and mixed-monitor acceptance remain open.

The0.12.0 collection/license stage adds an inspection-only face selector with
per-face metadata and glyph mapping. A real Windows MSGOTHIC.TTC fixture verifies
that selection/sample changes preserve native configuration and do not copy the
installed font. Embedded-font licenses can be saved or removed in their own
panel, with no-op detection and one-checkpoint Undo/Redo. License text/line endings
survive save, reopen and export; malformed/oversized/installed targets and failed
validation leave the project/history intact. Non-UTF8 existing bytes remain
unchanged. Reset retains unrelated license files.

Seven focused core font groups and five windowless WPF font groups pass. The UI
checks caught stale license text after Undo when equal font-reference records did
not fire SelectionChanged; reference/inspection/license refresh is now explicit.
Three collection/license layouts are reviewed at normal/narrow widths. The new
font/license export passes C++ validation. After retention, the exact0.12.0 build
passed desktop license Save, visible Undo/Redo, project Save and normal Close in
an isolated copied project. The saved UTF-8 text matches exactly; the owned
process is absent and the computer-use kernel is reset. Collection dropdown
interaction and native/system-DPI acceptance remain open. The receipt is outside
the immutable archive at `theme-editor-font-collection-evidence-20261027/desktop-runtime-check.json`.

Widget/layout work continues on `codex/ansastorm-widget-editor`. The new model
exposes control hierarchy, typed/current/inherited properties and source/hash
provenance. Known-property batches and raw XUI replacements validate before one
checkpoint; property/file reset restores inheritance and keeps original bytes.
Protected callbacks, required names and ordered structure retain the existing
native rules. This first model uses the three already approved XUI files; wider
file/property coverage still requires the planned explicit adapter/native audit.
The structured widget UI and state preview are implemented in the0.13.0 source.

Three focused XUI model groups and one WPF raw-handler group pass. They found
document-edge newline differences creating false edits, and rejected texture XML
replacing the shared project object while the font tab retained the old one.
XML equality now ignores only document-edge whitespace while retaining comments;
font reference no-op detection uses it too, including a re-serialized unchanged
reference regression. Raw XML now validates a staged edit copy and retains
document ownership on failure. Full core checks pass44/44 after the shared
comparison change. The width/Unicode-label/RGBA widget export passes the C++
validator as revision3db57a4e5a919962595c6c6189590d3e1375496d6fa4c5db3dbf3e8419b6951e.
Those foundation checks retain their original scope.

The0.13.0 workspace exposes searchable controls, current/base properties,
protected values, color/image reference lists, the shared picker and explicit
Set/reset actions. Before/after use a shared comparison scale and state selector.
Samples use native artwork, clipping and fixed slice borders, with approximate
WPF text/behavior clearly labelled. Final normal/narrow layouts are reviewed;
small-row clipping was corrected. Exact asymmetric slice pixels are asserted.

Fresh full runs pass45 core groups,28 windowless WPF groups and12 codec groups,
including all1641 bundled TGA images. Two final exports pass the C++ validator:
the widget edits and a4095-byte Unicode attribute. The4098-byte counterpart is
rejected atomically by the editor. Managed attribute validation now counts UTF-8
bytes, matching the native limit. New desktop/native-appearance and system-DPI
acceptance remain open until separately recorded.

The exact retained0.13.0 desktop check passed label Set, visible before/after,
Undo, Redo and Save in a disposable project. Disk XML retains the label and one
Undo checkpoint. Its owned native preview reported ready, but two screenshot
attempts timed out. Native appearance is not accepted. Both test windows closed
after the editor Close; computer control is disconnected. The receipt remains
outside the immutable archive at `theme-editor-widget-evidence-20261027/desktop-runtime-check.json`.

The next local branch, `codex/ansastorm-xui-rules`, fixes native attribute presence
comparison and text byte limits. The old native validator disagreed on4 of15
shared cases: removing width, adding an empty protected callback and two oversized
text cases. The corrected standalone validator and current managed CLI agree on
all15. Five focused XUI model groups pass, including atomic rejected edits.
Attribute lookup no longer inserts absent map entries; both validators enforce
the4096-byte UTF-8 budget on text content and use the policy's text-element list.
These checks do not rebuild or validate a new viewer installer. Native82310 and
the retained0.13.0 editor are unchanged. Broader exact file/type/property rules
and a matching native build remain in progress.

Expanded widget rules now cover27 exact English paths and47 control/nested
parameter types, using168 visual attributes. The inspector consumes those type
rules directly. Geometry, supported anchoring, bounded glow/spacing, exact state
images, color references, booleans and enum/flag values validate in both languages.
Names, callbacks, bindings and unknown parameters remain protected. Button
disabled-selected colors and widget font subparameters use their actual native
spellings. The sample routes focused input artwork and the corrected state/font
properties; WPF rendering remains approximate.

Fresh full runs pass48 core groups,30 WPF groups and12 codec groups. All30 shared
expanded revision fixtures agree in managed/native validation, and a single
package containing all27 approved XUI payloads passes C++ validation. Focused
checks cover template navigation, per-control inspector filtering and state/font
sample updates. The normal/narrow/asymmetric WPF renders are pixel-identical to
the reviewed0.13 renders. These are separate from native/system-DPI acceptance.

The preview protocol now checks an exact rule fingerprint when XUI is present,
before library publication or child launch. Shared CMake and managed definitions
normalize CRLF to LF; an independent SHA-256 check verifies both line endings.
Final managed checks cover that normalization. Native82310 and retained editors
remain unchanged. The expanded policy requires a new matching viewer build;
its private build/retention is the next step, with appearance acceptance pending.

The matching0.14.0/7.2.5.82311 candidate is now privately retained at source
`0a3e1de458e42950b7e8d4eab3435ff22c694056`, with an NSIS installer, runnable
viewer, editor, private symbols, source bundle and evidence. All402 editor files,
installer, viewer, symbols and bundle hashes match their receipt. The native
capability fingerprint matches the editor. The shared checkout is restored and
the exclusive build is finished. Earlier candidates remain unchanged.

Exact retained0.14 desktop checks passed native `font.style=BOLD|ITALIC`, visible
before/after, Undo/Redo and Save in the disposable project. Disk XML retains the
native field and one checkpoint. Its matching native viewer reported ready with
no fallback diagnostic. Two native capture attempts timed out; appearance remains
unverified. One editor Alt+F4 closed its owned child; both exact processes are
absent and computer control is disconnected. The immutable archive is untouched;
the later receipt is `theme-editor-xui-rules-evidence-20261027/desktop-runtime-check.json`.

The next branch, `codex/ansastorm-library-preview`, starts package/library work.
Its model lists bounded library headers without writes, reports unsupported
headers, checks immutable folder identity, rejects stale selections and validates
the full chosen revision before use. Thumbnail edits preserve exact PNG bytes,
stored originals, unrelated edits and one-step history. Three focused model groups
pass, including save/reopen, no-op/reset/Undo, corrupt preview and future-header
cases. These are model foundations; the browser UI, native capture protocol and
image-codec/desktop acceptance were pending at that checkpoint. Native capture should use
the isolated viewer's own renderer to generate package thumbnails, with explicit
session/revision binding, rather than depending on desktop frame capture.

The branch now implements the library browser, package-file inspection, isolated
preview and explicit open-as-new-project flow. It validates the chosen revision
and decodes its PNG before enabling actions. Rapid selections share one validation
slot and skip stale queued work. Rows identify their revision and explain absent
preview images. A development desktop check opened the real library and validated
existing revisions; the final label/empty-state polish still needs desktop review.

Project now supports PNG import/reset with visible Undo/Redo, saved exact bytes
and independent compiled-package import. Its capture action starts a fresh owned
offline preview, locks project changes during capture and decodes before committing.
Native capture protocol1 binds the request ID, generation, revision, process and
fresh capture ID. The renderer reads its completed startup UI frame before swap,
preserves readback state and atomically writes only a nonce-derived PNG in its
session. This is a package-thumbnail feature, not desktop automation.

Focused capture/library model checks pass5 groups; the full WPF suite passes33,
the codec suite passes12 (including1641 TGA assets), and managed/native capture
acceptance agrees on17 cases. The first full core run found a test-directory
collision (51/53); those fixtures now have separate roots and the fresh full run
passes53/53. Final browser polish also passes3 focused WPF groups; the33-group
full WPF run preceded only that row-label/empty-preview presentation change.
A matching native build, real capture/PNG appearance, desktop open-as-project and
system-DPI checks remain pending. No installation, source push or publication.

The exact0.15.0/7.2.5.82312 source `2f7ad70da4c9aed8239cc555865190895daa33de`
is now privately retained with NSIS, runnable viewer, editor, symbols and source
bundle. All402 editor files and the recorded native artifacts match their hashes.
The shared checkout is restored, the cache checkpoint identifies82312 and no
build owner remains. Earlier archives stay unchanged.

An actual Project capture produced a1264x861 native renderer PNG at revision
`92c4bafef4d9e5364285b540b89a5c775f558da368cee24e04ed7cf9d3412b8c`.
The image was decoded and reviewed: controls, text and colors are drawn, with
correct orientation and opaque background. Actual Ctrl+Z removed the thumbnail,
Ctrl+Y restored it and Ctrl+S saved it. The saved PNG matches the nonce-bound
native receipt SHA and one durable checkpoint. Both owned processes are absent
and computer control is disconnected. The later receipt and PNG are outside the
immutable archive in `theme-editor-library-evidence-20261027`.

Editor desktop capture timed out twice after native launch; subsequent checks
used supported accessibility/keyboard. Final library image/open-as-project
desktop checks, changed-state captures and actual system DPI remain open.

The next branch, `codex/ansastorm-compiled-palette`, fixes package import semantics.
Its first change selects an archive's declared base before validating XUI; the
0.15 archive picker could otherwise reject valid widget packages for lacking a
base. The new path preserves the bounded ZIP reader and exact payload bytes.
Six focused library/capture model groups and the editor build pass. No0.16
candidate is retained yet. Imported raw palettes still need effective inspection,
collision-free staged edits/reset, and atomic destination-safe project import.

The0.16 branch now resolves imported colors as a separate native-style layer.
The inspector, quick sample, widget samples and bulk tools read that layer.
Explicit compiled literals stop authored role fan-out; missing original links
are not inferred. Imported XML and manifest bytes survive a no-op exactly.
Set/link/reset and reset-all stage validation before a checkpoint, keep originals
and unrelated edits, and support durable Undo/Redo. Bulk preview performs no
writes. Unrelated native-only references remain in the source XML.

Import now validates a byte snapshot, stages a full project in a new sibling and
renames without replacement after validation. Existing destinations and folders
inside the protected library are refused. Failed stages stay recoverable without
exposing the final project. The optional `compiled_manifest_path` edit field
retains exact original header formatting, capabilities and font metadata. The
actual retained0.15 reader opens this field read-only and rejects Save with source
bytes unchanged; normal older projects omit the field and stay compatible.

Full runs pass59 core groups,36 windowless WPF groups and12 codec groups. New
checks cover exact imported values/bytes, explicit override precedence, failed
cycles and compilation, scoped reset, save/reopen, raw native-only references,
manifest formatting/metadata, destination bounds and an8000-license import whose
project exceeds the authoring limit after staging. The final destination stays
absent and its stage is retained. A package with edited imported colors and
preserved declared font/capability metadata passes the matching C++ validator.
Full suites precede only a final empty-XML rejection/depth-diagnostic guard,
covered by the final focused imported-project run. Retention and new desktop
acceptance are next; source remains local and the whole goal is incomplete.

Remaining work includes widget/layout editing, project/library/package tools, host/distribution/base
phases and the outstanding native/desktop acceptance. Retained candidates stay
available while these phases continue.

The0.16 source5f0c05ae71 is now retained as a separate private candidate with402
hash-verified editor files and the unchanged82312 native viewer. Actual desktop
checks passed imported mint provenance, Set/Undo/Redo/Save, package-file inspection
and Open as project into a separate folder with fresh history. A real1264x861
native renderer capture was decoded and saved with its receipt hash unchanged.
The desktop receipt remains outside the immutable archive. Owned editor, viewer
and crash-handler processes are absent; the computer-use connection is reset.

The next project-changes branch adds a searchable/grouped review of palette,
artwork/registrations, fonts/licenses, formatted XUI, thumbnails and other files.
Resets use isolated candidate edits and validate the complete result before a
single appearance checkpoint. Original files, details and imported header remain
recoverable. An edited registration table resets its related artwork/recipes as
a group. Invalid scopes, stale authoring/content snapshots and failed package
compilation leave the source document/history unchanged. The raw XML editor is
refreshed when its selected override changes. Pending details validate first and
retain a separate checkpoint, matching the other authoring tools.

Initial focused checks pass5 core groups and3 windowless WPF groups. Full runs
pass64 core and39 WPF groups. These precede the final XML changed-property summary,
wrapping and raw-XML refresh on Undo/Redo. Final focused checks pass5 core,
3 project-change WPF and7 XUI WPF groups. The final narrow/wide comparison renders
show the changed property before the full XML with readable wrapping.
No native code, XUI policy, image codec or project schema changed.0.17 retention
and desktop review remain pending; broader full-goal phases continue.

The exact previously rejected source push was retried after connected GitHub
confirmed repository ownership and push access. Automatic approval review again
rejected it because trusted user content does not explicitly approve this public
branch/destination. No source was pushed. The existing prompt stays pending;
local branches, bundles and candidates continue without an alternate egress path.

Computer control is off. Prefer user screenshots for small visual checks, use a
question prompt for input, and leave the user's personal viewer alone. Any future
computer-control batch must be announced, delayed at least five seconds, kept
short and explicitly disconnected when finished.
