# Local assistant inventory review and recovery

The authenticated viewer bridge advertises `capabilities.bulk_inventory_review: 1` in normal inventory status, inventory status denied by Read, and assistant health. The additive MCP tools require this capability. Existing rename/move tools keep their names and result fields; they also create session history when called through the assistant bridge.

Preview and history endpoints require Read. Execute derives Edit or Move from the stored plan, checks current policy, and uses one frozen-plan approval when Ask applies. A caller's `confirm`, candidates, or class list grants no authority. Possession of a plan ID does not enable execution. Native review uses local inventory labels and an explicit Execute button as one-request consent. The optional large-operation review setting defaults off; at ten requested objects it uses the same approval as Ask.

Plans contain the exact requested rows, predicted skips, normalized names, original parents, and destinations. Pending plans expire after ten minutes; consumed plans cannot dispatch changes again. At most 32 pending plans and four active operations are retained; consumed plan IDs are bounded by retained history. Each operation admits at most four requests at once and finishes within 30 seconds or the remaining bridge deadline. Repeating Execute returns the same operation's current outcome without another dispatch or approval.

History remains in memory for this login session: at most 100 operations, 5,000 rows, and 4 MiB reserved for bounded recovery data. Completed records expire after two hours. Display labels are capped at 256 UTF-8 bytes, paths at 1,024, and control characters are removed. No asset bodies, credentials, caller messages, or raw requests are stored. **Clear history** removes plans, labels, and inverse metadata without undoing or stopping submitted changes. **Stop remaining work** cancels queued changes and remains effective for a retained active operation ID after Clear.

The row status tells what is known:

| Status | Meaning |
| --- | --- |
| `not_started` | Waiting or performing a read-only preflight; no mutation submitted. |
| `skipped` | Current validation or authoritative preflight did not permit submission. |
| `submitted` | A mutation request was dispatched. |
| `confirmed` | Successful AIS update plus a separate authoritative fetch matched the desired object state. |
| `failed` | The server explicitly rejected the single-object update. |
| `unconfirmed` | Submitted work may have completed, but its outcome is uncertain. |
| `cancelled_before_submission` | This row's mutation was not submitted. |

Legacy `ok` retains accepted/submitted semantics; use `status` to distinguish confirmed completion. Network failures or timeouts are never retried automatically. An unconfirmed target remains reserved against duplicate execution until its real callback reconciles it or the login session ends. Clear cannot bypass active execution limits or these reservations.

**Review undo** prepares another exact plan. Only confirmed ordinary renames and ordinary copyable-item moves between ordinary folders qualify. Folder moves, links, no-copy moves, Trash and special-folder moves do not. The inverse requires unchanged current identity/name/parent and current permissions and locks, including RLV and AO/bridge/favorites protections. Known subsequent material changes observed in this viewer invalidate older inverses, even when a name or parent later returns to its former value. This includes other writes while the original request awaits confirmation: the frozen external-writer epoch is preserved across this object's own AIS cache updates and readback. AIS provenance is fiber-local and restores nested scopes within that fiber, so a yield during parsing cannot mask native edits or another coroutine's writes. Confirming the final fields never refreshes authority over an intervening edit. There is no server causal revision guarantee: changes made and reversed entirely by another viewer may not be detectable. Inverse submission supersedes the original inverse; there is no recursive undo or redo.

AIS submission uses the existing item/category PATCH endpoints with `name` or `parent_id`. The existing viewer full-object AIS update methods already include these fields. This does not establish live grid acceptance of every narrow request: native SL/OpenSim acceptance remains a required check. Successful HTTP status alone, an optimistic inventory observer update, and a UUID-only callback never enable undo. Supported UDP/OpenSim forward paths retain existing mutation behavior and report unconfirmed, with undo unavailable.

The production store/executor has standalone regression tests in `indra/newview/tests/folderstorm`. Coverage includes consumption, scope/session/TTL limits, ordering and duplicate callbacks, bounded concurrency, cancellation before versus after real submission, late reconciliation, Clear/Stop behavior, inverse eligibility, and display bounds. Viewer adapters require native acceptance on disposable inventory for normal/Ask/Never policies, mixed results, relog verification, SL AIS and OpenSim/UDP, UI skins/scales, and protected folders. No graphical or live grid verification is claimed by the portable checks.
