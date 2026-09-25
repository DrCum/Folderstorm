# Using Folderstorm

This is the practical guide: what you can do in the viewer, how to turn on the local assistant bridge, and how to connect Cursor, Codex, and Claude Code. The full tool list is in [tools/fs-mcp/README.md](../tools/fs-mcp/README.md).

## In the viewer

### Login and identity

The window title, preferences tab, icons, and crash reports use the name Folderstorm. The login screen shows the winged folder, splash art from folderstorm.sodie.net, a purple Log In button, and the I'm Sodie feed.

### Wide-screen layout

Open **Preferences → Move & View**.

- **Viewport.** Turn on **Use a custom world viewport** and set the left, right, top, and bottom margins. The camera, HUD, and mouse stay in the remaining world area so Inventory and other windows can sit in the margins of a stretched window. If two opposite margins would leave less than 5% of the window, they are scaled down. The change previews immediately. There is a separate switch for mouselook.
- **Layout.** Place the toolbars, chat bar, and top bars around that viewport. Side bars sit outside the viewport when there is room, and just inside it when there is not. Save the arrangement as a named profile, then apply, rename, or delete profiles later. **Reset** restores the default chrome.

### Outfit Gallery

Open the Outfits panel and the gallery tab. The thumbnail slider sets the photo width from 80 to 256 pixels. The name row stays a fixed height. 256 is the photo size the gallery fetches.

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

Open **Preferences → Privacy → General** and enable **Local assistant**. That choice is saved. The viewer listens on `127.0.0.1` and writes a discovery file named `fs-mcp-<pid>.json`. The file can exist at the login screen, before inventory is usable. Log in and wait until inventory has loaded before asking the assistant to use it.

`--mcp-api` still forces the bridge on for that one session. It does not save the preference. Quit and the next launch follows the saved checkbox.

Turning the switch off deletes the discovery file and rejects requests. After the first enable in a session, the port stays bound until you quit. Enabling it again keeps that port and writes a new token. Quit deletes the file.

The discovery file is a bearer secret. Another program running as this user can read it while the switch is on. Leave it on disk for the sidecar, and do not paste it into chat, tickets, or a remote machine.

| OS | Discovery file directory |
| --- | --- |
| Linux | `~/.folderstorm_x64/user_settings` |
| Windows | `%APPDATA%\Folderstorm_x64\user_settings` |
| macOS | `~/Library/Application Support/Folderstorm/user_settings` |

On Linux you can point the viewer at another directory with `FOLDERSTORM_X64_USER_DIR`.

### 2. Build the sidecar

Install Go 1.25 or newer, then:

```bash
cd tools/fs-mcp
go build -o fs-mcp ./cmd/fs-mcp
```

Windows output name: `fs-mcp.exe`. Use the absolute path of that binary in the client config below. The sidecar speaks MCP over stdin and stdout. It has no port of its own.

### 3. How the sidecar finds the viewer

The sidecar searches the Folderstorm directory in the table above. It also checks the older Firestorm folders, so a Firestorm install on the same machine is still found. A normal install does not need `FIRESTORM_MCP_DISCOVERY`.

Set `FIRESTORM_MCP_DISCOVERY` when this viewer's settings live somewhere else. It may be one file, one directory of `fs-mcp-*.json` files, or a path list (`:` on Unix, `;` on Windows). When it is set, it replaces the default search. On Linux, `FOLDERSTORM_X64_USER_DIR` is recognized too.

`FIRESTORM_MCP_TIMEOUT` is how long one viewer call may take. The default is `90s`.

## Connect a client

All three clients run the same binary. They differ in file format, file location, and how environment variables are written.

Use a server name such as `folderstorm`. Tool names inside the sidecar stay `inventory_search`, `camera_snapshot`, and so on. Claude Code shows them to you as `mcp__folderstorm__inventory_search`.

### Cursor

Project file `.cursor/mcp.json`, or the global file `~/.cursor/mcp.json`. You can also add it from **Cursor Settings → MCP**.

```json
{
  "mcpServers": {
    "folderstorm": {
      "command": "/absolute/path/to/fs-mcp"
    }
  }
}
```

Restart the MCP server from the Cursor MCP panel after you edit the file. Cursor does not use a `type` field for a local command. If settings were moved, add an `env` object with `FIRESTORM_MCP_DISCOVERY` set to that `user_settings` directory.

### Claude Code

Three scopes, same JSON shape:

| Scope | Stored in | Who sees it |
| --- | --- | --- |
| local (default) | `~/.claude.json`, under this project | Only you, this project |
| project | `.mcp.json` in the project root | Anyone who clones the repo, after they approve it |
| user | `~/.claude.json`, top-level `mcpServers` | Only you, every project |

Add it from a shell. Flags go before the server name. The double dash separates the sidecar command:

```bash
claude mcp add --scope user \
  folderstorm -- /absolute/path/to/fs-mcp
```

Or write `.mcp.json` yourself:

```json
{
  "mcpServers": {
    "folderstorm": {
      "type": "stdio",
      "command": "/absolute/path/to/fs-mcp",
      "args": []
    }
  }
}
```

Check with `claude mcp list`. A project `.mcp.json` asks for approval the first time that folder is opened. Claude Code does not read `~/.claude/mcp.json`.

### Codex

Codex uses TOML, and the table name is `mcp_servers`, not `mcpServers`.

User file `~/.codex/config.toml`:

```toml
[mcp_servers.folderstorm]
command = "/absolute/path/to/fs-mcp"
startup_timeout_sec = 20
tool_timeout_sec = 120
```

Or:

```bash
codex mcp add folderstorm -- /absolute/path/to/fs-mcp
```

`codex mcp list` shows what was saved.

A project file `.codex/config.toml` is loaded only when that project is trusted. If the server appears in the file and Codex ignores it, trust the project or put the same table in `~/.codex/config.toml`.

`tool_timeout_sec` should stay above the sidecar timeout (default 90 seconds). Inventory reads wait for the viewer to fetch objects, and a short client timeout cancels them first. The sidecar itself starts immediately, so the startup timeout only needs to cover process launch.

### What is the same everywhere

- One command, no arguments, stdio transport.
- The same tool names and arguments.
- `FIRESTORM_MCP_DISCOVERY` only when settings were moved, and the same path on every client.
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

If two viewers are open, say which one, or ask for the process list and pick a pid.

## When it does not connect

- The local assistant switch is off. Turn it on under **Preferences → Privacy → General**. `--mcp-api` forces one session on without saving that choice. You do not need to quit and relaunch.
- `FIRESTORM_MCP_DISCOVERY` is set and points somewhere other than this viewer's `user_settings` directory. That variable replaces the default search. Unset it, or point it at the Folderstorm directory from the table above.
- Inventory is still downloading. `viewer_status` shows fetch progress. Wait until `usable` is true.
- The binary path in the client config is relative, or it is the Go source directory instead of the built `fs-mcp` file.
- Codex is using a project `.codex/config.toml` in an untrusted folder, or the table is named `mcpServers`.
- Claude Code was given `--env` after the server name, so the variable was never applied. Flags go before `folderstorm`.
- A tool dies around 10–30 seconds while inventory is still loading. Raise the client tool timeout above 90 seconds, or set `FIRESTORM_MCP_TIMEOUT` to match a timeout you accept.

The discovery file contains the bearer token. Leave it on disk for the sidecar, and do not paste it into chat, tickets, or a remote machine.
