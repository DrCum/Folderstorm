# Bulk inventory review, session history, and limited undo

Planning only, for review item **8**. This document authorizes no implementation or changes to PR 1. The dependency is **unmerged PR 1 at `65e6ebd115`**, which supplies authoritative approval summaries, prepared replacement targets, trusted execution lifetime/revocation, and safe asynchronous link replacement. Implement on a branch based on the landed PR 1 changes, or an explicitly stacked branch targeting PR 1 while it remains unmerged; reconcile interfaces after its final merge revision. Do not assume those changes are already on the default branch.

## Outcome and first release

Show the exact proposed rename/move for each selected inventory object, including known skips. After execution, show readable per-object outcomes and the changes that can safely be reversed. Partial completion remains partial; neither execution nor undo is a transaction.

Start with `batchRename` and `batchMove` plans, at most 50 requested rows. Track assistant rename/move operations and operations launched through the new review UI; label the UI **Assistant inventory history** until native bulk actions are integrated. Ordinary manual edits are relevant to stale checks but are not silently collected as a complete viewer activity log.

The first inverse operations are:

- **Rename:** restore the prior name of an eligible item or ordinary folder, subject to current rename/modifiability rules. Copy permission is unnecessary: renaming a no-copy item does not relocate its unique copy. Exclude inventory links and protected/system folders from this first inverse path.
- **Move:** return a copyable ordinary inventory item to its original ordinary folder. Exclude folder moves, links, no-copy items, Trash/restore, and protected/special destinations from move undo. Previews can still describe otherwise supported forward moves, marking them **Undo unavailable**.

Do not undo copies, link replacement, wear/detach, asset contents, scripts/notecards, deletion, uploads, payments, snapshots, or `confirmCopy` no-copy relocation. Existing permanent-delete prohibitions stay intact. History is a recovery aid, not a backup or a way to bypass policy.

## Findings that determine the design

- `llinventorylistener.cpp::batchMove()` validates items/folders and then calls `changeItemParent()`/`changeCategoryParent()`, appending `ok: true` immediately. `batchRename()` calls `update_inventory_item()` or `rename_category()` with no callback. Their current results indicate requests were submitted, not that every server update completed.
- `LLInventoryModel::changeItemParent()` updates the local model optimistically and can silently refuse moves out of locked AO/bridge/favorites folders. Its parent change and an inventory observer notification are not server acknowledgment.
- AIS completion is normally UUID-only; successful `UPDATEITEM` does not necessarily provide a UUID. Null UUID alone cannot distinguish success, refusal, and an uncertain response. UDP paths have different confirmation behavior. Undo must not rely on this signal alone.
- PR 1's `FSAssistantPreparedOperation`, `fs_prepare_assistant_operation()`, approval floater, `ApprovalGate`/`ExecutionGate`, and trusted `FSAssistantExecutionContext` are reusable boundaries. They are not yet a public persistent mutation-plan service.
- `LLInventoryListener::changes()` and its generation cursor are a bounded inventory change feed. They do not attribute changes to an assistant request or prove remote completion, and must not be presented as an activity history.
- Current MCP tools in `inventory_extra.go` accept up to 50 rows and forward per-row results. Maintain their names, limits, older-viewer behavior, and result fields.

## Concrete defaults and limits

| Resource or behavior | First-release decision |
| --- | --- |
| Requested rows per forward or undo plan | 1–50, checked before filtering |
| Prepared plans per account/session | 32; expire after 10 minutes |
| Concurrent executing operations | 4 overall; at most 4 outstanding mutation requests per operation; one active writer per target UUID |
| Execution window | At most 30 seconds and within PR 1's remaining trusted request deadline, with 0.5 second reserved for response delivery |
| History | At most 100 operations, 5,000 rows, and 4 MiB of stored display/recovery data; oldest completed records expire after 2 hours |
| Display fields | Control characters removed; name/label capped at 256 UTF-8 bytes and folder path at 1,024 bytes, with visible shortening |
| Display pagination | 50 rows per page; exact requested/eligible/skipped counts always visible |
| Automatic large-change review | Optional setting, threshold 10 unique affected objects, disabled by default to preserve existing automation behavior |
| Retention boundary | In-memory only; clear on logout, account/grid/session change, viewer shutdown, or **Clear history** |

Keep a bounded active record until it becomes terminal; reject new work as busy when capacity cannot be honored rather than evicting live recovery state. Clearing the history removes its inverse metadata immediately and prevents an outstanding callback from restoring it. It does not cancel already submitted server work; offer a separate **Stop remaining work** action for active operations.

## Separate preview, permission, and execution

**Preview is a read-only dry run.** Preparation resolves viewer-owned objects, normalizes proposed values, and records exact IDs and expected before/after fields. It creates no inventory objects, reserves no permanent authority, and dispatches no mutation. Show plans even when execution is currently denied, with an explicit **Execution blocked by current permissions** state.

**Permission approval remains the existing Ask/Never/Allow policy.** Previewing or possessing a `plan_id` grants no permission. Execute always reevaluates the stored operation's mutating class: Edit for rename and Move for move. Required Never rejects; required Ask presents one approval over the frozen plan; Allow can execute an explicitly submitted plan without an additional permission dialog. Reuse PR 1's latched revocation and bridge/session lifetime checks.

**Review is a separate workflow choice.** The native **Review bulk changes** command always opens a preview. An enabled automatic-review setting opens it for operations affecting at least 10 unique objects, including when permission is Allow. Its Execute button is a one-request workflow decision, not a permission change. When Ask is also required, the same floater shows both review details and the one-request permission choice; do not display two successive dialogs. With automatic review disabled, legacy Allow calls keep their current direct-execution behavior. Documentation must state that direct legacy calls are not guaranteed to have a preview.

MVP exposes explicit preview tools rather than silently returning `confirmation_required` from today's batch methods. A subsequent native integration can route opted-in legacy batch calls through review. The preferences/preset plan owns permission presets; it does not turn this review setting on or off implicitly.

## Authoritative plan service

Add a small viewer-owned bulk-plan service, proposed `fsinventorybulkplan.h/.cpp`, shared by the listener, review UI, and bridge. Store a random session-bound ID, operation kind, normalized desired values, exact requested rows, creation/expiry, required class IDs, and a minimal validation snapshot. Do not retain caller JSON, arbitrary operation names, bearer tokens, script/notecard text, asset contents, or connection configuration.

Preparation must reuse the real rename/move validation rules: loaded agent inventory; trimmed valid names; actual item/folder identity and type; modifiability and Calling Card constraints; protected folder/COF rules; current RLV and AO/bridge/favorites locks; valid destinations; and folder self/descendant checks. Resolve names and paths from inventory rather than request labels. Mark unknown/unloaded rows honestly and keep them out of the executable set. A preview's skip reasons are predictions, not guarantees that all other rows will succeed.

Use before/after columns, original/destination folders, exact counts, no-op markers, and **Undo unavailable** reasons. First-release plans reject duplicate target UUIDs and ancestor/descendant overlaps in folder-move selections with a clear instruction to choose the parent or its children. Preserve duplicate-name distinctions by UUID and folder context. Do not silently rescan a search or incorporate items added after review. Mark a no-copy forward move as relocation of a unique item, without changing existing permission class mappings.

At Execute, revalidate the complete material snapshot before dispatch: session/expiry, identities/types, current names/parents, target resolution for links, destinations, and eligibility. A row becoming eligible or ineligible invalidates the reviewed plan; return `stale_plan` and require preparation again. Do not expand its scope or retry automatically. Once execution starts, revalidate each not-yet-submitted row; races become per-row skipped/failed outcomes. Account for this plan's own prior effects rather than treating its renamed parent label as an unrelated edit.

A plan moves through `prepared -> executing -> terminal`, and is consumed atomically on first dispatch. Repeating Execute for the same ID returns its existing operation ID/status, never dispatches it again. Cancellation before dispatch consumes no mutations; cancellation after dispatch stops only remaining steps. TTL expiry and history eviction do not grant a new right to replay.

## Public API and permission boundary

Proposed additive tools/events:

- `inventory_preview_batch_rename` / `previewBatchRename` and `inventory_preview_batch_move` / `previewBatchMove`: return `plan_id`, expiry, eligible/skipped counts, full bounded preview, and current execution availability.
- `inventory_execute_plan` / `executeBulkPlan`: consume a prepared forward or inverse plan and return per-row outcomes.
- `inventory_history` / `bulkHistory`: paginated session records or one operation's details.
- `inventory_preview_undo` / `previewBulkUndo`: construct a separate inverse plan over an explicit subset of eligible history rows; execution uses the same plan endpoint.

Names are proposed implementation contracts, not tools available today. Advertise a versioned `bulk_inventory_review` capability in authenticated status/health. The sidecar checks it before offering/using new operations. Older viewers return a clear unsupported-feature result; never fabricate a trusted dry run or silently fall back to immediate mutation. Existing tools remain available for explicit caller use.

Join the bridge's explicit operation allowlist and classification. Exported preview/history/undo-preview contains names and paths and **requires Read**. Resolve execution classes from the authenticated, viewer-owned plan after lookup; callers cannot supply required classes, candidate lists, expected snapshots, deadlines, or a `confirm` value that bypasses Ask. Bind plans to the selected viewer/account/grid/login session. Missing/foreign/expired IDs disclose no other session's records.

Execution retains its existing mutating classes; a later Read=Never does not itself grant or revoke Edit/Move. The new execute endpoint returns only IDs and outcome metadata when Read is denied, with no stored names/paths or name-bearing error strings. Legacy tools retain their existing fields, including requested new names, without adding private history labels when Read is denied. Local review UI may use authoritative local names under the same model as PR 1; do not expose that UI-only access through the new history/preview APIs. Recheck current Read before any exported history/detail response, including already prepared plans.

## Actual outcomes and bounded history

Add a focused asynchronous bulk executor, proposed `fsinventorybulkoperation.h/.cpp`, using a testable inventory backend, monotonic clock, exact selection, and PR 1's trusted execution context. It produces one stable row per requested object and one terminal response. Do not infer successful renames from a null/non-null UUID alone or infer completed moves from optimistic local inventory.

Use distinct states: `not_started`, `skipped`, `submitted`, `confirmed`, `failed`, `unconfirmed`, and `cancelled_before_submission`. A timeout after submission is uncertain; it is not a failure that can safely be retried. Disconnect, shutdown, Deny, and deadline stop new requests. Late callbacks may reconcile the existing operation record while its session/retention epoch remains valid, but may never restart execution or recreate cleared history.

Before exposing undo, implement and verify a narrow authoritative result adapter. Prefer a structured AIS completion containing HTTP outcome plus the affected object/result fields; preserve existing UUID callbacks for other callers. Prove whether the supported service accepts parent updates on that path before switching bulk move submission. A fresh server fetch can reconcile the original object after submission, but distinguish that response from the prior optimistic local mutation. If a supported UDP/OpenSim path cannot reliably confirm a row, keep it **Submitted/unconfirmed — undo unavailable** rather than disabling the viewer or guessing success.

History stores operation ID, local timestamp, origin, action kind, required mutating class, requested/eligible/skipped/submitted/confirmed counts, bounded display snapshots, target UUIDs, minimal before/after recovery fields, result state/reason, and inverse eligibility. It records no credentials, arbitrary raw requests, caller messages, or asset bodies. Keep it separate from bridge diagnostics and the inventory change feed. No disk persistence, automatic exports, or telemetry are added.

Preserve existing MCP `results` rows and their fields; add `operation_id`, `status`, counts, and `undo_available` metadata. During compatibility transition, document legacy `ok` as accepted/submitted where that was its meaning, and use the new status for confirmation. A row error must not become a top-level string error that discards mixed results in the Go forwarding layer. A lost client response can be inspected by operation ID without executing the plan again.

## Limited undo is another validated operation

Only confirmed rows with a captured before-state can produce an inverse plan. The button reads **Review undo**; show exactly which names/locations will change and which rows cannot be reversed. Never automatically reverse already completed rows when another row fails.

For rename, require the same object UUID/type and expected current name/parent, current rename permissions/locks, an eligible ordinary folder/item, and the original name. For move, also require current copy permission, exact expected current parent, unchanged relevant identity/name/type, and an existing original destination that passes current ordinary-folder policy. Do not substitute another folder when the original is missing. Links, no-copy moves, folder moves, Trash, and special folders remain ineligible.

Immediately before inverse submission, fetch/reconcile current authoritative state and compare against the expected after-state. Check server revision/version when it is meaningful, plus known subsequent viewer mutations; a later tracked write supersedes the older inverse even if a name later returns to the same value. Do not treat the global inventory generation as an object revision. Where the service lacks causal revision evidence, guarantee only a current-state-checked inverse over these limited fields; do not claim detection of every intervening edit from another viewer.

Undo reevaluates current Edit/Move and, when Ask applies, prompts once over the inverse plan. Per-row inverse races, lock changes, and network failures produce explicit partial outcomes. The inverse is itself a bounded history entry linked to the original; confirmed inverse rows make the original's undo unavailable. Submitted/unconfirmed inverse rows lock out duplicate inverse submission until reconciled. No redo or recursive undo-of-undo UI in this release.

## Owned implementation stages and dependencies

1. **Inventory backend owner — preparation and outcome foundation.** After PR 1, extract shared validation from the real batch paths into the bulk-plan service; add exact selection, TTL/session/duplicate handling, and capability/Read classification with the bridge owner. Add the status-aware completion/reconciliation adapter and focused backend tests before enabling inverse actions. Ship explicit dry-run endpoints first; existing mutation tools remain unchanged.
2. **Inventory backend owner — bounded execution and history.** Add the asynchronous executor, idempotent plan consumption, per-target reservations, bounded history service (proposed `fsinventoryhistory.h/.cpp`), retention/clear semantics, and additive legacy result metadata. Integrate tracked assistant rename/move calls at dispatch/submission, including Allow; do not require a preview merely to record outcomes.
3. **UI owner — review/history surfaces.** Reuse PR 1's paginated approval presentation where practical. Add a dedicated bulk review/history floater and XUI, **Review bulk changes** entry, navigation from an operation result to its history, and the separately labeled optional review setting with threshold 10. Register through `llviewerfloaterreg.cpp`, relevant inventory menus, and `CMakeLists.txt`; coordinate preferences with the item 5 owner. Include default, Vintage, and StarLightCUI skins and normal localization fallback.
4. **Inventory backend owner — limited inverse; MCP owner — additive tools.** Enable only the confirmed eligible inverse rows above, then expose preview/execute/history/undo-preview with capability negotiation and Read gating. Keep `inventory_extra.go`, `policy.go`, `viewerapi/policy.go`, the bridge classifier, README, and schema tests consistent. Integrate reviewed native bulk actions after the assistant path is stable.
5. **Integrator — compatibility and native acceptance.** Rebase against PR 1's final merged interfaces, register the new sources and portable tests, run affected existing tests once, and record the native checks below. Each stage can be a separate PR; preparation and history UI can progress in parallel only after the shared plan/result schema is agreed.

## Tests and acceptance evidence

- Deterministic plan tests against the real validator adapter: names with whitespace/Unicode/control characters, duplicate names/IDs, 50 versus 51 requested rows, no-ops, invalid/unloaded objects, Calling Cards, protected folders/COF, RLV/AO/bridge/favorites restrictions, self/descendant moves, overlapping folder selection, and expired/foreign plans. A dry run sends zero mutation requests.
- Execute/approval tests: any required Deny blocks even with a forged plan/confirm; Ask uses one UI; Allow executes explicit plans without a permission prompt; opted-in large review appears at 10 objects even for Allow; disabled review preserves legacy direct behavior. Changed names/parents/targets/destinations/eligibility invalidate the whole prepared scope before dispatch. Permission revocation stays terminal after Deny→Allow.
- Async tests: callbacks out of order/duplicated/lost/late, explicit server rejection, UUID-only ambiguous completion, silent local move refusal, cancelled/expired context before dispatch, timeout within PR 1's remaining budget, logout/clear-history/eviction, and concurrent same-target execution. Observe actual submissions and terminal outcomes; never equate a local observer update with confirmation.
- History/privacy tests: caps, 2-hour expiry, UTF-8 truncation, paging, clear during active work, no resurrection from late callbacks, Read denial before preparation/detail export, and absence of tokens, script bodies, arbitrary JSON, caller messages, and asset contents. Clearing or expiry removes all corresponding inverse metadata.
- Inverse tests: confirmed rows only; rename works without Copy permission; no-copy/link/folder/special/Trash moves remain ineligible; current after-state or revision change, superseding viewer action, missing old folder, policy/lock change, and uncertain forward/inverse completion block unsafe replay. Partial undo stays partial and does not dispatch compensation for other rows.
- MCP tests with a mock viewer: existing tools/schemas and 50-row behavior, additive structured partial results, capability absent/unsupported, Read-separated preview/history, policy resolved from stored plan, repeated execute without repeat mutation, and session-bound operation lookup. Run `go test ./...` in `tools/fs-mcp` plus the affected portable C++ plan/executor/history and existing assistant/link tests.
- Native checks: disposable inventory only; default/Vintage/StarLightCUI, minimum floater size and multiple UI scales, long localized labels, keyboard/pagination, Ask/Allow/Never, preview at threshold, Clear/Stop distinction, partial failures and relog verification on a supported SL grid and OpenSim/UDP configuration. Verify confirmed versus unconfirmed status against fetched remote state and ensure unsupported confirmation paths honestly disable inverse actions.

Acceptance: displayed plans match the selected work; Read protects exported inventory labels; execution never gains authority from a plan ID; history accurately distinguishes submission and confirmation; eligible undo changes only validated names/parents; and uncertain or stale rows never trigger an automatic replay or a claimed transactional rollback. Planning supplies no graphical or network confirmation evidence; those remain implementation acceptance checks.
