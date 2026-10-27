# Image editing stage

This stage follows the palette/TGA snapshot in PR #126, retained as editor 0.2.2
at `b27ecf2414`. The user verified TGA artwork/transparency, palette changes,
Undo/Redo, inspector scrolling and Save As. Those candidates remain separate.

The image backend now stores content-addressed current/original files and applies
PNG derivatives through a registration scoped to the selected logical image ID.
An inherited shared file is never replaced by that operation. Successive edits
retain the original source and ordered recipes; undo/redo restores earlier
derivatives. File preparation finishes before the document checkpoint changes.
Changed stored bytes are rejected rather than overwritten.

`TextureGeometry` keeps the native bottom-origin clip rectangle separate from
top-origin border margins. Native stretch coordinates are relative to the clipped
image, not the entire source bitmap. Crop and resize transform both regions;
preview uses the clipped pixels. The original Round Rect top/bottom definitions
exercise this distinction.

Validation:

- 23 core/model groups pass, including original-byte preservation, changed-file
  refusal and clipped/stretch geometry through crop/resize.
- Six Windows model/codec groups pass. The derivative integration case edits
  `Inv_FolderClosed`, preserves its TGA original and leaves the shared-file sibling
  `Inv_SysClosed` unchanged. Save/reopen, history and runtime export pass.
- The exported revision
  `6861da4864ed6d1d5dcc492e143cb0849d11f5a8e6c5586d38d43aec5be0bd70`
  passes the production C++ package validator. This is package acceptance, not a
  new native visual check.
- The TGA decoder's 1641 bundled-file pixel checks remain part of the codec probe.

The first image workspace now has family/override filtering, current/original
comparison, zoom and pixel picking, crop/resize, tint and basic adjustments,
fill/gradient/border generation, three stretch samples and draggable/numeric
slice/clip guides. A guide drag commits on release; Escape or capture loss cancels
it. The user verified readability, tint, Undo/Redo and guide dragging/Escape in
retained editor 0.3.0. Multi-DPI checks remain open.

From Images, Preview in viewer composes a preview-only complete sample XUI with
the selected logical image on `test_button` at 100x32. All its image states use
that ID, its text is empty, and global button defaults remain inherited. Export
and source project files/history are unchanged. The composed preview revision
`1e4319bddb82a307fac576ba7bcc591720b1c445e2c506e285167fb174ab0f05`
passes the production C++ validator. The corresponding UI check is recorded below.

The first human native-image check still displayed the ordinary Button label.
Its owned preview request used the original combined revision rather than the
selected-image composition. The 0.3.1 correction captures the active tab/image
before disabling the workspace and restores the active tab afterward. It also
forwards the ComboBox selection template selector so family labels render, and
clears an earlier image's guide error when a valid image is selected. The WPF
build passes. The user confirmed the retained 0.3.1 native sample has no Button
text. Its ready revision
`030e31227221ee3caed786c2ca39e00948178e6141ff5fe71bd44e855f681d6a`
contains the selected `PushButton_Off` binding and empty label. Family label
rendering has not been separately confirmed.

The next image stage adds Reset image, Reset family and Reset all images. A
group reset makes one checkpoint; stored originals and history files are never
deleted. A reset restores the inherited registration and removes its recipe.
When a physical-file override is shared, a selected-image reset instead binds
that ID to a private copy of the inherited pixels so siblings keep their edits.
Reset all removes image registrations, derivatives and recipes while preserving
palette, font and license edits. Empty/repeated resets do not add undo entries.

Samples can switch between stretch and tile at the same three sizes. Tiling
repeats the clipped image without interpolation and preserves transparent RGB.
Core checks now pass 25/25, including reset/history/original preservation and
partial tile edges. Seven Windows image/codec groups pass, including a shared
PNG reset that restores the selected ID's inherited pixels while preserving the
sibling's changed pixels, save/reopen, Undo/Redo and repeated-reset no-op behavior.
Registration output ignores whitespace outside its root so parse/write cycles do
not add meaningless changes. These new controls still need runtime visual checking.

The user confirmed Undo and Tile in retained 0.4.0, but Reset image appeared to do
nothing. A windowless check of the production WPF handler reproduced it: the
model reset succeeded while the selected image/canvas retained the edited pixels.
The 0.4.1 correction explicitly reloads the selected image after refreshing the
catalog. Image/family/all-image reset and Undo/Redo share that refresh path;
Undo also refreshes an empty Overrides filter. Four focused WPF handler checks
pass after reproducing the stale-pixel failure before the fix. These checks do
not show a window or launch a viewer. Human confirmation of 0.4.1 remains open.

The user resumed implementation and requested pixel-art Pen, Line and Circle
tools plus a picker beside each hex-color field. These use the existing dark
workbench and image canvas. The toolbar defaults to Inspect; drawing is explicit.
Pen connects input points; Line replaces its live endpoint preview; Circle is
dragged from center to edge. Brushes write exact RGBA pixels, including alpha
zero, with no smoothing or blending. The optional grid appears at 6x zoom or
larger. A completed gesture creates one scoped PNG/recipe/checkpoint; Escape or
capture loss cancels before the project is touched. Original comparison and
read-only documents cannot be drawn on.

The shared picker offers saturation/brightness, hue, opacity, hex/RGBA input and
small preset swatches. It is available for palette values, image tint, both
generated colors and drawing color. Choosing a color updates its field; Set
color, Apply tint and generation commands retain their existing apply behavior.
The pixel inspector can also copy an artwork pixel into tint and drawing color.

Core checks now pass 28/28, image/codec groups pass 8/8, and focused WPF handler
checks pass 6/6. The production C++ validator accepts the drawing revision
`b5b25e0e9655774e381f3a0a63902775fa84e3c03ff067f89c65705c01ee077b`
and its sample preview
`7b9e01a9966965cbe1449c5951f7edd1908a866411770411de5ffa78e9d50ec0`.
Actual desktop checks for the pixel tools remain in progress. A Windows staging rename
failure in Save As exposed a short file-sharing race; its final rename now uses
the same bounded Windows retry pattern as library publishing. The source project
and failed stages remain preserved. Closing the editor now queues the final
Close call after its first Closing event returns, avoiding reentrant close while
still disposing only its owned preview.

Actual 0.5.0 desktop checks now confirm the palette picker Cancel path, drawing
picker saturation/hue/opacity, Pen/Line/Circle, one-gesture Undo/Redo, saved recipes
and a single-click editor close after its owned preview closes. The saved test
has exactly three drawing checkpoints. The native preview reported ready with
the exact saved PNG hash and selected-image/empty-label binding. Its OpenGL
capture failed twice with `FrameArrived timed out`; no native visual result is
claimed for the painted icon. The owned viewer/editor were closed and computer
control disconnected.

The 0.5.1 follow-up widens the clipped brush-size label and adds a canvas tooltip
for the active tool. Eight non-visible WPF checks pass, including the production
Escape and lost-capture handlers restoring image data, display bitmap, command
availability and unchanged project/history. Their gesture state is primed in
the probe; this is not a claim that Computer Use held the mouse while pressing
Escape. Core/codec/native-package code remains unchanged from the tested 0.5.0.

The 0.6.0 candidate adds an explicit external-editor round trip. Choose an
installed executable, open a separate PNG copy, save it externally, then Reload.
TGA sources are converted to working PNG without touching the base. Reload
validates and fully decodes changed bytes, updates resized geometry and makes
one scoped checkpoint. A watcher reports exact-file changes, including atomic
replacement, without applying them. Opening the same project/image can recover
the last compatible local session. Stale project-side edits refuse Reload;
working copies remain available. Pending external edits are identified before
closing, opening another project or Save As. No external application is killed.
Local executable paths and session data never enter project/runtime exports.

Thirty core groups, nine image/codec groups and nine non-visible WPF handler
groups pass. These cover original/sibling isolation, resized guide geometry,
exact imported PNG bytes, recovery, no-op reload, malformed/missing files,
conflict refusal and Undo/Redo. Desktop round-trip validation is in progress.

The 0.7.0 candidate adds Edit family with checked-state Before/After samples.
Tint, color/alpha adjustments, resize and guide sharing use each state's own
source artwork. Preview is explicit and changing a field invalidates Apply.
One Apply creates one checkpoint after all outputs and assets are prepared;
unchecked states, originals and prior recipes remain intact. Invalid inherited
guides stay visible with an explanation and cannot be selected. Family previews
are bounded to 64 states and 16 megapixels; batch PNG outputs to 128 MiB.
The document transaction and GUI handler checks cover failure after partial
asset preparation, neutral/repeated no-ops, Cancel, settings/validation gating,
durable history and selected-image redraw. Checks pass 32/32 core, 10/10
image/codec and 10/10 non-visible WPF groups. Both normal and selected-image
revisions pass the production C++ validator. Reviewed windowless WPF renders at
1060 and 820 logical pixels show readable controls, wrapping state cards and
fitting actions. Additional 1.5x/2x bitmap renders are retained. These do not
establish live desktop or numeric system-DPI acceptance, which remains open.

The 0.8.0 candidate adds PNG/TGA/BMP/JPEG import with bounded reads, supported
container checks and retained exact original bytes/extensions. PNG runtime
bytes stay intact; the other supported formats prepare an RGBA PNG. Import
continues to affect only the chosen logical ID. An explicit Reset guides button
appears for decoded images whose inherited bounds cannot be edited. It replaces
those bounds with full-image clipping and zero margins without changing source
artwork. Undo restores the inherited attributes. Project-side image edits now
mark external working copies stale before Reload is attempted.
Twelve image/codec and twelve non-visible WPF groups pass, including a real
inherited `BuyArrow_Press` guide fixture. Four import/repair normal and preview
revisions pass the production C++ validator. Core and its tests are unchanged
from the 32/32 checked 0.7.0 source. Actual desktop acceptance remains open.

Additional formats requiring native inspection remain unavailable. PNG remains the package
replacement format. Native source and the retained 82310 installer are unchanged.
Final feature/DPI acceptance is open.
