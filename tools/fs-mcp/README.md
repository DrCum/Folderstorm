# Firestorm MCP sidecar (`fs-mcp`)

`fs-mcp` is a **stdio MCP server** that translates Cursor MCP tool calls into the Firestorm viewer's local, bearer-authenticated JSON Event API bridge. It is an independent Go module: the viewer does not link or build it.

The sidecar is **local-only**. It can read and edit agent inventory, dress the avatar, pose the camera, and return a snapshot. Task inventory, Marketplace folders, teleport, chat, and remote access stay out of scope. Wear, detach, link replacement, emptying trash, and purge require explicit confirmation.

## Build

Requires Go 1.25+ (the official [`modelcontextprotocol/go-sdk`](https://github.com/modelcontextprotocol/go-sdk) v1.8+ toolchain).

```bash
cd tools/fs-mcp
go test ./...
go vet ./...
go build -o fs-mcp ./cmd/fs-mcp
```

Cross-compile examples:

```bash
GOOS=linux GOARCH=amd64 go build -o fs-mcp-linux ./cmd/fs-mcp
GOOS=darwin GOARCH=arm64 go build -o fs-mcp-darwin ./cmd/fs-mcp
GOOS=windows GOARCH=amd64 go build -o fs-mcp.exe ./cmd/fs-mcp
```

## Enable the viewer bridge

Start Firestorm with the **non-persistent** Event API bridge setting (C++ command-line mapping lands with the viewer half of this work). The viewer then:

1. Binds `127.0.0.1` on an ephemeral port
2. Generates a per-launch bearer token (never logged)
3. Writes `user_settings/fs-mcp-<pid>.json`

Default discovery directories:

| OS | Paths |
| --- | --- |
| Linux | `~/.firestorm_x64/user_settings`, `~/.firestormos_x64/user_settings`, `~/.firestorm/user_settings`, `~/.firestormos/user_settings` |
| macOS | `~/Library/Application Support/Firestorm/user_settings` (and FirestormOS / `_x64` variants) |
| Windows | `%APPDATA%\Firestorm_x64\user_settings` (and FirestormOS / non-x64 variants) |

Linux also honors `FIRESTORM_X64_USER_DIR` / `FIRESTORM_USER_DIR` (and OS-channel equivalents), matching the viewer's `*_USER_DIR` override.

### `FIRESTORM_MCP_DISCOVERY`

Set this to a file, a directory of `fs-mcp-*.json` files, or a path list (`:` on Unix, `;` on Windows) to override default search locations. Use it in tests and when the viewer writes discovery somewhere else.

```bash
export FIRESTORM_MCP_DISCOVERY="$HOME/.firestorm_x64/user_settings"
```

Optional `FIRESTORM_MCP_TIMEOUT` is a Go duration for Event API HTTP calls (default `90s`).

## Cursor configuration

Add a stdio MCP server pointing at the built binary. In Cursor MCP settings (or `mcp.json`):

```json
{
  "mcpServers": {
    "firestorm": {
      "command": "/absolute/path/to/fs-mcp",
      "env": {
        "FIRESTORM_MCP_DISCOVERY": "/home/you/.firestorm_x64/user_settings"
      }
    }
  }
}
```

Logs go to stderr so they do not corrupt the stdio MCP stream. The sidecar never prints the bearer token.

## Multi-viewer behavior

- **One live viewer:** tools auto-select it.
- **Several live viewers:** call `viewer_list`, then `viewer_select` with a `pid`. Inventory tools also accept optional `viewer_pid`.
- Stale PIDs are skipped best-effort (`kill(pid, 0)` on Unix; process exit-code check on Windows).

## Tools

| Tool | Notes |
| --- | --- |
| `viewer_status` | Selected viewer + Event API `status` (token redacted) |
| `viewer_list` / `viewer_select` | Required when more than one viewer is running |
| `inventory_get` | UUID lookup (`op=get`) |
| `inventory_list` | Direct descendants (`op=list`, `folder_id`) |
| `inventory_search` | Name/desc/type search (`op=search`) |
| `inventory_system_folder` | System folder UUID (`op=systemFolder`, `ft_name`) |
| `inventory_create_folder` | `parent_id`, `name` |
| `inventory_move` | `id`, `parent_id` (moves; does not copy) |
| `inventory_rename` | `id`, `name` |
| `inventory_copy` | `id`, `parent_id`, optional `policy`: `default`, `strict`, `copyable_only` |
| `inventory_confirm_copy` | `plan_id`, optional `confirm` (default true) |
| `inventory_types` | Folder type and asset type names |
| `inventory_get_many` | Up to 50 UUIDs |
| `inventory_resolve_path` | Path from root or `folder_id`; may be ambiguous |
| `inventory_protected_folders` | Read-only list of protected folders |
| `inventory_changes` | Changes since a `since` generation from `viewer_status` |
| `inventory_read_notecard` / `inventory_read_script` / `inventory_landmark` | Capped asset reads; they do not save edits |
| `inventory_set_description` / `inventory_set_thumbnail` / `inventory_set_favorite` | Item or folder metadata |
| `inventory_link` / `inventory_replace_links` | Create a link, or retarget existing links after confirmation |
| `inventory_create_item` | Notecard, script, gesture, material, settings, clothing, body part, or a landmark of the current location |
| `inventory_batch_move` / `inventory_batch_rename` / `inventory_batch_copy` | Up to 50 objects; batch copy returns a `plan_id` per no-copy item |
| `inventory_trash` / `inventory_restore` | Move into or out of Trash |
| `inventory_empty_trash` / `inventory_purge` | Permanent. Require confirmation |
| `appearance_outfits` / `appearance_outfit_items` / `appearance_worn` | Outfit catalog and Current Outfit. Poll `appearance_worn` after wear; baking is asynchronous |
| `appearance_wear_outfit` / `appearance_wear_items` / `appearance_detach` | Require confirmation |
| `camera_get` / `camera_set_pose` / `camera_set` / `camera_reset` | Presets: `portrait`, `full_body`, `front`, `back`, `left`, `right`. Explicit pose uses region `[x, y, z]` |
| `camera_snapshot` | JPEG image content plus width, height, and camera pose. UI and HUD are hidden by default |
| `confirm_action` | Approve or decline a wear, detach, link-replacement, empty-trash, or purge `plan_id` |

`inventory_list` accepts `limit` and `offset` and returns `total_matches` and `truncated`. `inventory_search` also accepts `include_trash`, `creator_id`, `creator_name`, `inv_type`, `linked_id`, `worn`, `copyable`, `modifiable`, `favorite`, `created_after`, and `created_before`.

Read tools advertise MCP `readOnlyHint`. Move, copy, wear, trash, and purge advertise `destructiveHint`. All tools set `openWorldHint=false` (local viewer only).

Address objects by **UUID**. Names and paths are not unique.

### No-copy copy confirmation

Default `inventory_copy` copies copyable items, then if unique no-copy leaves remain the viewer returns a stored, expiring `plan_id` and proposed source→destination moves.

If the MCP client supports **elicitation**, the sidecar prompts: approval **moves unique no-copy items out of the source** into the destination. Decline leaves the copyable-only destination in place.

If elicitation is unavailable, the call is interrupted, or `skip_elicitation` is true, the tool returns `plan_id` and you resume with `inventory_confirm_copy`.

Wear, detach, link replacement, empty trash, and purge use the same prompt. When elicitation is unavailable they return a sidecar `plan_id`; resume with `confirm_action`. The viewer still requires `confirm: true`, so a raw Event API call without it does not wear or purge. A declined prompt keeps the plan so `confirm_action` can approve it later. `confirm_action` with `confirm: false` drops the plan.

## Viewer integration contract (assumptions)

The sidecar posts to the viewer's loopback Event API:

### Discovery file `fs-mcp-<pid>.json`

```json
{
  "pid": 12345,
  "port": 54321,
  "token": "<per-launch bearer secret>",
  "api_version": 1,
  "host": "127.0.0.1",
  "path": "/",
  "scheme": "http"
}
```

Aliases accepted: `http_port`, `bearer_token` / `auth_token`, `apiVersion`. `host` must be loopback. `url` may replace scheme/host/port/path.

### HTTP Event API

`POST http://127.0.0.1:<port>/` (or discovery `path` / `url`)

Headers:

- `Authorization: Bearer <token>`
- `Content-Type: application/json`

Body is an LLEventAPI request:

```json
{
  "api": "LLInventory",
  "op": "get",
  "id": "01234567-89ab-cdef-0123-456789abcdef"
}
```

The loopback bridge accepts only `LLInventory`, `LLAppearance`, and `LLCamera`. Any other Event API, including teleport, chat, and `UI.call`, is rejected with HTTP 403.

Inventory ops include `status`, `get`, `list`, `search`, `systemFolder`, `createFolder`, `move`, `rename`, `copy`, `confirmCopy`, `types`, `getMany`, `resolvePath`, `protectedFolders`, `setDescription`, `setThumbnail`, `setFavorite`, `link`, `replaceLinks`, `createItem`, `batchMove`, `batchRename`, `batchCopy`, `trash`, `restore`, `emptyTrash`, `purge`, `readNotecard`, `readScript`, `landmark`, and `changes`.

Appearance ops: `getOutfitsList`, `getOutfitItems`, `worn`, `wearOutfit`, `wearItems`, `detachItems`.

Camera ops: `get`, `setPose`, `set`, `reset`, `snapshot`. `snapshot` writes a temp JPEG the sidecar reads and deletes. Callers cannot supply a path.

Success: HTTP 2xx JSON object (Event API reply). Event API failures may be HTTP 2xx with `"error": "..."` or HTTP 4xx/5xx. `401` is mapped to `unauthorized`.

Copy confirmation payload (any of these trigger the sidecar confirm flow):

```json
{
  "status": "confirmation_required",
  "plan_id": "…",
  "proposed_moves": [
    {"source_id": "…", "destination_parent_id": "…"}
  ]
}
```

`confirmCopy` body: `{"api":"LLInventory","op":"confirmCopy","plan_id":"…","confirm":true}`.

## Security

- Loopback only; non-loopback discovery hosts and URLs are rejected
- Per-launch bearer token, redacted from tool results
- No Origin header (the viewer rejects browser Origin)
- Wear, detach, empty trash, and purge require a confirmed plan
- Snapshots are temp files created by the viewer, not caller-supplied paths
