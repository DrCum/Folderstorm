# Folderstorm theme editor

Based on the exact PR #88 head. The native architectural slice is retained and
validated at the observed desktop scale; full authoring and final acceptance are
still in progress. See `doc/skins/theme-editor-gate-a-validation.md` and
`doc/skins/theme-editor-authoring-design.md` for evidence and remaining scope.

The palette workbench includes named links, hex/RGBA and alpha, before/after WPF
samples, metadata/history and Save As. Images searches IDs/filenames and previews
PNG/TGA with a checkerboard. WPF samples approximate appearance; Preview in viewer
uses the isolated native sample. Set color applies an edited field before Save
or Save As. The image workspace includes crop/resize/tint/adjustments, generators,
slice guides, stretch/tile samples and image/family/all-image reset. Reset keeps
stored originals and history; Undo restores the edits. Future projects and
unsupported operations open read-only.

Palette tools offers Violet, Mint and Paper starting presets. Ctrl/Shift selects
colors for bulk Set color, Link to color or Reset checked. Preview lists direct
edits and effective before/after values, including linked followers. Apply uses
one Undo step and leaves other project edits intact. Changing options invalidates
Apply until another preview. Native-only inherited values are preserved when
resetting. The normal palette sample remains an approximation of viewer artwork.

Import image accepts PNG, TGA, BMP and JPEG. PNG bytes are kept directly; other
formats become a PNG while the exact imported bytes and extension remain in
the project. The import applies to the selected logical ID and is undoable.
When inherited guides are invalid, Reset guides explicitly uses the full image
and zero slice margins so editing can continue. Artwork bytes stay intact and
Undo restores the original guides.

Images also has Pen, Line and Circle pixel tools. Zoom in for the optional grid;
drag a circle from center to edge. Release commits one undoable gesture; Escape
cancels. Pick buttons open the shared color/opacity picker beside palette, tint,
generator and drawing-color fields. Picking a tint does not apply it until Apply
tint is clicked.

The picker also keeps up to 32 local saved swatches, including opacity. Save
swatch stores a color independently of project edits; selecting a saved swatch
restores it. Screen pick chooses a screen RGB value while keeping the current
opacity. Escape or right-click cancels. This candidate still needs a manual
physical-mouse RGB check: automated sampling did not match the captured swatch.
The picker scrolls on short displays and keeps Cancel/Use color visible.

In Images, expand External editor, choose an installed editor application, and
Open copy. Save the separate PNG in that editor, then Reload in the lab. Reload
creates one undoable edit and preserves original artwork. A file watcher only
reports changes; it never imports automatically. Reopening the same project and
image with Open copy resumes a compatible saved working copy. Conflicting image
edits require a new copy; old working files remain in Working files. Application
preferences and working sessions stay local and are excluded from exports.

Edit family shows related image states with checkboxes and Before/After samples.
Choose Tint, Adjust, Resize or Guides from current, then Preview checked. Apply
updates only changed checked states in one Undo step. Unchecked states retain
their artwork; each edited state keeps its own imported original and recipes.
Changing settings invalidates the preview. Unsupported legacy guides are shown
with a reason and cannot be checked. Cancel does not change the project.

Use .NET 10 on Windows. The editor is x64 WPF, self-contained and non-trimmed;
core and CLI have no UI or third-party package dependencies.

```powershell
./build.ps1 -Test
dotnet publish Folderstorm.Theme.Editor -c Release -o artifacts/editor
```

Pass `-Dotnet <portable-dotnet.exe>` and `-DevelopmentCache <directory>` to use a
private SDK/package cache. The script does not alter the global SDK or PATH.

CLI examples, from this directory:

```powershell
dotnet Folderstorm.Theme.Cli/bin/Release/net10.0/Folderstorm.Theme.Cli.dll create artifacts/MyTheme 'My theme' --base ../..
dotnet Folderstorm.Theme.Cli/bin/Release/net10.0/Folderstorm.Theme.Cli.dll set-color artifacts/MyTheme ThemeAccent '0.2 0.8 0.5 1' --base ../..
dotnet Folderstorm.Theme.Cli/bin/Release/net10.0/Folderstorm.Theme.Cli.dll export artifacts/MyTheme artifacts/MyTheme.fstheme --base ../..
```

For exact preview, author against the matching viewer's resource folder, then
select its executable in the editor. Projects retain original imports, recipes
and undo checkpoints. Runtime package imports start a separate compiled-override
project. Importing never applies a theme to all profiles.

`contracts/FORMAT.md` documents the runtime package and project contracts. Build
the separate `native/` CMake project with the existing Boost include directory;
it does not configure/download the viewer or require .NET. Replay shared cases:

```powershell
python contracts/check-fixtures.py --dotnet dotnet --cli Folderstorm.Theme.Cli/bin/Release/net10.0/Folderstorm.Theme.Cli.dll --native artifacts/native/Release/theme_validate.exe
```

The independent validator's `revision <directory> <viewer-resource-root>` command
checks actual payload and inherited bytes, references and protected XUI structure.
Neither a green contract test nor a WPF screenshot establishes native appearance.

`integration/Workbench/WorkbenchProbe.csproj` exercises the production WPF reset
handlers, selected-image canvas and override-filtered Undo/Redo without showing
windows. Pass the matching viewer resource root as its only argument. This is a
focused UI-state regression check, separate from human visual acceptance.
