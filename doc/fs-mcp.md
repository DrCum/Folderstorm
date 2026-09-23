# Local Firestorm MCP

Firestorm can expose its inventory Event API to a local MCP sidecar. The
integration is disabled by default and does not accept remote connections.

## Start Firestorm

Launch the viewer with:

```text
--mcp-api
```

After startup, Firestorm listens on an operating-system-selected
`127.0.0.1` port and writes an `fs-mcp-<pid>.json` discovery file to
the Firestorm user-settings directory. The file contains a random bearer
token and is removed when the viewer exits. A new token is generated for
every viewer launch.

The API becomes useful after the viewer is logged in and `viewer_status`
reports that inventory is usable. Do not copy or publish the discovery file.

## Configure Cursor

Build the sidecar:

```sh
cd tools/fs-mcp
go build -o fs-mcp ./cmd/fs-mcp
```

Add it to Cursor's MCP configuration:

```json
{
  "mcpServers": {
    "firestorm": {
      "command": "/absolute/path/to/fs-mcp"
    }
  }
}
```

The sidecar uses MCP over stdio. It discovers the local viewer and forwards
requests to the authenticated loopback API. Set `FIRESTORM_MCP_DISCOVERY`
to a discovery file or directory to override normal discovery.

When more than one viewer is running, use the viewer listing and selection
tools before inventory operations.

## Inventory behavior

Inventory objects are identified by UUID because names and paths are not
unique. The tool set supports status, get, list, search, system-folder lookup,
folder creation, move, rename, and copy. It also covers richer item
fields, batch get, path resolution, protected folders, descriptions,
thumbnails, favorites, links, item creation, batch move/rename/copy,
trash and restore, notecard/script/landmark reads, and an inventory
change generation.

`viewer_status` includes the current change `generation`.
`inventory_changes` returns later changes with a `since` cursor. This is
not a push stream: the bridge stays request/response.

### Appearance and camera

`appearance_outfits` and `appearance_outfit_items` list My Outfits.
`appearance_worn` lists the Current Outfit, including wearable type and
attachment point. Wearing and detaching require confirmation. Baking
continues after the wear call returns, so poll `appearance_worn` before
taking a picture.

`camera_set_pose` uses `portrait`, `full_body`, `front`, `back`, `left`,
or `right`. `camera_set` takes region `[x, y, z]` position and focus.
`camera_reset` restores the default camera. `camera_snapshot` returns a
JPEG with the UI and HUD hidden by default. The viewer writes that file
itself and the sidecar deletes it after reading.

### Confirmation

No-copy copy still resumes with `inventory_confirm_copy`. Wear, detach,
link replacement, emptying trash, and purging resume with
`confirm_action` when the client cannot show an elicitation prompt.
Pass `skip_elicitation` to receive a `plan_id` immediately. Emptying
trash and purging are permanent. Moving one object to Trash is not.

Reads fetch incomplete inventory before returning where practical. Mutations
reject protected or locked folders, unsupported special destinations,
library writes, calling-card renames, and stale move plans.

### No-copy items

The default `include_no_copy` folder-copy policy:

1. Copies the folder structure, links, and all copyable items.
2. Waits for those operations to complete.
3. Returns the exact no-copy items that would need to move to complete the
   destination.
4. Requires explicit confirmation before moving those unique items.

Confirming removes the no-copy items from their original folders. Declining
keeps them in the source and leaves the already-created copyable-only
destination in place.

Additional policies:

- `strict`: make no changes if any descendant is no-copy.
- `copyable_only`: copy what is permitted and leave no-copy items behind.

Plans expire after ten minutes and are revalidated immediately before any
no-copy move. Folder copies are not server-side transactions, so results
include per-item errors when only part of an operation succeeds.

## Security

- The viewer binds only to IPv4 loopback.
- Every request requires the per-launch bearer token.
- Requests carrying a browser `Origin` header are rejected.
- Request bodies are size-limited and responses disable caching.
- The bridge must be explicitly enabled for each viewer launch.
- The bridge allowlist is `LLInventory`, `LLAppearance`, and `LLCamera`.
  Other Event APIs are rejected.
- The MCP sidecar never receives Second Life credentials or simulator
  capability URLs.

Any process running as the same desktop user may be able to read user-owned
files or viewer memory. The token prevents accidental cross-process access
and browser DNS-rebinding attacks; it is not a sandbox against malicious
software already running as that user.
