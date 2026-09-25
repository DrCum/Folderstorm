# Building the packaged sidecar

The viewer build compiles two Go programs and copies both into the install folder. Install Go 1.25 or newer and make sure `go` is on `PATH` before configuring. Platform steps are in [building_windows.md](building_windows.md), [building_macos.md](building_macos.md), and [building_linux.md](building_linux.md).

| Program | What it is | Where the package puts it |
| --- | --- | --- |
| `migrate-settings` | Settings copy the installer can run | Same folder as below |
| `fs-mcp` | Local MCP sidecar | Next to the viewer, beside `migrate-settings` |

On Windows the file is `fs-mcp.exe`. A Velopack install of the Release channel puts it at `%LocalAppData%\Folderstorm-Release\current\fs-mcp.exe`, in the same directory as the viewer executable. The legacy NSIS installer puts it at `%ProgramFiles%\Folderstorm-Release\fs-mcp.exe`. On Linux the file is `fs-mcp`, next to `install.sh`. On macOS it is `Folderstorm.app/Contents/Resources/fs-mcp`.

The viewer does not start `fs-mcp`. The local assistant stays off until it is enabled in Preferences. The installer does not write an MCP config into Cursor or another app. Point the client at the installed binary. See [help.md](help.md).
