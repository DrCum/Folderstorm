# Firestorm MCP sidecar (`fs-mcp`)

`fs-mcp` is a **stdio MCP server** that translates Cursor MCP tool calls into the Firestorm viewer's local, bearer-authenticated JSON Event API bridge. It is an independent Go module: the viewer does not link or build it.

The first release is **inventory-only** and **local-only**. Delete/purge, wear/rez, task inventory, Marketplace folders, and remote access are out of scope.

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

Read tools advertise MCP `readOnlyHint`. Move/copy/confirm advertise `destructiveHint`. All tools set `openWorldHint=false` (local viewer only).

Address objects by **UUID**. Names and paths are not unique.

### No-copy copy confirmation

Default `inventory_copy` copies copyable items, then if unique no-copy leaves remain the viewer returns a stored, expiring `plan_id` and proposed source→destination moves.

If the MCP client supports **elicitation**, the sidecar prompts: approval **moves unique no-copy items out of the source** into the destination. Decline leaves the copyable-only destination in place.

If elicitation is unavailable, the call is interrupted, or `skip_elicitation` is true, the tool returns `plan_id` and you resume with `inventory_confirm_copy`.

## Viewer integration contract (assumptions)

The C++ Event API bridge is implemented in parallel. This sidecar assumes:

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

Inventory ops: `status`, `get`, `list`, `search`, `systemFolder`, `createFolder`, `move`, `rename`, `copy`, `confirmCopy`.

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
- No delete/wear/rez tools
