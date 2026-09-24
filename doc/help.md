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

## The local assistant bridge

The bridge lets an MCP client on this computer read and change inventory, change your outfit, move the camera, and take a picture. It does not teleport, chat, or reach the Marketplace. It does not run unless you ask for it, and the choice is not remembered.

### 1. Start the viewer with the bridge

```text
--mcp-api
```

On Windows, add that to the shortcut target after the executable. On macOS and Linux, pass it on the command line.

After startup the viewer listens on `127.0.0.1` and writes a discovery file named `fs-mcp-<pid>.json`. Log in and wait until inventory has loaded before asking the assistant to use it. Quit the viewer and the file is deleted. The next launch needs `--mcp-api` again.

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
- If the client can show an MCP confirmation form, answer it there. If it cannot, the tool returns a `plan_id` and tells you which follow-up tool to call. That fallback is the normal path on clients that do not advertise elicitation.

## Asking for something

Log in first. A good first request is "check the viewer status." `usable` should be true.

Then, in ordinary language:

- "List my outfits and tell me what I am wearing."
- "Search inventory for the shirt named dusk, under the Clothing folder."
- "Make a folder called Studio shots in Textures and move these two textures into it." Use UUIDs when two items share a name. Ask the assistant to resolve the path and show you the matches before it moves anything.
- "Wear the outfit named Portrait, then wait until the outfit is no longer dirty, frame a portrait, and take a picture with the HUD hidden."

Wear, detach, replacing links, emptying Trash, and permanent delete stop for confirmation. A no-copy copy copies the copyable pieces first, then stops and lists the unique items that would have to move. Approving that step removes them from the original folder. Declining leaves the copyable copy where it is and leaves the originals alone.

Moving one object to Trash is reversible with restore. Empty Trash and purge are not.

Baking continues after a wear call returns. Take the snapshot after `appearance_worn` reports the outfit is clean, or the picture can show the previous outfit.

Camera presets are `portrait`, `full_body`, `front`, `back`, `left`, and `right`. An exact shot uses region coordinates: where the camera stands, and the point it looks at.

If two viewers are open, say which one, or ask for the process list and pick a pid.

## When it does not connect

- The viewer was started without `--mcp-api`. Quit and relaunch with the flag. There is no preference that keeps it on.
- `FIRESTORM_MCP_DISCOVERY` is set and points somewhere other than this viewer's `user_settings` directory. That variable replaces the default search. Unset it, or point it at the Folderstorm directory from the table above.
- Inventory is still downloading. `viewer_status` shows fetch progress. Wait until `usable` is true.
- The binary path in the client config is relative, or it is the Go source directory instead of the built `fs-mcp` file.
- Codex is using a project `.codex/config.toml` in an untrusted folder, or the table is named `mcpServers`.
- Claude Code was given `--env` after the server name, so the variable was never applied. Flags go before `folderstorm`.
- A tool dies around 10–30 seconds while inventory is still loading. Raise the client tool timeout above 90 seconds, or set `FIRESTORM_MCP_TIMEOUT` to match a timeout you accept.

The discovery file contains the bearer token. Leave it on disk for the sidecar, and do not paste it into chat, tickets, or a remote machine.
