# Contents copy receipts with hidden asset identities

`object_contents_put` cannot equate the source inventory item UUID, the source
asset UUID, and the destination task item UUID. Second Life can hide either
asset UUID, including for an ordinary copyable `AT_OBJECT` containing scripts.
A new server-observed item is evidence of presence, not proof of which request
created it. Names, timing, a single new candidate and two null assets are not
content-identity evidence.

## Native evidence and recovery

`LLViewerObject::updateInventory()` sends `UpdateTaskInventory` with the source
item and its existing transaction ID. It then makes an optimistic local update.
`ReplyTaskInventory` reports only the prim, inventory serial and transfer filename.
The HTTP `RequestTaskInventory` reader loads an inventory snapshot and serial,
without a per-copy transaction-to-destination-item result. Reliable UDP delivery
is not an application-level copy acknowledgment. Do not invent a new asset
transaction or assume the server preserves the source item UUID as the task UUID.

Verified visible assets and eligible hidden script assets keep their existing
automatic correlation path. Hidden object assets have no authorized script-source
resolution path. Their receipt can therefore remain `submitted_unknown` with
`source_asset_undisclosed`. A timeout/revocation retains that submitted uncertainty;
its cancelled guard and finished observer cannot establish a later asset proof.
Polling never resubmits work, so the original implementation had no supported
recovery short of clearing the process-local journal by relogging.

The new `object_contents_reconcile` operation makes an explicit local association
from an original retained put receipt to a selected task item in a fresh Contents
snapshot. It does not send a simulator message or retry the original copy. It
requires current inventory Read, World read, task Contents read/write permissions
and the exact source login. The new `contents_reconciliation: 1` capability keeps
older viewers from receiving the operation.

The selected UUID must be owned, of the original source's native asset type,
absent from the pre-submission inventory UUID set, and present unchanged in a
complete server snapshot newer than submission. The snapshot must still match
the current prim/root/local-ID/region and inventory evidence at approval and
dispatch. A known conflicting asset UUID is rejected, including an eligible
item-bound script-read result whose item evidence still matches. Independently
read script source snapshots also reject conflicts while their login, epoch,
expiry, native/RLVa read permission and exact object/item evidence remain current.
A matching cached read cannot mask a conflicting current read. Multiple candidates
remain an explicit choice; the viewer never picks the first candidate.

The original operation ID and receipt remain addressable. Its new state is
`associated_unverified`, correlation `explicit_selection`, with
`asset_identity_verified=false` and `copy_outcome=unknown`. `destination_present`
describes the selected item at reconciliation time, not its continuing presence.
The earlier state, correlation and reason are retained in `*_before_reconciliation`.
This says nothing definitive about asset identity or which request created the
item. The association is immutable and never replaces an already verified result.

Only this put's exclusive mutation lock is released. Outstanding reads retain
the receipt and drain independently; callbacks cannot alter the chosen result,
submit the original copy, or release another operation's lock. Reconciliation
reserves a replay tombstone without needing a new live receipt slot. Repeating
the same reconciliation UUID with identical arguments returns the recorded
receipt, even after that snapshot expires. Changed arguments or another choice
are refused. Bounded receipt eviction still applies; retired copy/reconciliation
request UUIDs cannot be used to resubmit native work.

## In-world reproduction and recovery

Use a future matching viewer/sidecar built from this patch. No viewer or installer
was built for these source checks. Use a disposable owned modifiable QA prim and
an owned copyable/modifiable inventory object with scripts. Do not use FFAP's
spawner, project objects, or the incident's IDs.

1. Call `viewer_list`, then `viewer_status` with the exact returned
   `viewer_session`. Confirm `contents_reconciliation: 1` and the native permissions.
2. Call `object_contents_list` on `QA_PRIM`; save its `snapshot_id` as `S0` and
   record the preexisting task item UUIDs.
3. Submit exactly once:

   ```json
   {"tool":"object_contents_put","arguments":{"viewer_session":"LOGIN","object_id":"QA_PRIM","snapshot_id":"S0","source_item":"INVENTORY_OBJECT_ITEM_UUID","client_request_id":"NEW_COPY_REQUEST_UUID"}}
   ```

   Retain its original `operation_id` as `O`. The source item UUID is not an
   asset UUID and is not a destination task UUID. Never retry an uncertain copy.
4. Read `object_operation_status` for `O` and fetch fresh Contents. When the source
   asset is hidden, expect the uncorrelated receipt and an observed new object
   item. Save the new snapshot as `S1`; inspect the exact new task UUID `D`.
   A rename attempt on disposable `D` can demonstrate the refusal naming `O`.
   If several candidates exist, inspect and deliberately choose one; do not infer
   identity from names or list order. If no defensible choice exists, leave it
   unresolved and retain the evidence.
5. Request the explicit association with a different fresh request UUID:

   ```json
   {"tool":"object_contents_reconcile","arguments":{"viewer_session":"LOGIN","object_id":"QA_PRIM","operation_id":"O","item_id":"D","snapshot_id":"S1","client_request_id":"NEW_RECONCILIATION_REQUEST_UUID"}}
   ```

6. Check that the response still identifies `O`, reports `associated_unverified`,
   `copy_outcome=unknown`, `asset_identity_verified=false` and
   `mutation_blocking=false`. Read the original receipt again. Repeat the exact
   reconciliation request and verify it does not copy another item or change the
   association. A different task UUID or reused request UUID for a copy must fail.
7. Fetch another fresh snapshot `S2`. Rename disposable `D` with
   `object_contents_rename`, a new edit request UUID and `S2`. Inspect the edit's
   own receipt. Eligible scripts in that QA prim can then use their ordinary
   fresh-snapshot running-state APIs; their independent permission/query guards
   still apply.
8. Also test two manual additions, stale/expired snapshots, optimistic local
   inventory changes, timeout before association, revoked permissions between
   approval and dispatch, and a different login. These must never make an
   automatic association or resubmit the copy. For a script candidate, let the
   put time out, read that candidate with `object_script_get`, and then attempt
   reconciliation. A still-valid source snapshot proving a different asset must
   refuse association without consuming its reconciliation UUID or releasing the
   original lock. Capture all original receipts
   before relogging. Clean up only fixtures you are authorized to remove.

Focused automated coverage compiles the production preparation, dispatch,
validation, timeout, observer, callback and settlement methods with bounded
scene/transport/serialization fixtures. Public MCP tests check the schema,
capability, source selection, permission checks and dispatch. This does not
replace a future matching-build simulator acceptance run.
