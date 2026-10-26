# Folderstorm theme editor

Gate A work in progress, based on PR #88. The native preview integration needs
its first matching viewer build and visual validation; this is not a completed
feature or a release candidate. See `doc/skins/theme-editor-progress.md` for the
full authorized sequence and current evidence.

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
