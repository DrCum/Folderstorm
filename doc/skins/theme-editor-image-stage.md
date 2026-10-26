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
it. These UI interactions still need runtime visual checking.

From Images, Preview in viewer composes a preview-only complete sample XUI with
the selected logical image on `test_button` at 100x32. All its image states use
that ID, its text is empty, and global button defaults remain inherited. Export
and source project files/history are unchanged. The composed preview revision
`1e4319bddb82a307fac576ba7bcc591720b1c445e2c506e285167fb174ab0f05`
passes the production C++ validator. Actual native visual checking is pending.

Tiled samples, bulk/state-family edits, external editing, per-image/group reset
and additional import formats remain in progress. PNG remains the package
replacement format. Native source and the retained 82310 installer are unchanged.
Final feature/DPI acceptance is open.
