# Folderstorm Violet and Electric Jungle

Choose **Preferences → Skins → Skin: Folderstorm**, then select either new
theme. Click OK and restart the viewer, as with the existing themes.
The current theme remains the default; both additions are opt-in.

**Folderstorm Violet** follows folderstorm.sodie.net: near-black purple
surfaces, lavender and violet accents, off-white text and warm gold highlights.
**Electric Jungle** uses dark plum surfaces, magenta headers, cyan outlines,
violet buttons, lime list/menu selections and small patterned frame accents.
Inventory uses a darker green selection so library entries, links and favorite
text remain readable; those entries have separate foreground-color rules.

Both inherit the existing Folderstorm/Firestorm layout, language files and
icons. They supply colors and chrome for windows, navigation, buttons, tabs,
dropdowns, text fields, scrollbars, sliders, progress bars, checkboxes and radio
buttons. Their illustrative selector previews are UI samples, not viewer
screenshots. The mockup's spacing and window contents are not layout changes.
World rendering, map/status warning colors and existing skin defaults are
unchanged. The normal Skins preference for resetting toolbar arrangements
still applies; turn it off before changing themes to retain custom toolbars.

## Authoring

Generated resources are committed, so building or running the viewer does not
require Pillow. To regenerate them, use Python 3 and Pillow >= 10.1:

```sh
python3 scripts/skins/generate_folderstorm_themes.py
```

The script draws original assets from palette definitions. It reads the
inherited images only for dimensions, preserves their filenames and existing
nine-slice registrations, and excludes icons and animated loading sprites.
The existing viewer manifest globs already include every added XML/PNG file;
no packaging or application-code changes are needed.

## Focused validation

Checked with Pillow 12.3.0:

- Both registered theme names, their folders and globally available preview
  image names match the existing skin selector's lookup rules.
- The four changed/new XML files parse. Each theme has 119 unique color names;
  all non-palette overrides are existing color roles and references resolve.
  Numeric colors have four components in the valid range.
- Each theme's 117 RGBA chrome textures has the corresponding inherited
  dimensions and nonempty content. All theme assets and previews match the
  existing packaging wildcard rules without running packaging.
- Main text against panel, input, selected-button, title and Inventory fills;
  muted text against panels; selected Inventory links/favorites; and Electric
  Jungle menu/list text against lime all exceed 4.5:1 in the opaque palette.
  Minimum checked ratios: Violet 4.65:1, Electric Jungle 6.05:1. User transparency
  settings and native overlays still need visual acceptance.
- Regeneration produces identical resources with the installed Pillow version.
- The complete diff passes whitespace checks; the authored PNGs were inspected.

No viewer build, packaging, GitHub builds or unrelated tests were run.

## Native acceptance still needed

On Windows, select each theme, confirm with OK and restart. Check Inventory
including selected links/library/favorites, ordinary and minimized floaters,
Preferences, chat, dropdown arrows, selected/disabled controls and active versus
inactive windows. Confirm navigation and all toolbar orientations, including
the shared workspace/landmark row when PR #5 is installed. Try larger UI scale
and window resizing for stretch artifacts. Canceling a selection should keep
the old skin, and switching back to an existing theme should restore its look.
