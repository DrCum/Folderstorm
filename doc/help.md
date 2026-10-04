# Using Folderstorm

This is the practical guide: what you can do in the viewer, how to turn on the local assistant bridge, and how to connect Cursor, Codex, and Claude Code. The full tool list is in [tools/fs-mcp/README.md](../tools/fs-mcp/README.md).

## In the viewer

### Login and identity

The window title, preferences tab, icons, and crash reports use the name Folderstorm. The login screen shows the winged folder, splash art from folderstorm.sodie.net, a purple Log In button, and the I'm Sodie feed.

### Wide-screen layout

Open **Preferences → Move & View → Layout**.

- **Viewport.** Turn on **Use a custom world viewport** and set the left, right, top, and bottom margins. The camera, HUD, and mouse stay in the remaining world area so Inventory and other windows can sit in the margins of a stretched window. If two opposite margins would leave less than 5% of the window, they are scaled down. The change previews immediately. There is a separate switch for mouselook.
- **Layout.** Place the toolbars, chat bar, and top bars around that viewport. Side bars sit outside the viewport when there is room, and just inside it when there is not. Save the arrangement as a named profile, then apply, rename, or delete profiles later. **Reset** restores the default chrome.

### Outfit Gallery

Open the Outfits panel and the gallery tab. The thumbnail slider sets the photo width from 80 to 256 pixels. The name row stays a fixed height. 256 is the photo size the gallery fetches.

### Workspaces

Open **Preferences → Move & View → Workspaces** after login. Arrange your windows and choose **Save current**. Workspaces are stored separately for each account. They include viewport and bars, supported Inventory/map windows, compatible chat geometry, and the primary Inventory's Received Items panel size. Preferences and the workspace switcher are excluded. **Layout** and **Workspaces** are the first two Move & View tabs.

**Preview** changes the arrangement immediately. Preferences **OK** keeps the arrangement and saved definitions; **Cancel** restores the previous arrangement. Conversations, Inventory contents and filters stay as they are. Additional open Inventory windows are saved too (up to 16, plus the workspace-owned extra). Loading restores their size, position, visibility and minimized state, reuses existing windows and hides extras absent from that workspace. Folder selections and filters are not saved. **Add workspace inventory window** remains available for creating the original workspace-owned extra. Resave older workspaces to include additional Inventory windows; older saves restore no additional windows. Workspaces are restored manually, rather than automatically at login.

For quick switching, choose **World → Workspaces & Layouts…**, or drag **Workspaces** from **Toolbar Buttons** to any toolbar. Double-click an entry or choose **Switch**. **Favorite** puts entries in a separate Favorites section at the top of this list; landmark favorites remain in their own bar. Workspaces include supported window arrangements; Layouts change only the viewport and bars. Saved workspace definitions stay unchanged when switching. Enter a name under **Save current as a new workspace** and choose **Save** to keep the current setup immediately, without opening Preferences. This creates a new workspace; replacing an existing name is handled in Preferences. Finish Preferences with **OK** or **Cancel** before switching or saving from the list. **Preferences…** opens the selected entry's Workspaces or Layout settings.

Tabbed **Conversations** restores size and position only; loading a workspace leaves its open/closed state and selected conversations unchanged. Detached **Nearby Chat** can restore visibility when its existing presentation is compatible. Workspace capture never stores messages, chat sessions, or inventory contents. The included workspace starting arrangements have been removed; your named workspaces remain available.

Enable **Show workspace favorites strip** in the Workspaces switcher or Preferences → Move & View → Workspaces to show a separate row beneath the navigation/landmark bars. It uses the same account favorites as the switcher, labels Workspaces and Layouts separately, and places overflow entries in **More…**. More also opens the switcher or hides the strip. The row hides in Mouselook and when the viewer UI is hidden; switching is disabled while Preferences is open.

Settings search also recognizes **MCP**, **dual monitor**, **workspace**, and **undo**. Matching tabs open automatically. Undo leads to **Local assistant → History**, where **History and undo…** opens the existing inventory review window. Searching or opening history does not authorize or execute inventory changes.

### Review Inventory changes

Select 1–50 items in the Inventory list, then use the gear menu's **Review bulk changes…** to preview a rename pattern or move to an existing folder. Review the exact before/after rows and skipped items before executing. In that window, enable **Review assistant rename/move operations affecting 10 or more objects** to also review large assistant batches.

The gear menu's **Assistant inventory history…** shows this session's tracked rename/move operations. **Stop** prevents additional work; submitted changes may still finish. **Clear history** removes the visible records without changing Inventory. Undo is offered only for confirmed changes that still match their recorded state; ordinary renames and copyable-item moves have narrower rules than general Inventory actions. Unconfirmed changes, folder moves, moves involving Trash, and non-copyable-item moves cannot be undone here. See [bulk review and recovery](local-assistant-bulk-inventory.md) for the exact limits.

## Settings from Firestorm

Folderstorm keeps its own settings folder. It does not read Firestorm's, and a normal install does not copy it unless you say so.

| OS | Firestorm | Folderstorm |
| --- | --- | --- |
| Linux | `~/.firestorm_x64/user_settings` | `~/.folderstorm_x64/user_settings` |
| Windows | `%APPDATA%\Firestorm_x64\user_settings` | `%APPDATA%\Folderstorm_x64\user_settings` |
| macOS | `~/Library/Application Support/Firestorm/user_settings` | `~/Library/Application Support/Folderstorm/user_settings` |

Toolbar layout is not in `user_settings`. The viewer loads `toolbars.xml` from the per-account folder next to it, for example `~/.firestorm_x64/first_last/toolbars.xml` into `~/.folderstorm_x64/first_last/toolbars.xml`. The same account name is used on Windows under `%APPDATA%\Firestorm_x64\` and `%APPDATA%\Folderstorm_x64\`, and on macOS under Application Support. A grid other than Second Life keeps its suffix, such as `first_last.osgrid`.

On Linux, `FOLDERSTORM_X64_USER_DIR` replaces the Folderstorm folder, and `FIRESTORM_X64_USER_DIR` is where a Firestorm install keeps its own. macOS settings stay in `Folderstorm` with no `_x64`. That suffix is only the cache.

The program is `migrate-settings` (`migrate-settings.exe` on Windows). Double-click it, or run it from a terminal. Saved passwords, cookies, assistant tokens, caches, and logs are not copied. If Folderstorm already has a settings folder, the copy is still offered. Files that are already there are kept unless you pass `--overwrite` or confirm the second prompt. Choosing not to replace them still copies settings Folderstorm does not have yet.

Account folders are a separate question. The whole folder is copied, including `toolbars.xml`, only if you accept. An existing Folderstorm account folder is left unchanged unless you pass `--overwrite-accounts` or confirm that folder.

The Windows NSIS installer and a Velopack install ask after the files are in place, for the Windows account that is running the installer. They ask even when the Folderstorm settings folder already exists. If some of those files are already present, they ask again before replacing them. They then ask separately about account folders. `install.sh` on Linux asks at the end of the install. Pass `--migrate-settings` and, for account folders, `--migrate-accounts` when the install itself is non-interactive. The macOS disk image does not ask: that script runs while the image is built, not on your Mac. After you copy Folderstorm to Applications, run:

```bash
"/Applications/Folderstorm.app/Contents/Resources/migrate-settings"
```

From the Linux package next to `install.sh`, or from the Windows install folder, run `migrate-settings` or `migrate-settings.exe`.

`--yes` copies settings without the first question. `--accounts` copies account folders. `--dry-run` prints the plan and writes nothing. `--check` is what the installers use for settings: it exits 0 when a copy does not need to replace a file, 2 when there is nothing to copy, and 3 when Folderstorm already has some of those files. `--check-accounts` is the same check for account folders.

## The local assistant bridge

The bridge lets an MCP client on this computer read and change inventory, change your outfit, move the camera, and take a picture. It does not teleport, chat, or reach the Marketplace. It stays off until you turn it on.

### 1. Turn the bridge on

Open **Preferences → Local assistant** and enable **Allow local assistant access**. Choose **Read only**, **Ask before changes**, or **Custom** permissions. Selecting Custom exposes individual controls; it does not restore an earlier set of values. Existing saved permissions are kept until you choose a preset or change a control.

Changes preview immediately and are saved when you click **OK**. **Use these settings for this session only** keeps both access and permissions temporary; the saved settings resume after restart. **Cancel** restores the previous settings. The viewer listens on `127.0.0.1` and writes a discovery file named `fs-mcp-<pid>.json`. The file can exist at the login screen, before inventory is usable. Log in and wait until inventory has loaded before asking the assistant to use it.

`--mcp-api` still forces the bridge on for that one session. It does not save the preference. Quit and the next launch follows the saved checkbox.

Turning the switch off deletes the discovery file and rejects requests. After the first enable in a session, the port stays bound until you quit. Enabling it again keeps that port and writes a new token. Quit deletes the file.

The discovery file is a bearer secret. Another program running as this user can read it while the switch is on. Leave it on disk for the sidecar, and do not paste it into chat, tickets, or a remote machine.

| OS | Discovery file directory |
| --- | --- |
| Linux | `~/.folderstorm_x64/user_settings` |
| Windows | `%APPDATA%\Folderstorm_x64\user_settings` |
| macOS | `~/Library/Application Support/Folderstorm/user_settings` |

On Linux you can point the viewer at another directory with `FOLDERSTORM_X64_USER_DIR`.

### 2. Point a client at the sidecar

The installer ships `fs-mcp` in the same folder as the viewer and `migrate-settings`. On Windows that file is `fs-mcp.exe`. The viewer does not start it. The installer does not write a config for Cursor, Claude Code, or Codex.

On Windows, use **Preferences → Local assistant → Setup → Copy configuration** in the installation you want to connect. New installers do not create or retarget a shared ProgramData command. Each installation keeps its own sidecar. Codex and Claude Code use that absolute executable path; Cursor's entry includes a quoted `cmd.exe` adapter when the path contains spaces.

A normal Velopack installation uses its stable `current\fs-mcp.exe` path. Setup verifies the package layout/id/version before describing that command as following updates. If the location is unverified, Setup uses the actual executable and asks you to recopy after updating. If the installed package changed while this viewer is running, restart before copying. Nonadmin installs need no writable ProgramData directory or symlink privilege for MCP configuration.

Older configurations may still use `C:\ProgramData\Folderstorm\fs-mcp.exe`. That legacy alias is kept unchanged during new installs and updates. Uninstall removes only file symlinks independently proven to target the uninstalling installation; unverified/busy links and unrelated files remain untouched. Old installers can still change the alias, so recopy existing client entries from their intended viewer's Setup page. No client files are rewritten automatically.

Typical Release paths are below; private/OpenSim channels use their own installation directory.

| Install | `fs-mcp.exe` beside the viewer |
| --- | --- |
| Velopack, Release | `%LocalAppData%\Folderstorm-Release\current\fs-mcp.exe` |
| NSIS, Release | `%ProgramFiles%\Folderstorm-Release\fs-mcp.exe` |

Set `FIRESTORM_MCP_DISCOVERY` to the Folderstorm user_settings directory, `%APPDATA%\Folderstorm_x64\user_settings`. For an account named Jane that directory is `C:\Users\Jane\AppData\Roaming\Folderstorm_x64\user_settings`. Use that Folderstorm directory. Firestorm's settings live in `%APPDATA%\Firestorm_x64\user_settings`, and that path will not see this viewer. When the variable is set, it replaces the default search, which also looks in Firestorm folders. It may be one file, one directory of `fs-mcp-*.json` files, or a `;`-separated path list.

On Linux and macOS the program is the `fs-mcp` binary beside the viewer: next to `install.sh` on Linux, and `Folderstorm.app/Contents/Resources/fs-mcp` on macOS. Set `FIRESTORM_MCP_DISCOVERY` to that platform's Folderstorm user_settings directory from the table above.

Turn **Local assistant** on before you connect. With the bridge off, the sidecar finds no viewer.

To build the binary yourself instead, install Go 1.25 or newer, then:

```bash
cd tools/fs-mcp
go build -o fs-mcp ./cmd/fs-mcp
```

Windows output name: `fs-mcp.exe`. Use that binary's absolute path in your client entry. The sidecar speaks MCP over stdin and stdout. It has no port of its own.

`FIRESTORM_MCP_TIMEOUT` is how long one viewer call may take. The default is `90s`.


### Copy configuration and check this viewer

Open **Preferences → Local assistant → Setup**, choose Codex, Cursor, or Claude Code, then **Copy configuration**. Merge the entry into that client's file; keep your existing servers. Codex receives a TOML table; Cursor and Claude Code receive a JSON `mcpServers` entry. This uses the current installation and the viewer's actual settings directory, including directory overrides. It copies no discovery contents or bearer token and writes no client files.

On Windows copied commands always belong to this installation; a shared ProgramData alias is never selected. Cursor uses an explicit quoted `cmd.exe` adapter with the executable in `FOLDERSTORM_MCP_BINARY` when that path contains spaces. Legacy aliases are not modified during setup/install/update. On Linux the installed sidecar is at the package root, one directory above the viewer's `bin` directory; on macOS it is in `Contents/Resources`.

**Check connection** runs the installed sidecar asynchronously against this viewer. It checks authenticated bridge health even when inventory Read is Never. A successful check means the sidecar could reach this viewer at that moment; it does not mean your external assistant loaded its configuration. Inventory and recent authenticated external requests are shown separately. Diagnostic probes do not count as external assistant activity. Closing preferences or leaving the page cancels a running check; changes to bridge readiness or permissions clear an old result.

For a terminal check, use the installed executable:

```text
fs-mcp --diagnose --viewer-pid 12345 --discovery "/absolute/path/to/user_settings"
```

Replace the PID and directory with this viewer's values (the discovery filename includes its PID). The command emits bounded JSON with fixed failure stages and readiness facts, never the bearer token or raw server errors. Exit status is 0 on success, 1 when the check fails, and 2 for invalid arguments/output failure. HTTP checks have a four-second timeout; the viewer terminates its subprocess after five seconds. A pre-login viewer can pass bridge health while inventory is unavailable. Running `fs-mcp` with no arguments still starts the MCP stdio server.

## Connect a client

Cursor, Claude Code, and Codex each keep their own file. `mcp.json`, `.claude.json`, and `config.toml` are three configs. Adding the server in one of them leaves the other two unchanged. All three run the same exe.

The server name is `folderstorm`. Tool names inside the sidecar stay `inventory_search`, `camera_snapshot`, and so on. Claude Code shows them to you as `mcp__folderstorm__inventory_search`.

The snippets show an NSIS Release installation under `C:\Program Files\Folderstorm-Release`. Use your installation's copied entry for Velopack or another channel. Leave `FIRESTORM_MCP_DISCOVERY` on `%APPDATA%\Folderstorm_x64\user_settings`. For an account named Jane that is `C:\Users\Jane\AppData\Roaming\Folderstorm_x64\user_settings`.

### Cursor

Open **Settings → MCP**, or edit `%USERPROFILE%\.cursor\mcp.json`. The entry is `mcpServers.folderstorm`, with `command` and `env`.

```json
{
  "mcpServers": {
    "folderstorm": {
      "command": "cmd.exe",
      "args": ["/d", "/s", "/c", "\"\"%FOLDERSTORM_MCP_BINARY%\"\""],
      "env": {
        "FOLDERSTORM_MCP_BINARY": "C:\\Program Files\\Folderstorm-Release\\fs-mcp.exe",
        "FIRESTORM_MCP_DISCOVERY": "%APPDATA%\\Folderstorm_x64\\user_settings"
      }
    }
  }
}
```

A single project can use `.cursor/mcp.json` in that project. That file is separate from `%USERPROFILE%\.cursor\mcp.json`. Restart the MCP server from the Cursor MCP panel after you edit the file. Cursor does not use a `type` field for a local command.

### Claude Code

User scope writes `%USERPROFILE%\.claude.json`. Flags go before the server name. The double dash separates the sidecar command:

```bat
claude mcp add --transport stdio --scope user folderstorm -- "C:\Program Files\Folderstorm-Release\fs-mcp.exe"
```

For a single project, use `.mcp.json` in that project's root. That file is approved the first time the folder is opened. The user-scope file remains `%USERPROFILE%\.claude.json`. Claude Code does not read `%USERPROFILE%\.claude\mcp.json`.

If that `claude` command has no environment flag, add the env block on the `folderstorm` server in the file it wrote:

```json
"env": {
  "FIRESTORM_MCP_DISCOVERY": "%APPDATA%\\Folderstorm_x64\\user_settings"
}
```

A project `.mcp.json` looks like this:

```json
{
  "mcpServers": {
    "folderstorm": {
      "type": "stdio",
      "command": "C:\\Program Files\\Folderstorm-Release\\fs-mcp.exe",
      "args": [],
      "env": {
        "FIRESTORM_MCP_DISCOVERY": "%APPDATA%\\Folderstorm_x64\\user_settings"
      }
    }
  }
}
```

Check with `claude mcp list`.

### Codex

Codex is ChatGPT's coding client. `codex mcp add` writes `%USERPROFILE%\.codex\config.toml`. The table name is `mcp_servers`.

```bat
codex mcp add folderstorm -- "C:\Program Files\Folderstorm-Release\fs-mcp.exe"
```

The file should contain the command and the Folderstorm discovery directory:

```toml
[mcp_servers.folderstorm]
command = 'C:\Program Files\Folderstorm-Release\fs-mcp.exe'
startup_timeout_sec = 20
tool_timeout_sec = 120

[mcp_servers.folderstorm.env]
FIRESTORM_MCP_DISCOVERY = '%APPDATA%\Folderstorm_x64\user_settings'
```

If `codex mcp add` writes the command and leaves out the env table, add that table in `%USERPROFILE%\.codex\config.toml`. `codex mcp list` shows what was saved.

The ChatGPT desktop app is separate. Add the sidecar by hand in that app's connector settings. The desktop app does not read `%USERPROFILE%\.codex\config.toml`.

A project file `.codex/config.toml` is loaded only when that project is trusted. If the server appears in the file and Codex ignores it, trust the project or put the same table in `%USERPROFILE%\.codex\config.toml`.

`tool_timeout_sec` should stay above the sidecar timeout (default 90 seconds). Inventory reads wait for the viewer to fetch objects, and a short client timeout cancels them first. The sidecar itself starts immediately, so the startup timeout only needs to cover process launch.

### What is the same everywhere

- Stdio transport with an installation-bound command. Cursor on Windows may use the quoted adapter shown above. On Linux and macOS it is the `fs-mcp` binary beside the viewer.
- The same tool names and arguments.
- `FIRESTORM_MCP_DISCOVERY` set to this viewer's Folderstorm `user_settings` directory. Each client still has its own file.
- **Local assistant** is on. With the bridge off, the sidecar finds no viewer.
- Ask is a dialog in the viewer. The tool call stays in flight until you answer, or for 60 seconds, and then it is denied. A client that auto-approves `confirm_action` does not skip that dialog.

## Asking for something

Log in first. A good first request is "check the viewer status." `usable` should be true.

Then, in ordinary language:

- "List my outfits and tell me what I am wearing."
- "Search inventory for the shirt named dusk, under the Clothing folder."
- "Make a folder called Studio shots in Textures and move these two textures into it." Use UUIDs when two items share a name. Ask the assistant to resolve the path and show you the matches before it moves anything.
- "Wear the outfit named Portrait, then wait until the outfit is no longer dirty, frame a portrait, and take a picture with the HUD hidden."

Wear, detach, replacing links, and moving no-copy items during a copy ask in the viewer unless that permission is Allow. A no-copy copy copies the copyable pieces first. Allow on that row moves the unique items out of the source with no dialog. Ask waits for the viewer dialog. Never leaves those unique items in the source.

Trash is its own permission, default Allow, and the preference says Trash can be restored. Restore follows Move and copy, so Ask or Never on Trash does not block it. Permanent delete is not possible. The bridge hard-denies purge and empty Trash. There is no Ask and no preference row that can enable them. A hand-edited setting cannot turn them on. Those tools fail immediately, with no dialog.

If an older viewer has no permission list, wear, detach, link replacement, and no-copy copies still use the sidecar confirmation. `confirm_action` and `inventory_confirm_copy` are that older path. Purge and empty Trash fail on those viewers too.

Baking continues after a wear call returns. Take the snapshot after `appearance_worn` reports the outfit is clean, or the picture can show the previous outfit.

Camera presets are `portrait`, `full_body`, `front`, `back`, `left`, and `right`. An exact shot uses region coordinates: where the camera stands, and the point it looks at.

`camera_snapshot` can take `width` and `height`. When both are set, the picture is that size, center-cropped to the ratio. A square is the same number on both sides. Each side is clamped from 64 to 2048. Omit them and the picture stays the whole window, scaled so the long edge is at most `max_edge` (default 1024). `viewport_only` uses the world view instead of the full window. It defaults to false, so existing calls stay the same. UI and HUD stay hidden unless you ask for them. The tool still returns a temp JPEG and does not upload.

`inventory_snapshot_upload` takes the outfit, folder, or item id and sets that image. `width` and `height` default to 1024. `viewport_only` defaults to true. `destination` is `thumbnail` unless you pass `texture`. A thumbnail is free, follows Rename and edit details, and is scaled down to 256. Allow on that row uploads it with no dialog. A texture creates a real inventory texture and then sets the folder image. It follows Create folders and items. The viewer quotes the L$ price first. Never refuses the upload. Ask shows the L$ amount. Allow uploads with no dialog only when that price is L$0. Allow with a price above L$0 asks and shows the amount. OpenSim can quote L$0. `inventory_set_thumbnail` still only points at an existing texture and does not spend L$. Allow does not spend L$.

If two viewers are open, say which one, or ask for the process list and pick a pid.

## When it does not connect

- The local assistant switch is off. Turn it on under **Preferences → Local assistant**. With the bridge off, the sidecar finds no viewer. `--mcp-api` forces one session on without saving that choice. You do not need to quit and relaunch.
- `FIRESTORM_MCP_DISCOVERY` points at Firestorm's user_settings directory, or at any folder other than this viewer's Folderstorm `user_settings`. That variable replaces the default search. On Windows set it to `%APPDATA%\Folderstorm_x64\user_settings` (for an account named Jane, `C:\Users\Jane\AppData\Roaming\Folderstorm_x64\user_settings`). On Linux and macOS use the Folderstorm directory in the table above.
- Inventory is still downloading. `viewer_status` shows fetch progress. Wait until `usable` is true.
- The command points at another installation or a retired shared alias. Recopy the entry from this viewer's Setup page; Cursor needs the provided adapter for paths containing spaces.
- Codex is using a project `.codex/config.toml` in an untrusted folder, the table is named `mcpServers`, or the ChatGPT desktop app was expected to read `%USERPROFILE%\.codex\config.toml`.
- Claude Code's file has no `env` block. If `claude mcp add` has no environment flag, add `FIRESTORM_MCP_DISCOVERY` in the file it wrote (`%USERPROFILE%\.claude.json`, or `.mcp.json` for one project). Flags that do exist go before `folderstorm`.
- A tool dies around 10–30 seconds while inventory is still loading. Raise the client tool timeout above 90 seconds, or set `FIRESTORM_MCP_TIMEOUT` to match a timeout you accept.

The discovery file contains the bearer token. Leave it on disk for the sidecar, and do not paste it into chat, tickets, or a remote machine.
