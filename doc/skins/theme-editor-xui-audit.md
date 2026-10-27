# XUI adapter audit

The retained0.13.0 editor supports three exact English paths. The next stage
must expand a checked list of widget templates and control-specific properties.
Keep the ordered control tree, required names, bindings and behavioral attributes
unchanged. Login, consent, security and update documents stay excluded.

Attribute presence matters. The former C++ map lookup inserted absent values,
rejecting an allowed width removal and overlooking an empty protected attribute.
Lookup now preserves presence. Text and attribute limits both count4096 UTF-8
bytes. Five focused model groups and15 shared revision fixtures pass against the
corrected standalone validator. A matching viewer build remains required.

The next property audit uses the actual LLUI Params declarations and widget XML:

| Area | Source | Next coverage |
| --- | --- | --- |
| Geometry and anchoring | `llview.h`, `llview.cpp` | Rect edges, relative pads/deltas, follows/layout with explicit value rules |
| Shared control labels/fonts | `lluictrl.h`, `llui.cpp` | Label, font and native `font.style`/`font.size` subparameters |
| Buttons | `llbutton.h`, `widgets/button.xml` | Exact image-state names, image tints, label colors and padding |
| Windows/panels | `llpanel.h`, `llfloater.h` | Background artwork, opacity/tints, header font and supported geometry |
| Inputs/text | `lllineeditor.h`, `lltextbase.h` | Normal/focused/disabled artwork, text/cursor colors, padding and line spacing |
| Tabs | `lltabcontainer.h`, `widgets/tab_container.xml` | First/middle/last state images and tab label geometry |
| Scrollbars/lists | `llscrollbar.h`, `llscrolllistctrl.h` | Track/thumb artwork, state colors and supported spacing |
| Check/radio | `llcheckboxctrl.h`, `llradiogroup.h` | Their actual nested button/label parameters, preserving bindings |
| Menus | `llmenugl.h` | Text/highlight colors, artwork and spacing; callbacks remain protected |

Two existing spelling assumptions need correction before broader samples are
accepted. A disabled selected button uses `label_color_disabled_selected`.
LLUI font parameters use `font.style`; `font_style` belongs to the font registry
format, not the widget parameter. Do not advertise an ignored property as an edit.

English XUI uses `CURRENT_SKIN`: the last matching English document is a complete
replacement. A selected locale can then update that document through
`LLXMLNode::getLayeredXMLNode`. Keep exact English replacement semantics and
capture locale dependencies before exposing other locales. Widget template
defaults are read by `LLUICtrlFactory::loadWidgetTemplate`; do not synthesize
missing defaults by flattening unrelated controls.

Before exposing expanded rules, add control-specific checks in both validators,
reference/enum/range fixtures and an exact rules fingerprint in the viewer
capability receipt. The editor should reject a mismatched viewer before starting
a preview containing XUI. Rebuild and retain the native viewer privately, then
check representative widget states and actual windows. Standalone validation and
WPF samples do not establish native appearance or system-DPI acceptance.
