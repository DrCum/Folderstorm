# Folderstorm MCP sidecar (`fs-mcp`)

`fs-mcp` is a local stdio [MCP](https://modelcontextprotocol.io) server. It turns tool calls from Cursor, Codex, Claude Code, or any other MCP client into the Folderstorm viewer's loopback Event API.

The viewer does not link this program or start it. The viewer build compiles this module and the packaged installer copies `fs-mcp` (`fs-mcp.exe` on Windows) into the install folder next to the viewer, beside `migrate-settings`. An MCP client still has to be pointed at that file. The installer does not write a client MCP config. The bridge is off until you enable **Preferences → Privacy → General → Local assistant**. With the bridge off, the sidecar finds no viewer. `--mcp-api` still forces it on for one session and does not save that choice. Nothing in this sidecar listens on the network. It does not receive Second Life credentials, session cookies, or simulator capability URLs.

On Windows, point Cursor, Claude Code, and Codex at:

`C:\ProgramData\Folderstorm\fs-mcp.exe`

That path has no spaces. Cursor starts the MCP command through `cmd.exe`, and a path under `C:\Program Files` is split at the space (`'C:\Program' is not recognized`). Use the ProgramData path as the command, with no `cmd` wrapper and no extra quotes. The Windows installers create it as a symbolic link, not a `.lnk` shortcut, to the `fs-mcp.exe` just installed beside the viewer. The channel directory may be `Folderstorm-Release`, `FolderstormOS-private-<host>`, or another channel name. The real exe stays there. An upgrade replaces the link so it points at the new install. Uninstall removes the link, and removes `C:\ProgramData\Folderstorm` only when that directory is empty. Removing the link leaves the real exe in place.

A Velopack install is per-user and usually not elevated. It tries to create the same link. When `C:\ProgramData` is not writable, the install still succeeds and `fs-mcp.exe` stays in the Velopack `current` folder next to the viewer.

The link targets the Release-channel copy below. A different channel uses that channel's directory.

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

A packaged Windows install already has the binary next to the viewer, and the installers link `C:\ProgramData\Folderstorm\fs-mcp.exe` at it. Use that ProgramData path in the client config. On Linux and macOS, use the `fs-mcp` binary beside the viewer. See [doc/building-from-source.md](../../doc/building-from-source.md). Building from this directory still requires Go 1.25 or newer, and that copy is for a checkout. An installed viewer already includes the program above.

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

The viewer does not start `fs-mcp`. Enable **Local assistant**, or the sidecar finds no viewer. Cursor, Claude Code, and Codex each have their own file. One file does not configure the other two. On Windows the command is `C:\ProgramData\Folderstorm\fs-mcp.exe` for NSIS, Velopack, and a private channel. The same instructions are in [doc/help.md](../../doc/help.md).

On Linux and macOS, use the `fs-mcp` binary beside the viewer and set `FIRESTORM_MCP_DISCOVERY` to that platform's Folderstorm user_settings directory.

### Cursor

**Settings → MCP**, or `%USERPROFILE%\.cursor\mcp.json`. The entry is `mcpServers.folderstorm`, with `command` and `env`. A project file `.cursor/mcp.json` is a different file.

```json
{
  "mcpServers": {
    "folderstorm": {
      "command": "C:\\ProgramData\\Folderstorm\\fs-mcp.exe",
      "env": {
        "FIRESTORM_MCP_DISCOVERY": "%APPDATA%\\Folderstorm_x64\\user_settings"
      }
    }
  }
}
```

### Claude Code

`claude mcp add --transport stdio --scope user` writes `%USERPROFILE%\.claude.json`:

```bat
claude mcp add --transport stdio --scope user folderstorm -- C:\ProgramData\Folderstorm\fs-mcp.exe
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
codex mcp add folderstorm -- C:\ProgramData\Folderstorm\fs-mcp.exe
```

```toml
[mcp_servers.folderstorm]
command = 'C:\ProgramData\Folderstorm\fs-mcp.exe'
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
| `inventory_set_thumbnail` | Texture UUID, or clear it. |
| `inventory_set_favorite` | Set or clear the favorite flag. |
| `inventory_link` | Link an item or folder into `parent_id`. |
| `inventory_replace_links` | Point every link that targets `source_id` at `target_id`, and move the old links to Trash. Ask is a viewer dialog unless that permission is Allow. |
| `inventory_move` | Move `id` to `parent_id`. This is a move, including no-copy items. Follows Move and copy. |
| `inventory_batch_move` | Up to 50 ids into one `parent_id`. Per-item results. Follows Move and copy. |
| `inventory_batch_rename` | Up to 50 `{id, name}` entries. Per-item results. |
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
| `camera_snapshot` | JPEG bytes plus width, height, and camera pose. Hides UI and HUD and writes a temp JPEG. Follows the Camera permission. Not a read. |
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

`camera_snapshot` is not a read. It follows the Camera permission. It hides UI and HUD unless you pass `show_ui` or `show_hud`, and it writes a temp JPEG. `max_edge` defaults to 1024 and is clamped from 64 to 2048. The viewer writes that file under its temp `fs-mcp-snapshots` directory. The sidecar checks that the path is absolute and at most 8 MiB, returns the bytes as `image/jpeg`, and deletes the file. Callers cannot choose the path.

## Event API body

```json
{
  "api": "LLInventory",
  "op": "get",
  "id": "01234567-89ab-cdef-0123-456789abcdef"
}
```

Inventory ops: `status`, `get`, `list`, `search`, `systemFolder`, `createFolder`, `move`, `rename`, `copy`, `confirmCopy`, `types`, `getMany`, `resolvePath`, `protectedFolders`, `setDescription`, `setThumbnail`, `setFavorite`, `link`, `replaceLinks`, `createItem`, `batchMove`, `batchRename`, `batchCopy`, `trash`, `restore`, `emptyTrash`, `purge`, `readNotecard`, `readScript`, `landmark`, `changes`.

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
