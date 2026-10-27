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

Bulk/state-family edits, external editing and additional import formats remain
in progress. PNG remains the package
replacement format. Native source and the retained 82310 installer are unchanged.
Final feature/DPI acceptance is open.
