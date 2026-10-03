# Safe asynchronous inventory link replacement

Planning only; no implementation is authorized by this document. Covers review item 9. Coordinate with `assistant-permissions-and-approvals.md` for the prepared operation and trusted execution context.

## Goal and scope

For each accepted inventory link, create and verify its replacement before submitting the old link's move to Trash. A failed, uncertain, cancelled, or late creation must leave the original alone. Return one final response with accurate per-link results, including partial success and remaining originals.

Keep `inventory_replace_links`, its current arguments, and the `LLInventory.replaceLinks` event. Preserve the existing confirmation behavior and top-level `count`, `ok`, and `results` fields; add diagnostic fields. Do not introduce an inventory transaction service, undo feature, persistent journal, or general operation-status API in this change.

## Source findings

- `LLInventoryListener::replaceLinks()` in `indra/newview/llinventorylistener.cpp` gathers links outside Trash, invokes `link_inventory_object(parent_id, target_id, nullptr)`, immediately calls `gInventory.changeItemParent()`, and appends `ok: true`. It checks the Trash destination but never validates each replacement parent or the old link's move restrictions.
- `link_inventory_object()` / `link_inventory_array()` in `llviewerinventory.cpp` resolve linked targets and choose item/folder link types. A missing target can return without firing the callback. Unsupported link types can produce an empty creation request. Preflight must reject these cases explicitly.
- AIS `CREATEINVENTORY` completion in `llaisapi.cpp::InvokeAISCommandCoro()` applies inventory updates before callback delivery, reports created UUIDs, and calls back with a null UUID when no created item was returned. Its general callback interface is UUID-only. UDP creation uses `LLInventoryCallbackManager` and may never callback on failure.
- `LLInventoryModel::changeItemParent()` optimistically updates local inventory after submitting the server move. It has no completion callback and can silently refuse moves from locked AO, bridge, or wearable-favorites folders. Seeing the local parent change is **not server acknowledgment**.
- Existing `LLInventoryListener::move()`, `validateDestination()`, and `get_is_item_removable()` provide relevant restrictions. These include agent inventory, special destinations, protected COF/worn items, folder locks, and RLVa removal/move rules.
- `tools/fs-mcp/internal/mcptools/inventory_extra.go::inventoryReplaceLinks()` forwards the viewer result through `callOrConfirm()`. `viewerapi/client.go` treats a nonempty top-level `error` as a failed call. Individual result errors need to remain in `results` for mixed outcomes to survive unchanged.
- The bridge allows a 60-second approval inside a 70-second chain guard; its HTTP chain has a 75-second budget. The Go client defaults to 90 seconds. Execution cannot safely assume a fresh 30-second window after approval.

## Proposed design

### 1. Prepare the exact selection without mutation

Implement replacement preparation/validation behind `LLInventoryListener`, consumed by the shared `PreparedAssistantOperation` described in the permissions plan. Both the bridge and direct confirmed Event API path use the same preparation logic.

Preparation must:

1. Require logged-in, usable inventory with the relevant tree loaded. Reuse the existing inventory-completeness/background-fetch approach; do not advertise "every link" when only a partially loaded tree was searched. Fetch within the remaining budget or return `inventory_not_ready` before mutation.
2. Require non-null source/target IDs. Keep support for a missing source object when loaded, broken links still reference its UUID; report the source UUID/name availability honestly. Require an available, linkable target. Resolve target links using existing link semantics, capture the effective linked target and link kind, and reject broken targets and same-effective-target requests as a no-op or clear validation error before creation. Prefer a successful zero-change no-op.
3. Collect matching agent-inventory links, excluding Trash. Capture immutable records: old-link UUID, original parent UUID, linked source UUID, actual link kind, and original name/description for summaries and stale checks. Capture requested/effective target IDs, target kind, Trash UUID, and account/session identity.
4. Validate each parent with the existing destination policy. Skip unsupported special folders rather than bypassing them; show skipped counts/reasons in the approval summary. Validate old-link removal with `get_is_item_removable(&gInventory, id, false)` and RLVa `canMoveItem(id, trash_id)`. Check existing AO/bridge/favorites locks through that helper. Validate insertion into the destination against the applicable existing inventory/RLVa add rules, reusing a supported helper rather than inventing different rules.
5. Produce the names, paths, eligible/skipped counts, target descriptor, and consequence "Create a replacement in the same folder, then move the old link to Trash" for the approvals plan. The prepared object owns the exact accepted candidate IDs, not a later live search.

Before execution, validate this selection again. If matching links were added, removed, reparented, retargeted, or the effective target changed while approval was pending, return `stale_approval` before any mutation and require a fresh request. Never quietly enlarge the accepted set. Once execution starts, per-item races become explicit per-item results; do not restart the entire operation automatically.

### 2. Own asynchronous state separately from the listener

Add a small reference-counted operation state, preferably `FSLinkReplacementOperation` in a focused source/header pair. It owns immutable request/selection data, ordered result slots, a monotonic deadline, a session guard, and a weak cancellation/authorization token. Never capture raw inventory item pointers or an unprotected `LLInventoryListener*` across callbacks.

Use a bounded queue (initially four concurrent single-link creation calls) so a large inventory does not flood AIS or UDP. Emit results in selection order, regardless of callback order. Maintain one terminal flag for the operation and each link, and release timers/observers when terminal. Callback ownership must not form a reference cycle; callback destructor is cleanup, not evidence of success or failure.

Per-item progression:

`queued -> creating -> replacement_verified -> trash_submitted`

Terminal alternatives include `skipped`, `creation_failed`, `creation_unconfirmed`, `original_changed`, `permission_revoked`, `cancelled`, and `trash_not_submitted`. Store the created link UUID whenever known, including cases where the original remains.

### 3. Verify creation, then recheck the destructive follow-up

Immediately before dispatch, re-fetch the old link by UUID and validate its captured parent/linked source, target availability/kind, parent policy, current restrictions, and execution context. Then call `link_inventory_object()` with a non-null callback for **one old link at a time**; do not use a multi-link create whose returned UUIDs cannot be paired reliably with old links.

When callback arrives:

1. Ignore duplicate terminal callbacks. A null UUID is creation failure; leave the old link alone.
2. Look up the returned UUID and verify it is a link in the expected parent with the expected effective target and link kind. If the item is not yet visible (particularly a UDP ordering case), use a bounded inventory observer/fetch to await the returned UUID; its mere presence as a UUID is not enough. Do not identify a replacement by matching only its name or folder contents.
3. Recheck the live session, deadline, cancellation/authorization token, original parent/linked source, and all removal/move/parent restrictions. If anything changed, preserve the original and return the replacement UUID for recovery.
4. Submit `changeItemParent(old_link, trash_id, false)` only after all checks pass. Verify whether the local model accepted the transition; silent rejection becomes `trash_not_submitted`. Do not purge either link or automatically roll back a valid replacement on a follow-up failure.

Preserve current replacement metadata behavior for this first change: the existing link helper derives replacement metadata from the target. Do not add outfit-order/description migration in the same patch; document it for manual verification so the safety fix does not silently alter existing semantics.

### 4. Return truthful results without claiming server-confirmed Trash moves

Keep each existing result's `id`, `parent_id`, `ok`, and optional `error`. Add `new_id`, `status`, `original_preserved`, `trash_state`, and `retry_safe` where relevant. Add top-level `operation_id`, `completed_count`, `failed_count`, `skipped_count`, and `partial`.

- `count` remains the number of selected matching links; skipped candidates remain visible in `results`.
- Per-item `ok: true` means the replacement was verified and the viewer accepted submission of the old link's Trash move. Report `status: "replacement_created_trash_submitted"` and `trash_state: "submitted"`. Describe this meaning in the tool documentation; do not label it server-confirmed completion.
- Per-item `ok: false` identifies failure, cancellation, uncertainty, or skipped mutation, with a stable status/error and known `new_id` if any.
- Top-level `ok` should mean every selected link reached the successful submission state, including `true` for a valid zero-change operation. Mixed/all per-item failure returns `ok: false` plus the complete `results`, without a top-level string `error`; this keeps the Go forwarding layer from discarding the structured outcomes. Preflight failures before mutation may use the existing top-level error convention.
- A timeout reports `creation_unconfirmed` rather than "creation failed" when the request may have reached the server. `original_preserved` states what this operation did; avoid asserting that another user/process never moved or removed the original.

Server-confirmed Trash completion is a separate extension: it requires an authoritative fetch/response-aware move path, not polling the optimistic local parent. It is not required to fix the unsafe create-before-trash ordering, provided the submitted/confirmed distinction is exposed plainly.

### 5. Deadlines, cancellation, disconnect, and retries

The bridge/permissions owner supplies a trusted C++ execution context with the remaining chain deadline and a weak session/cancellation token. These values must never be accepted from client JSON. A direct Event API operation uses a bounded local execution deadline. Reserve a small response-delivery margin; do not dispatch creation when there is no useful budget left.

- On deadline, stop dispatching, mark queued items `not_started`, unresolved requests `creation_unconfirmed`, and send exactly one final structured response while the bridge can still deliver it.
- On logout, disconnect, bridge disable, viewer teardown, or revocation of a required permission, stop follow-up mutations. Outstanding network creation cannot necessarily be cancelled. A late callback may update diagnostics but must never trash an original after the operation is terminal. Account/session identity prevents callbacks from a prior login mutating a new session.
- A client HTTP/MCP cancellation is not presently a reliable viewer cancellation signal. Do not claim cancellation rolled back work. Use the shared bridge request-lifecycle hook if available; otherwise enforce the viewer deadline and document this limitation.
- Do not add automatic sidecar retry. An `operation_id` identifies the attempt for its returned results, not a claim of durable idempotency.
- Prevent simultaneous operations from creating replacements for the same old-link UUID with a per-session in-flight reservation. Retain a conservative reservation for creation with an uncertain outcome until its callback or an authoritative reconciliation resolves it; a second attempt returns `replacement_pending`/`recovery_required` for that link. Bound reservation count and reject new work at capacity rather than evicting unresolved records and silently allowing duplicate creation.
- Clear session records on logout. Across a viewer restart or lost final HTTP response, no durable retry guarantee is provided. Return `retry_safe: false` for uncertain rows, and tell the assistant to inspect inventory/new IDs before retrying. A persisted request-key journal and recovery/status tools are deferred unless this first implementation demonstrates a practical need.

## Implementation slices and ownership

1. **Shared preflight integration:** agree on `PreparedAssistantOperation`, exact-selection validation, deadline/cancellation token, and result semantics with the permissions/approvals implementer. Expose only narrow prepare/validate/execute hooks; that plan owns prompt rendering and required-class policy. This plan owns replacement candidate validation and execution.
2. **Executor and tests:** add the operation state and a testable adapter for create/lookup/move/clock/context checks; wire `replaceLinks()` to asynchronous `sendReply()` and remove the stack `Response` success path once async execution starts. Preserve synchronous validation/confirmation responses. Wire new sources/tests through `indra/newview/CMakeLists.txt`.
3. **MCP contract/documentation:** update `inventory_extra.go`'s description and `tools/fs-mcp/README.md`/`doc/fs-mcp.md` as needed for partial outcomes, `trash_state`, and retry caution. No argument/schema change is needed. Add a mock-viewer forwarding test to prove rich results survive the Go layer.
4. **Viewer integration verification:** use test inventory with disposable links, exercise normal AIS and supported UDP/OpenSim behavior, then verify the original/replacement state after relog. No destructive tests on real wardrobe links.

## Meaningful checks and acceptance criteria

Use deterministic executor tests with delayed callbacks and a fake clock. Assert observable actions/results, especially the absence and ordering of Trash submissions:

- No Trash submission before a successful callback and matching new-link lookup; null callback, wrong target/parent/type, missing UUID, unsupported target, missing target, and no callback preserve the original.
- Out-of-order/duplicate callbacks give one stable result per selected link and one final reply. Large sets keep concurrency bounded.
- Original moves/disappears/changes target after dispatch, parent deletion, RLV restrictions, protected COF, and AO/bridge/favorites locks prevent follow-up mutation; known replacement UUID remains reported.
- Shared preflight rejects stale approval selection before mutation; links added while approval is pending never slip into execution.
- Mixed success, zero matches, skipped-only, same-effective-target, and all failure retain correct counts and existing response fields. A mixed result survives Go forwarding without losing rows.
- Deadline immediately before approval completion, during create, and between verified creation/Trash submission leaves unresolved originals untouched. Late callback after final response/logout/disable/revocation cannot submit a move.
- Concurrent same-link attempts are blocked; uncertain attempts stay reserved until resolved. No automatic retry causes a second create.
- Local move rejection is reported as failure; accepted optimistic move is explicitly `trash_state: submitted`, never server-confirmed.

Run the focused C++ test target, existing relevant inventory/policy checks, and `go test ./...` in `tools/fs-mcp`. Validate creation/move results after relog on a supported grid. Acceptance: every Trash submission has a previously verified replacement; each selected link has one accurate terminal result; uncertain operations do not trigger destructive late follow-up; current MCP callers still receive the existing fields.
