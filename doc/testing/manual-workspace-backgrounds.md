# Native workspace backgrounds

Open **Preferences → Move & View → Backgrounds** after logging in. Backgrounds occupy the spare native workspace outside the world viewport. They are static images; host character-preview windows are separate.

## Behavior

- **This workspace → Use default background** inherits the account's default. Editing **Default for workspaces** updates every inheriting workspace. Custom backgrounds, including display overrides, belong to the saved workspace. Apply/OK accepts edits; Cancel restores the current background, shared default and pending saved records.
- Sources are solid color, an inventory texture/texture UUID on its original grid, or an imported PNG/JPEG/BMP/TGA. Local imports are limited to 32 MiB, 32 megapixels and 8192 pixels per side, normalized to at most 4096 texture pixels per side. Original image dimensions are retained for aspect ratio, Center and Tile.
- Local files are copied into the character's persistent account data directory under `workspace_backgrounds`. They are never uploaded. Workspace XML contains validated managed filenames, not absolute paths or image data. Moving/deleting the original is safe; transferring XML to another profile may require **Locate replacement**.
- Fill crops proportionally; Fit letterboxes; Stretch ignores aspect; Center and Tile use original image pixel sizes. Span uses the combined workspace and suspends saved display overrides. Brightness dims the image independently of its backing color.
- Display overrides use normalized geometry in left-to-right/top-to-bottom order, never session-only native IDs. An incompatible arrangement uses the common image and retains the assignments for explicit **Reassign saved displays**. Unsupported display enumeration uses one client area. Up to 16 display slots are retained.
- Workspace schema 3 adds independent Background bit 128. Schemas 1/2 migrate to inherited backgrounds on full application; partial group selection removes omitted background effects. The original window-only `All` mask remains 15. Full capture, startup/Last, Previous, export/import, preview and rollback carry the new group. No host IPC or MCP command was added.
- Rendering happens after scene finalization and before HUDs, restriction overlays and native UI. It clips non-overlapping rectangles outside the viewport, preserves graphics state, and is omitted from snapshots and host preview renders. Full-window/Mouselook without spare workspace does no image work. Native monitor enumeration and file/decode work stay out of drawing; fetched textures use the existing viewer texture manager and are shared by source.

## Focused validation

Configure `indra/newview/tests/folderstorm` as usual. Build only `workspace_background`, `workspace_layout`, `workspace_usability` and `world_view_geometry`, then run:

```sh
ctest --test-dir <build-dir> --output-on-failure \
  -R '^(workspace_background|workspace_background_adapters|workspace_background_native|workspace_layout|workspace_usability|world_view_geometry)$'
```

The adapter entry runs seven Python/C++ cases: strict background records and legacy migration, the existing workspace serialization suite, current/default editing without unrelated group changes, repeated-source texture reuse and cropped drawing, asynchronous import ownership/cancellation, native wiring contracts, and XUI bindings/bounds. Portable LLSD/UI fixtures test production model/adapter code; they do not validate real XML persistence or GPU output.

The native-boundary entry adds two regressions. The first executes production JPEG recovery declarations/handlers across two overlapping threads, rejects the old shared-buffer declarations safely, and compiles the existing ARM64 guards. The second executes the native texture factories/cache and directory append helpers: an existing managed file fails skin lookup, succeeds through its local-file URL, and reuses the same cached texture. Paths include spaces and Unicode; missing files and cross-grid texture references still fail safely. Hashing, file decoding and GL upload remain boundary fixtures.

The six focused CTest entries pass. The JPEG regression also passes when linked to the available native libjpeg archive, with 64 malformed-header errors returning to their owning thread. To repeat that check against your native dependencies:

```sh
FS_BACKGROUND_JPEG_INCLUDE=<directory-containing-jpeglib.h> \
FS_BACKGROUND_JPEG_LIBRARY=<path-to-libjpeg-archive> \
python3 indra/newview/tests/test_fsworkspacebackground_native.py \
  NativeBackgroundBoundaries.test_jpeg_error_context_is_thread_local -v
```

JPEG's error buffer is thread-local on non-ARM64 platforms; the existing ARM64 guards are unchanged. Managed images use `getFetchedTextureFromUrl` with `FTT_LOCAL_FILE`, bypassing the skin-relative file helper while retaining viewer caching. Real-header C++20 syntax checks pass for the changed JPEG codec and background runtime, in addition to the original background/workspace checks. Standalone `llappviewer.cpp` checking stops at the environment's missing pre-existing `cef/dullahan_version.h`. A complete viewer link, Windows/MSVC build and graphical acceptance have not been performed.

## Windows acceptance

1. Reserve a left/right workspace. Test a non-square photo, transparent PNG, BMP/TGA, an inventory texture and a pasted texture UUID. Compare all six modes with the diagram and adjust brightness/backing color. Verify accurate proportions and original-pixel tiling at 100% and 200% DPI.
2. Set a shared default, keep one workspace inheriting and customize another. Change the default, switch repeatedly, use Previous, restart and test named/Last startup restore. Existing schema-1/2 workspaces should inherit. Applying only Maps, Chat, Camera or window geometry must preserve the background; Background-only application must preserve those groups.
3. Import an image, delete the original, Apply and restart. Try a corrupt/oversized file and a missing managed image. Locate a replacement. Select an unavailable texture and one saved on another grid; expect the backing color and a settings status.
4. Preview new current/default images, then Cancel. Repeat with Apply followed by another edit/Cancel. Finish a picker/import after cancelling, changing tabs, switching login or closing Preferences; no late result may replace the current choice.
5. Span two/three unequal displays; customize individual displays, switch Span on/off, move/resize the viewer and change viewport layouts. Change DPI and unplug/rearrange displays. Verify safe fallback and retained explicit reassignment without misplaced overrides.
6. Test Mouselook with custom viewport kept and disabled, fullscreen/windowed mode, dark/light skins and translucent floaters. Wallpaper must stay behind native windows, HUDs and enforced overlays, and must not intercept clicking, typing, editing or camera input.
7. Check world-only snapshots and inactive-character host previews. Neither should contain wallpaper. Repeatedly replace images and use the same image on several displays; verify shared texture behavior without per-frame file I/O or native display enumeration.
