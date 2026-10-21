# MCP local-file uploads

Follow-up above #78. Preserve published history and existing source-bound
permissions. Uploading files never logs in, switches characters, wears assets,
plays sounds/animations, rezzes objects or executes scripts.

## Tools and scope

1. `inventory_upload_preview` accepts explicit absolute local files, or one
   directory. The local MCP sidecar enumerates deterministically, rejects links
   and special files, bounds depth/count/bytes, and supplies SHA-256 and relative
   folders. The viewer independently reads and verifies immutable bytes. Preview
   returns every planned file, destination, native encoding, account-specific
   price, permission defaults and expiration. Unsupported files refuse the
   entire preview with an actionable error; nothing is silently skipped.
2. `inventory_upload_execute` consumes an exact source-bound plan once. Images
   (PNG/JPEG/TGA/BMP), WAV sounds and native ANIM/BVH animations use native codecs
   and `NewFileAgentInventory`. Directory imports recreate relative folders
   beneath the chosen private inventory destination. Submission is sequential;
   a failure stops unsent work and preserves completed IDs. No automatic retry.
3. `inventory_upload_status` reports the same plan's per-file outcomes, created
   folders, confirmed IDs, failures and uncertain submissions. Cancellation
   stops unsent work; it never claims to undo server-accepted uploads or charges.
4. Mesh uses `inventory_upload_mesh_review`: load an explicit DAE into the native
   mesh importer for LOD/physics/rigging review and native server fee calculation.
   Opening review does not upload; the resident explicitly completes the native
   importer. Do not invent a mesh quote from texture pricing or claim an opened
   preview is an uploaded asset. The tool refuses an already occupied importer.

## Prices and authority

Use `LLAgentBenefitsMgr::current()` for the actual account's texture, sound and
animation fees, including free Premium uploads and dimension-specific texture
fees. An all-zero batch with Upload/Create set to Allow needs no confirmation.
Ask permissions still produce their ordinary authorization dialog. Any positive
batch price always requires the native price confirmation, even with Allow.
Freeze total/per-file costs before approval; recheck benefits and server-provided
prices before transferring data. Increases stop unsent work and require a new
plan/approval. Never interpret a missing or negative quote as free.

Add a separate Ask-by-default Upload permission and an additive `file_uploads`
capability. Require matching viewer/sidecar and explicit viewer_session. Bind
plans and receipts to account, grid, native login, worker and bridge epoch.
Carry durable authority through folder callbacks, upload queue, capability POST,
binary transfer, completion and local inventory commit. Respect pause/revocation
at each boundary. Repeated execute returns the original operation, never resends.
Reject protected/library/trash destinations. Apply native upload permission
defaults without changing Preferences.

## Bounds and validation

Start with 32 files, 64 MiB per file, 128 MiB total staged bytes, depth 8, four
plans, 64 created folders and 32 receipts. Keep source data in session memory; no persisted local
paths or content logs. Release staging on expiration/completion/account change.
Copy source bytes into unique temporary native-codec inputs; delete on every
exit. Retain encoded cache artifacts only while needed. No URL inputs or external
resource traversal in the automatic uploader.

Focused tests cover real registered MCP dispatch, sorting/enumeration/path and
hash limits, zero-price Premium eligibility, paid confirmation, increased price,
exact-source changes, one-shot execution, partial failure, revocation between
folder/capability/transfer stages and unknown outcomes without retries. Compile
portable models with strict warnings; run affected Go tests/vet and native source
contracts. Independent Sol 6.1 Extra High review follows implementation. No broad
viewer/packaging/GitHub builds. Windows native codec, Premium/server fees, folder
import and mesh review acceptance remain explicit until tested.

## Implemented checkpoint

The automatic types, directory workflow, price gates, durable status/cancel and
native mesh review above are implemented. Encoding uses immutable verified bytes;
source paths are not retained in automatic plan receipts. Exact created-folder
ancestry is rechecked by the durable guard at queued AIS submission. Missing server
quotes refuse new-file binary transfer, without changing existing notecard/script
update endpoints that legitimately omit a price. Animation validation removes
its temporary decoded keyframe cache. Read the [validation checklist](mcp-file-uploads-validation.md)
for completed focused checks and pending native acceptance.
