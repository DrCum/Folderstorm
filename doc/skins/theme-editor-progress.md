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
The structured widget UI and state preview remain in progress.

Three focused XUI model groups and one WPF raw-handler group pass. They found
document-edge newline differences creating false edits, and rejected texture XML
replacing the shared project object while the font tab retained the old one.
XML equality now ignores only document-edge whitespace while retaining comments;
font reference no-op detection uses it too, including a re-serialized unchanged
reference regression. Raw XML now validates a staged edit copy and retains
document ownership on failure. Full core checks pass44/44 after the shared
comparison change. The width/Unicode-label/RGBA widget export passes the C++
validator as revision3db57a4e5a919962595c6c6189590d3e1375496d6fa4c5db3dbf3e8419b6951e.
The structured UI/state preview and final expanded UI checks are still required.

The next native rules audit must include boundary parity for UTF-8 attribute
length versus C# string length, editable-attribute removal and text-node limits,
alongside wider file/type-specific property coverage. Current small Unicode
fixtures pass; they do not establish those boundary cases.

Remaining work includes widget/layout editing, project/library/package tools, host/distribution/base
phases and the outstanding native/desktop acceptance. Retained candidates stay
available while these phases continue.

Computer control is off. Prefer user screenshots for small visual checks, use a
question prompt for input, and leave the user's personal viewer alone. Any future
computer-control batch must be announced, delayed at least five seconds, kept
short and explicitly disconnected when finished.
