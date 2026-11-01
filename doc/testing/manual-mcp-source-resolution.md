# MCP source reads with undisclosed asset UUIDs

A task or inventory item UUID identifies the script entry. Its cached asset UUID
may be zero even when the native editor can download existing source. Native
`getInvItemAsset` resolves source through owner, task and item IDs and supplies the
actual asset UUID to its callback. Opening the native editor stores that UUID in
the editor; it need not update the inventory item's metadata.

The MCP source adapters now use that same route. Source snapshots retain the
resolved asset ID separately from unchanged native item/object evidence. Callers
must use `asset_id`, `content_hash` and (for task scripts) `source_snapshot` from
the complete source read when requesting preview/save. Never substitute an item
UUID or a zero catalog asset ID. No source is synthesized for a missing asset;
a failed download still blocks editing. Server revision checks remain best-effort,
not atomic.

Task Contents entries expose `asset_id_known` when source access is permitted.
A denied entry exposes `source_read_blocker`: `task_item_missing`,
`exact_script_required`, `script_owner_required`, `native_copy_required`,
`native_modify_required`, or `rlva_viewscript_restricted`. Target ownership,
object-editing restrictions and assistant permissions also remain enforced.

## Focused automated checks

From `indra/newview/tests/folderstorm`, configure the standalone CMake project.
Build `assistant_object_tools`, `assistant_script_tests`, `content_policy`,
`content_upload`, `content_read_permissions`, and `content_notecard_v1`, then run
those tests plus `assistant_object_native` and `content_source_native`.

`test_fscontentsource_native.py` compiles production eligibility, download
callbacks, edit-snapshot validation and preserve-running validation against
bounded inventory/asset fixtures. It uses production async guards and content
policies; LLSD, transport, hashing, and storage are fixture boundaries. It covers
unknown and known asset IDs, returned-ID mismatches, stale metadata/session/
privacy/permissions, cancellation, incomplete/invalid/empty/oversized source,
source snapshot/hash requirements, preserved running state, legacy inventory
read callbacks, receipt conflict checks and test-report source freshness. It does not
exercise real simulator transfer, upload submission, or Windows native UI.

## Windows / Second Life acceptance

Use a matching rebuilt viewer/MCP and a disposable owned, modifiable cube and QA
inventory folder. Obtain explicit authorization before modifying any existing
project scripts.

- With a script whose native editor opens successfully but whose Contents asset
  UUID is zero, close the editor without saving. Fetch fresh Contents, then call
  `object_script_get` using that exact object, item and snapshot. Source should
  match the editor, with a nonzero resolved `asset_id` and complete edit snapshot.
- Preview an unchanged source using the returned snapshot/hash/asset. On a QA
  script, save a small explicit change stopped; inspect `source_saved`, `compiled`
  and diagnostics separately. Reopen the native editor to verify the result.
- Query running state and repeat a QA save with `running_mode=preserve`, including
  when the source asset ID is resolved but cached Contents metadata remains zero.
- Create a fresh inventory script. `inventory_content_get` should resolve its
  server default source; preview/save using that complete read. A truly absent
  source must fail clearly rather than assume the previous source was empty.
- Repeat reads for known asset IDs and an editable notecard. Verify native
  COPY/MODIFY restrictions, RLVa View Script, owner restrictions, open-editor
  busy checks and Assistant Deny/Ask/Allow retain their existing behavior.
- Change/delete the task item, refresh Contents, log out, or revoke source access
  while a read is pending. No stale source or reusable edit snapshot may escape.
  Saving against an old item/source snapshot must fail without submission.
