# Folderstorm MCP sidecar (`fs-mcp`)

`fs-mcp` is a local stdio [MCP](https://modelcontextprotocol.io) server. It turns tool calls from Cursor, Codex, Claude Code, or any other MCP client into the Folderstorm viewer's loopback Event API.

The viewer does not link this program or start it. The viewer build compiles this module and the packaged installer copies `fs-mcp` (`fs-mcp.exe` on Windows) into the install folder next to the viewer, beside `migrate-settings`. An MCP client still has to be pointed at that file. The installer does not write a client MCP config. The bridge is off until you enable **Preferences → Local assistant**. With the bridge off, the sidecar finds no viewer. `--mcp-api` still forces it on for one session and does not save that choice. Nothing in this sidecar listens on the network. It does not receive Second Life credentials, session cookies, or simulator capability URLs.

On Windows, use **Preferences → Local assistant → Setup → Copy configuration** in the installation you want to connect. New installers do not create or retarget a shared ProgramData command. Each installation keeps its own sidecar. Codex and Claude Code use that absolute executable path; Cursor's entry includes a quoted `cmd.exe` adapter when the path contains spaces.

A normal Velopack installation uses its stable `current\fs-mcp.exe` path. Setup verifies the package layout/id/version before describing that command as following updates. If the location is unverified, Setup uses the actual executable and asks you to recopy after updating. If the installed package changed while this viewer is running, restart before copying. Nonadmin installs need no writable ProgramData directory or symlink privilege for MCP configuration.

Older configurations may still use `C:\ProgramData\Folderstorm\fs-mcp.exe`. That legacy alias is kept unchanged during new installs and updates. Uninstall removes only file symlinks independently proven to target the uninstalling installation; unverified/busy links and unrelated files remain untouched. Old installers can still change the alias, so recopy existing client entries from their intended viewer's Setup page. No client files are rewritten automatically.

Typical Release paths are below; private/OpenSim channels use their own installation directory.

| Install | `fs-mcp.exe` beside the viewer |
| --- | --- |
| Velopack, Release | `%LocalAppData%\Folderstorm-Release\current\fs-mcp.exe` |
| NSIS, Release | `%ProgramFiles%\Folderstorm-Release\fs-mcp.exe` |

Set `FIRESTORM_MCP_DISCOVERY` to the Folderstorm user_settings directory, `%APPDATA%\Folderstorm_x64\user_settings`. For an account named Jane that directory is `C:\Users\Jane\AppData\Roaming\Folderstorm_x64\user_settings`. Use that Folderstorm directory. Firestorm's settings live in `%APPDATA%\Firestorm_x64\user_settings`, and that path will not see this viewer.

On Linux and macOS the program is the `fs-mcp` binary beside the viewer (next to `install.sh`, or `Folderstorm.app/Contents/Resources/fs-mcp`). Set `FIRESTORM_MCP_DISCOVERY` to that platform's Folderstorm user_settings directory.

Cursor, Claude Code, and Codex each have their own file. Connection steps are under [Connect a client](#connect-a-client). The same guide is in [doc/help.md](../../doc/help.md).

## What it can do

Talk to one logged-in Folderstorm process on this machine:

- Read inventory status, objects, folders, system folders, paths, protected folders, and a change cursor.
- Search by name, description, creator, type, worn state, permissions, favorite, and creation time.
- Create folders and a limited set of new items. Rename, describe, thumbnail, favorite, link, move, copy, trash, and restore.
- Read notecard text, LSL source, and landmark locations. Those reads do not save edits.
- List My Outfits and the Current Outfit. Wear an outfit or individual items, or detach them, after confirmation.
- Read and set the camera, including named portrait and full-body presets, then return a JPEG snapshot.

Address inventory by UUID. Names and paths are not unique. `inventory_resolve_path` can return more than one match.

## What it will not do

The viewer allowlist is `LLInventory`, `LLAppearance`, and `LLCamera`. Any other Event API is rejected with HTTP 403, including teleport, chat, gestures, `UI.call`, and window or input injection.

Also out of scope:

- Task inventory (objects you are sitting on or editing in-world).
- Marketplace listings and purchases.
- Remote or multi-user access. The HTTP listener is `127.0.0.1` only.
- Saving notecard or script edits. Reads are capped and report `asset_id` and `stale`.
- Push notifications. `inventory_changes` is request/response. Poll it with the `generation` from `viewer_status`.

Restrained Love locks are honored. A locked wearable, attachment, or folder returns an error instead of changing the avatar or the inventory.

## Build

A packaged Windows install already has the binary next to the viewer. Copy an installation-bound client entry from its Setup page. On Linux and macOS, use the `fs-mcp` binary beside the viewer. See [doc/building-from-source.md](../../doc/building-from-source.md). Building from this directory still requires Go 1.25 or newer, and that copy is for a checkout. An installed viewer already includes the program above.

```bash
cd tools/fs-mcp
go test ./...
go vet ./...
go build -o fs-mcp ./cmd/fs-mcp
```

Cross-compile:

```bash
GOOS=linux GOARCH=amd64 go build -o fs-mcp-linux ./cmd/fs-mcp
GOOS=darwin GOARCH=arm64 go build -o fs-mcp-darwin ./cmd/fs-mcp
GOOS=windows GOARCH=amd64 go build -o fs-mcp.exe ./cmd/fs-mcp
```

Logs go to stderr so they do not corrupt the stdio stream. The bearer token is never printed.


### Copy configuration and check this viewer

Open **Preferences → Local assistant → Setup**, choose Codex, Cursor, or Claude Code, then **Copy configuration**. Merge the entry into that client's file; keep your existing servers. Codex receives a TOML table; Cursor and Claude Code receive a JSON `mcpServers` entry. This uses the current installation and the viewer's actual settings directory, including directory overrides. It copies no discovery contents or bearer token and writes no client files.

On Windows copied commands always belong to this installation; a shared ProgramData alias is never selected. Cursor uses an explicit quoted `cmd.exe` adapter with the executable in `FOLDERSTORM_MCP_BINARY` when that path contains spaces. Legacy aliases are not modified during setup/install/update. On Linux the installed sidecar is at the package root, one directory above the viewer's `bin` directory; on macOS it is in `Contents/Resources`.

**Check connection** runs the installed sidecar asynchronously against this viewer. It checks authenticated bridge health even when inventory Read is Never. A successful check means the sidecar could reach this viewer at that moment; it does not mean your external assistant loaded its configuration. Inventory and recent authenticated external requests are shown separately. Diagnostic probes do not count as external assistant activity. Closing preferences or leaving the page cancels a running check; changes to bridge readiness or permissions clear an old result.

For a terminal check, use the installed executable:

```text
fs-mcp --diagnose --viewer-pid 12345 --discovery "/absolute/path/to/user_settings"
```

Replace the PID and directory with this viewer's values (the discovery filename includes its PID). The command emits bounded JSON with fixed failure stages and readiness facts, never the bearer token or raw server errors. Exit status is 0 on success, 1 when the check fails, and 2 for invalid arguments/output failure. HTTP checks have a four-second timeout; the viewer terminates its subprocess after five seconds. A pre-login viewer can pass bridge health while inventory is unavailable. Running `fs-mcp` with no arguments still starts the MCP stdio server.

## How a call gets to the viewer

1. You enable the local assistant in Preferences, or you pass `--mcp-api` for this session only. The preference is saved. The flag is not.
2. The first enable in a process binds `127.0.0.1` on an ephemeral port. That port stays bound until you quit, even if you turn the switch off. Off deletes the discovery file and rejects requests. The next on writes a new bearer token to `fs-mcp-<pid>.json` and keeps the same port. The file can exist at the login screen. It is removed on exit. On Unix the file is mode `0600`. The file is a bearer secret: another program running as this user can read it while the switch is on. Do not copy or publish it.
3. The sidecar finds that file, checks that the process is still alive, and posts JSON to `http://127.0.0.1:<port>/firestorm/event-api` with `Authorization: Bearer <token>`.
4. The viewer rejects a non-loopback peer, a browser `Origin` header, a body over 1 MiB, and any API outside the allowlist. Responses send `Cache-Control: no-store`.

`viewer_status` is useful after login, when `usable` is true. Inventory tools error if you call them before that.

### Discovery file

```json
{
  "pid": 12345,
  "port": 54321,
  "token": "<per-launch bearer secret>",
  "api_version": 1,
  "host": "127.0.0.1",
  "path": "/firestorm/event-api",
  "scheme": "http"
}
```

Accepted aliases: `http_port`, `bearer_token` / `auth_token`, `apiVersion`. `host` must be loopback. A `url` field may replace scheme, host, port, and path. Do not copy or publish this file.

### Where the viewer writes it

The settings folder follows the Folderstorm name on every platform, including macOS.

| OS | User-settings directory | Viewer override |
| --- | --- | --- |
| Linux 64-bit | `~/.folderstorm_x64/user_settings` | `FOLDERSTORM_X64_USER_DIR` |
| Windows 64-bit | `%APPDATA%\Folderstorm_x64\user_settings` | — |
| macOS | `~/Library/Application Support/Folderstorm/user_settings` | — |

On macOS the cache directory is `Folderstorm_x64`. Settings, logs, and the discovery file stay in `Folderstorm`.

### Where the sidecar looks

Point each client at the installed exe and set `FIRESTORM_MCP_DISCOVERY` to the Folderstorm user_settings directory. On Windows that is `%APPDATA%\Folderstorm_x64\user_settings`. On Linux it is `~/.folderstorm_x64/user_settings`. On macOS it is `~/Library/Application Support/Folderstorm/user_settings`. Use the Folderstorm directory. Firestorm's user_settings folder is a different path.

When `FIRESTORM_MCP_DISCOVERY` is set, it replaces the default search. It may be one file, one directory of `fs-mcp-*.json` files, or a path list (`:` on Unix, `;` on Windows).

If the variable is unset, the sidecar searches in this order:

1. A user-dir override, if one is set: `FOLDERSTORM_X64_USER_DIR`, `FOLDERSTORMOS_X64_USER_DIR`, `FOLDERSTORM_USER_DIR`, `FOLDERSTORMOS_USER_DIR`, then the matching `FIRESTORM_*_USER_DIR` variables.
2. The Folderstorm settings folders for this operating system.
3. The older Firestorm settings folders, so a Firestorm install on the same machine is still found.

| OS | Folderstorm folders, then Firestorm folders |
| --- | --- |
| Linux | `~/.folderstorm_x64`, `~/.folderstormos_x64`, `~/.folderstorm`, `~/.folderstormos`, then `~/.firestorm_x64` and the other Firestorm names |
| Windows | `%APPDATA%\Folderstorm_x64`, `FolderstormOS_x64`, `Folderstorm`, `FolderstormOS`, then `Firestorm_x64` and the other Firestorm names |
| macOS | `~/Library/Application Support/Folderstorm`, plus `FolderstormOS`, `Folderstorm_x64`, and `FolderstormOS_x64`, then the same names with Firestorm |

Each entry is the `user_settings` directory inside that folder. The sidecar reads `fs-mcp-*.json` files there. The client configs below set `FIRESTORM_MCP_DISCOVERY` so that search stays on Folderstorm.

`FIRESTORM_MCP_TIMEOUT` is a Go duration for each HTTP call. The default is `90s`.

## Connect a client

The viewer does not start `fs-mcp`. Enable **Local assistant**, or the sidecar finds no viewer. Cursor, Claude Code, and Codex each have their own file. One file does not configure the other two. On Windows copy the command from the intended installation's Setup page. The same instructions are in [doc/help.md](../../doc/help.md).

On Linux and macOS, use the `fs-mcp` binary beside the viewer and set `FIRESTORM_MCP_DISCOVERY` to that platform's Folderstorm user_settings directory.

### Cursor

**Settings → MCP**, or `%USERPROFILE%\.cursor\mcp.json`. The entry is `mcpServers.folderstorm`, with `command` and `env`. A project file `.cursor/mcp.json` is a different file.

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

### Claude Code

`claude mcp add --transport stdio --scope user` writes `%USERPROFILE%\.claude.json`:

```bat
claude mcp add --transport stdio --scope user folderstorm -- "C:\Program Files\Folderstorm-Release\fs-mcp.exe"
```

For a single project, use `.mcp.json` in that project's root. If the CLI has no environment flag, add the env block in the file it wrote:

```json
"env": {
  "FIRESTORM_MCP_DISCOVERY": "%APPDATA%\\Folderstorm_x64\\user_settings"
}
```

### Codex

Codex is ChatGPT's coding client. `codex mcp add` writes `%USERPROFILE%\.codex\config.toml`:

```bat
codex mcp add folderstorm -- "C:\Program Files\Folderstorm-Release\fs-mcp.exe"
```

```toml
[mcp_servers.folderstorm]
command = 'C:\Program Files\Folderstorm-Release\fs-mcp.exe'
startup_timeout_sec = 20
tool_timeout_sec = 120

[mcp_servers.folderstorm.env]
FIRESTORM_MCP_DISCOVERY = '%APPDATA%\Folderstorm_x64\user_settings'
```

If the command leaves out the env table, add it in that file. The ChatGPT desktop app is separate. Add the sidecar by hand in its connector settings. That app does not read `%USERPROFILE%\.codex\config.toml`.

### More than one viewer

- One live discovery file: tools use it.
- Several: call `viewer_list`, then `viewer_select` with a `pid`. Every other tool also accepts optional `viewer_pid`.
- Stale PIDs are skipped. Unix uses a signal-0 check. Windows checks whether the process has exited.

## Tools

Annotations: read-only tools set `readOnlyHint`. Move, copy, wear, trash, and purge set `destructiveHint`. Every tool sets `openWorldHint` false because the data never leaves this machine.

| Tool | Effect |
| --- | --- |
| `viewer_status` | Selected viewer, redacted discovery, and inventory status including `generation`. |
| `viewer_list` | Live viewers. |
| `viewer_select` | Choose a `pid` when more than one viewer is running. |
| `inventory_get` | One item or folder by UUID. Waits for an incomplete item when the viewer can fetch it. |
| `inventory_get_many` | Up to 50 UUIDs. |
| `inventory_list` | Direct children of `folder_id`. `limit` and `offset`. Returns `total_matches` and `truncated`. |
| `inventory_search` | See search fields below. |
| `inventory_system_folder` | UUID of a system folder such as Textures, Objects, or Landmarks. `type` or `ft_name`. |
| `inventory_types` | Folder type names and asset type names the viewer accepts. |
| `inventory_resolve_path` | Every UUID matching a path from inventory root, or from `folder_id`. Ambiguous on purpose. |
| `inventory_protected_folders` | Protected folders. Does not change protection. |
| `inventory_changes` | Changes with `generation` greater than `since`. |
| `inventory_read_notecard` | Notecard text, capped at 64 KiB, plus `asset_id`, `stale`, and `truncated`. |
| `inventory_read_script` | LSL source, same cap and flags. |
| `inventory_landmark` | Region name and global position. |
| `inventory_create_folder` | `parent_id`, `name`. |
| `inventory_create_item` | See item types below. |
| `inventory_rename` | `id`, `name`. |
| `inventory_set_description` | Item or folder description. |
| `inventory_set_thumbnail` | Texture UUID, or clear it. Stays free. Follows Rename and edit details. |
| `inventory_snapshot_upload` | Capture and set a folder, outfit, or item image. `destination` is `thumbnail` by default, or `texture`. |
| `inventory_set_favorite` | Set or clear the favorite flag. |
| `inventory_link` | Link an item or folder into `parent_id`. |
| `inventory_replace_links` | Verify replacement links to `target_id` before submitting old links targeting `source_id` to Trash. Protected links can be skipped. Inspect each result; `trash_state: submitted` is not server confirmation, and `retry_safe: false` requires inventory inspection before retrying. Ask is a viewer dialog unless that permission is Allow. |
| `inventory_move` | Move `id` to `parent_id`. This is a move, including no-copy items. Follows Move and copy. |
| `inventory_batch_move` | Up to 50 ids into one `parent_id`. Per-item results. Follows Move and copy. |
| `inventory_batch_rename` | Up to 50 `{id, name}` entries. Per-item results. |
| `inventory_preview_batch_rename` | Dry-run up to 50 `{id, name}` entries, returning a viewer-owned plan and exact skips. Requires bulk review version 1 and Read. |
| `inventory_preview_batch_move` | Dry-run up to 50 `ids` into `parent_id`. Requires bulk review version 1 and Read. |
| `inventory_execute_plan` | Execute `plan_id` once, or retrieve its existing operation on a repeated call. The viewer derives current Edit/Move permission from the plan. |
| `inventory_history` | Session-only assistant inventory outcomes. Optional `operation_id`, or `offset`/`limit` (default 20, maximum 50). Requires Read. |
| `inventory_preview_undo` | Review limited eligible undo for `operation_id`; optional exact `ids` subset. Execute the returned plan separately. Requires Read. |
| `inventory_copy` | Copy `id` into `parent_id`. Optional `policy`. Copyable items follow Move and copy. |
| `inventory_batch_copy` | Up to 50 items. Folders still use `inventory_copy`. Copyable items follow Move and copy. |
| `inventory_confirm_copy` | Older-viewer follow-up for a no-copy copy plan. A current viewer asks in its own dialog. |
| `inventory_trash` | Move one object to Trash. Its own permission, default Allow. The item can be restored. |
| `inventory_restore` | Move an object out of Trash into its type folder. Follows Move and copy, not Trash. |
| `inventory_empty_trash` | Not available. The bridge hard-denies this. No setting can enable it. |
| `inventory_purge` | Not available. The bridge hard-denies this. No setting can enable it. |
| `appearance_outfits` | Outfit folders under My Outfits: id and name. |
| `appearance_outfit_items` | Items in an outfit folder: name, wearable type, worn flag. |
| `appearance_worn` | Current Outfit folder, including wearable type and attachment point, plus `outfit_dirty`. |
| `appearance_wear_outfit` | Wear by `folder_id` or `folder_name`. `append` adds to the current outfit. Ask is a viewer dialog unless Wear and detach is Allow. |
| `appearance_wear_items` | Wear one UUID or an array. `replace` removes conflicting worn items. Ask is a viewer dialog unless Wear and detach is Allow. |
| `appearance_detach` | Detach or take off by UUID. Ask is a viewer dialog unless Wear and detach is Allow. |
| `camera_get` | Region position, focus, agent-relative offset, global position, and distance. |
| `camera_set_pose` | `portrait`, `full_body`, `front`, `back`, `left`, or `right`, relative to the avatar's facing. |
| `camera_set` | Region `position` and `focus`, each `[x, y, z]`. |
| `camera_reset` | Default third-person camera. |
| `camera_snapshot` | JPEG bytes plus width, height, and camera pose. Hides UI and HUD and writes a temp JPEG. Optional `width`, `height`, and `viewport_only`. Follows the Camera permission. Does not upload. Not a read. |
| `confirm_action` | Resume a wear, detach, or link-replacement plan stored for an older viewer. A current viewer asks in its own dialog. Purge and empty-trash plans are denied and are not sent. |

### Object fields

An item lookup includes id, name, parent, description, inventory type, asset type, creation date, asset id, link flag, linked id, copyable, modifiable, transferable, worn, favorite, thumbnail, path, creator, wearable type, sale info, and permission masks. A folder lookup includes id, name, parent, preferred type, complete flag, version, descendant count, thumbnail, and path.

### Search fields

`inventory_search` accepts `query` or `name`, `desc`, `type`, `folder_id`, `filter_links` (`INCLUDE_LINKS`, `EXCLUDE_LINKS`, `ONLY_LINKS`), `include_trash`, `creator_id`, `creator_name`, `inv_type`, `linked_id`, `worn`, `copyable`, `modifiable`, `favorite`, `created_after`, `created_before`, and `limit`. Creator name matches only when the viewer name cache already has that name.

### Creating items

`inventory_create_item` takes `type`, optional `parent_id`, and optional `name` / `desc`.

| `type` | Result |
| --- | --- |
| `notecard`, `lsl`, `gesture`, `material` | A new inventory item of that kind. |
| `sky`, `water`, `daycycle` | An environment settings item. |
| A wearable name such as `shape`, `skin`, `hair`, `eyes`, `shirt`, `pants`, `shoes`, `jacket`, `gloves`, `undershirt`, `underpants`, `skirt`, `alpha`, `tattoo`, `universal`, `physics` | A clothing or body-part item. |
| `landmark` | A landmark of where the avatar is standing. The new id shows up on a later `inventory_changes` poll. The immediate reply only confirms the parent folder. |

Other asset types are rejected. Calling cards cannot be renamed. Library folders cannot be written. Protected, locked, and special destination folders are rejected. A move plan that went stale is rejected rather than applied.

### Copy policies

`policy` on `inventory_copy` and `inventory_batch_copy`:

| Policy | Behavior |
| --- | --- |
| `default` (omitted) | Copy the folder structure, links, and every copyable item. Then return the exact no-copy items that would have to move. Those unique items stay in the source until you confirm. |
| `strict` | Make no changes if any descendant is no-copy. |
| `copyable_only` | Copy what is permitted and leave no-copy items behind, with no confirmation step. |

On a current viewer, Ask is a dialog in the viewer. The sidecar does not elicit and does not return a `plan_id` for that question. Allow on Move no-copy items during a copy moves the unique items out of the source with no dialog. Never leaves them in the source and does not call the confirm step. The tool stays in flight until the dialog is answered or the viewer denies it at 60 seconds. A client that auto-approves `confirm_action` does not skip the dialog.

Trash is its own class, default Allow, and the preference row says Trash can be restored. Restore follows Move and copy. Permanent delete is not possible: the bridge hard-denies `purge` and `emptyTrash`, with no Ask and no row that can enable them. A hand-edited setting cannot turn them on. Those tools fail immediately and do not call the viewer.

An older viewer has no `permissions` object. Wear, detach, link replacement, and no-copy copies then keep the sidecar form. If the client does not advertise elicitation, or you pass `skip_elicitation: true`, the tool returns `plan_id`. Resume a copy with `inventory_confirm_copy`. Resume wear, detach, and link replacement with `confirm_action`. That fallback is not used for purge or empty Trash. Plans last ten minutes. Folder copy is not one server transaction, so a partial failure reports per-item errors.

Wearing returns before the avatar finishes baking. Poll `appearance_worn` and look at `outfit_dirty` before you take a picture.

### Bulk review and recovery

The five new review/history tools require authenticated status to advertise
`capabilities.bulk_inventory_review: 1` and a current permission map. They remain
listed by the sidecar when an older viewer is selected, but calls return
`unsupported_feature`; they never fall back to immediate batch mutation.
Existing batch tools remain available with their existing behavior.

Prepare a rename or move preview, inspect its exact before/after rows and skips,
then pass only its `plan_id` to `inventory_execute_plan` for the same viewer.
Plans expire after ten minutes and are bound to that viewer's login session.
Preview does not authorize execution. The viewer revalidates the frozen scope,
rejects stale plans, derives Edit or Move from its own plan, and asks in the viewer
when required. The sidecar never accepts caller-supplied permission classes,
candidate lists, confirmation overrides, or trusted deadlines for execution.
Repeated Execute returns the existing operation rather than replaying writes.

Inspect `status` and per-row outcomes instead of treating legacy `ok` as server
confirmation. `submitted` or `unconfirmed` means the viewer sent work whose
completion is not established. Inspect its history and inventory before retrying.
Partial results remain structured and do not trigger automatic rollback. With
Read set to Never, plan execution can still use its current Edit/Move permission,
but its response omits stored names and paths; preview and history are denied.

`inventory_preview_undo` prepares a separate limited inverse over eligible,
confirmed history rows. It does not reverse changes itself. Current state and
permissions must still permit execution. Link replacement, copies, no-copy
moves, folder moves, Trash, wear, asset edits, and payments have no undo here.
Unknown server completion never qualifies for recovery. History is bounded and
kept in memory for the current login session; it is cleared on logout or by
Clear history. Clearing history does not cancel work already sent to the server.

### Camera presets

Presets are offsets from the avatar, rotated by the avatar's horizontal facing:

| Preset | Camera offset (forward, left, up) | Focus height |
| --- | --- | --- |
| `portrait` | 1.6, 0, 1.55 | 1.65 |
| `full_body` | 3.8, 0, 1.1 | 0.9 |
| `front` | 2.6, 0, 1.3 | 1.2 |
| `back` | -2.6, 0, 1.3 | 1.2 |
| `left` | 0, 2.6, 1.3 | 1.2 |
| `right` | 0, -2.6, 1.3 | 1.2 |

`camera_set` uses region coordinates, not global coordinates. The avatar must be in a region. Presets also require a loaded avatar.

`camera_snapshot` is not a read. It follows the Camera permission. It hides UI and HUD unless you pass `show_ui` or `show_hud`, and it writes a temp JPEG. `max_edge` defaults to 1024 and is clamped from 64 to 2048. The viewer writes that file under its temp `fs-mcp-snapshots` directory. The sidecar checks that the path is absolute and at most 8 MiB, returns the bytes as `image/jpeg`, and deletes the file. Callers cannot choose the path. It does not upload.

`width` and `height` are optional. When both are set, the capture is that exact frame: the viewer center-crops to the ratio, then scales. A square is the same number on both sides. Each side is clamped from 64 to 2048. Omit them and the picture stays the whole window, scaled so the long edge is at most `max_edge`. `viewport_only` uses the world view instead of the full window. It defaults to false, so existing calls stay the same. With the custom world view off, that rect is the full window.

`inventory_snapshot_upload` takes `id` (an outfit folder, another folder, or an item) and sets that image. `width` and `height` default to 1024 and 1024. `viewport_only` defaults to true. `destination` is `thumbnail` unless you pass `texture`. Optional `name` is the inventory texture name; otherwise the folder or item name is used.

A thumbnail uses the same free upload as the Item Snapshot floater. It requires both Camera and Rename and edit details permissions: taking the picture and changing the inventory image are separate actions. The picture is scaled down to 256. The quoted cost is L$0, so both permissions set to Allow uploads with no dialog. The reply is `cost` 0, `destination` `thumbnail`, and `thumbnail_id`. It does not create a reusable inventory texture.

A texture creates a real inventory texture, then sets the folder image. It requires Camera, Create folders and items, and Rename and edit details. Any required permission set to Never refuses the upload; any set to Ask produces one viewer confirmation. The viewer quotes the L$ price before spending anything. A 1024 square uses the normal texture price. A larger request uses the 2K price. OpenSim can quote L$0. All required permissions set to Allow uploads with no dialog only when that price is L$0. A positive price always asks and shows the amount, and a price increase requires fresh confirmation. The reply is `cost`, `destination` `texture`, `asset_id`, and `thumbnail_id`.

`inventory_set_thumbnail` is unchanged. It still only points a folder or item at an existing texture UUID, stays on Rename and edit details, and does not spend L$. Allow does not spend L$ on a thumbnail upload or on a texture upload whose quoted cost is L$0.

## Event API body

```json
{
  "api": "LLInventory",
  "op": "get",
  "id": "01234567-89ab-cdef-0123-456789abcdef"
}
```

Inventory ops: `status`, `get`, `list`, `search`, `systemFolder`, `createFolder`, `move`, `rename`, `copy`, `confirmCopy`, `types`, `getMany`, `resolvePath`, `protectedFolders`, `setDescription`, `setThumbnail`, `snapshotUpload`, `setFavorite`, `link`, `replaceLinks`, `createItem`, `batchMove`, `batchRename`, `batchCopy`, `previewBatchRename`, `previewBatchMove`, `executeBulkPlan`, `bulkHistory`, `previewBulkUndo`, `trash`, `restore`, `emptyTrash`, `purge`, `readNotecard`, `readScript`, `landmark`, `changes`.

Appearance ops: `getOutfitsList`, `getOutfitItems`, `worn`, `wearOutfit`, `wearItems`, `detachItems`.

Camera ops: `get`, `setPose`, `set`, `reset`, `snapshot`.

Success is HTTP 2xx JSON. An Event API failure may be HTTP 2xx with `"error"` or an HTTP 4xx/5xx. HTTP 401 is reported as unauthorized.

A no-copy copy that needs a decision looks like:

```json
{
  "status": "confirmation_required",
  "plan_id": "…",
  "proposed_moves": [
    {"source_id": "…", "destination_parent_id": "…"}
  ]
}
```

The viewer still requires `confirm: true` on wear, detach, link replacement, empty trash, and purge. A raw POST without it does not change the avatar or destroy inventory.

## Security boundary

The token stops accidental cross-process use and browser DNS rebinding. It does not sandbox another program already running as the same desktop user: that program can read the discovery file. Leave the flag off when you are not using an assistant. Never forward the discovery file or the port off the machine.
