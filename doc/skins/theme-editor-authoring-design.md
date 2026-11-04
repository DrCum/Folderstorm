# Authoring stage

This branch follows PR #125 and its recorded Gate A native checks. The matching
native 82310 build and editor 0.1.1 remain retained separately. Current image-stage
checks and remaining acceptance are recorded in `theme-editor-image-stage.md`.
The user verified palette
changes, Undo/Redo, inspector spacing/scrolling and Save As in retained editor
0.2.1. Other workflows and final multi-DPI acceptance remain open.

Use the native viewer's visual vocabulary: a quiet violet workbench surrounding
one large, useful preview. Spend the visual emphasis on the selected artwork and
the changes it makes. Avoid dashboard cards, marketing copy and decorative stats.

| Token | Value | Use |
| --- | --- | --- |
| Ink | `#111016` | Workbench background |
| Panel | `#1A1921` | Browser and inspector |
| Line | `#302D3B` | Pane boundaries and inputs |
| Violet | `#BFA4FC` | Selection and primary action |
| Mint | `#64D8B2` | Valid state and comparison guides |
| Text | `#F5F3FA` | Primary text |

Use Segoe UI Variable/Segoe UI for controls and Cascadia Mono/Consolas only for
XML, channel values and pixel coordinates. Keep labels in sentence case. Use
short tooltips instead of repeating explanatory subtitles.

The layout is left aligned: project actions across the top, a compact section
rail and searchable browser on the left, a flexible working canvas in the middle,
and a 320-pixel inspector on the right. Pane splitters let artwork take more room.
Matching-viewer/base paths belong in setup controls, not permanent large fields
above the canvas. Keyboard focus and unsaved state remain visible.

The canvas changes with the task: a native-inspired sample scene for palette
work, a zoomable checkerboard with slice guides for artwork, and mixed-script
samples for fonts. Label the WPF scene Quick preview and keep Preview in viewer
as the exact native action. Before/after uses the same sample and dimensions.

Required model work includes explicit role/reference graphs, hex/RGBA and
alpha-aware contrast, original/derivative recipes, native/top-origin slice
conversion, real widget state bindings, metadata/history, read-only future
projects, reset/diff and safe project copying. Runtime packages remain version 1;
authoring-only schema changes require migration and preserve earlier projects.

Full palette, image, font, widget, library and package features remain in scope,
followed by host/Preferences/launcher/distribution integration, Folderstorm base
support and the separately planned base-independent phase. This design note does
not declare those remaining features implemented or waive final acceptance.

The image model now supports immutable crop, resize, tint, brightness/contrast/
saturation/gamma/alpha adjustment, fill, gradient, border and sliced stretch
previews. Bilinear interpolation uses premultiplied alpha and fixed slice regions
do not bleed into their neighbors. Editing is bounded to 16 megapixels; PNG
encoding is bounded to 32 MiB. WPF codec tests preserve channel order, alpha,
hidden RGB and inherited button pixels. The image workspace exposes these
operations with scoped derivative storage, retained originals and undo history.
Model/codec checks remain distinct from the recorded human UI checks.

The widget workspace uses the same three-pane workbench: searchable controls on
the left, one state preview in the middle and the selected property's inspector
on the right. File selection stays above it. Show protected properties read-only,
current versus inherited values, and exact source provenance. Color fields use
the shared RGBA picker; named colors and image IDs retain their native meaning.
Set property is explicit, Reset property restores the recorded base and Reset
file affects only that document. Before/after samples use the same state and
dimensions and remain labelled as WPF approximations. The actual viewer remains
the exact resource/behavior check. The initial UI targets approved widget defaults
and the native sample document; explicit rules must expand before other files
become editable. Do not flatten unrelated defaults or rewrite required bindings.

The image browser now searches IDs/filenames and previews TGA directly, including
uncompressed/RLE truecolor, palettes, grayscale, origins and alpha. The decoder
retains source bytes and uses the native reader's 32-bit-alpha convention even
when descriptor bits are zero. All 1641 bundled nonempty TGA files match Pillow's
decoded RGBA bytes; the repository's empty placeholder is rejected explicitly.
This validates decoded pixels, not native widget rendering. Interleaved TGA is
reported as unsupported. PNG remains the package replacement format until the
shared native/import validation is expanded.
