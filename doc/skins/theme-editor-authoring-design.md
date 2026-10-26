# Authoring stage

This branch starts at the frozen Gate A candidate `97c217e00f`. The matching
native 82310 build remains separate. Full WPF expansion waits for its native
state checks; independent authoring models can be prepared without altering it.

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
not declare any of those features implemented or waive Gate A.
