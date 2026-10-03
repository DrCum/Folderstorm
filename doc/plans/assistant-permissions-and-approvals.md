# Local assistant permissions and approvals

Status: implementation plan only; review items 2 and 3. No viewer or MCP code has been changed by this planning work.

## Outcome and scope

The bridge must enforce every permission a snapshot upload needs, honor permission revocation while a request awaits approval, and explain the concrete operation before the user approves it. Keep existing preference keys, permission defaults, permanent-delete prohibition, and older-viewer compatibility. Do not add presets, an activity history, a general bulk-operation preview system, or a new public summary endpoint.

This can be handed to one implementation agent. Implement the permission correction first, then the preparation/approval work. Coordinate the replacement preflight and execution handoff with [the link replacement plan](safe-link-replacement.md); that feature owns its asynchronous executor.

## Verified current behavior

- `fseventapibridge.cpp`: `classify()` returns one class. `State::dispatchSnapshotUpload()` bypasses that classifier and checks only Edit for thumbnail or Create for texture. Both paths capture a picture; the texture path also assigns the uploaded asset as the target's thumbnail in `LLInventoryListener::snapshotUpload()`.
- `FSEventAPIBridge::notePermissionClassesChanged()` only increments `gPolicyGeneration`. `State::resolveAsk()` checks enablement and timeout, then posts the stored request without rechecking permissions.
- `target_label()` does not handle batch `ids`, batch rename `items`, `plan_id`, or `target_id` meaningfully. Its fallback frequently displays raw UUIDs; request-supplied names can replace the identity of the existing object.
- `LocalAssistantConfirm` in `skins/default/xui/en/notifications.xml` displays a title/message with Yes/No buttons. Existing queue limits are 60 seconds after showing and 70 seconds total; avoid introducing additional network-fetch delays into that budget.
- `LLInventoryListener` already has `object_summary()`, `make_path()`, `no_copy_moves_to_llsd()`, private `mCopyPlans`, and execution-time validation for `confirmCopy()`. Replacement selection currently happens inside `replaceLinks()`.
- The sidecar's `buildSnapshotUploadParams()` and `inventorySnapshotUpload()` in `tools/fs-mcp/internal/mcptools/inventory_extra.go` select only one permission too. `mcptools/policy.go::openClass()` already preserves compatibility when an older viewer omits the permissions object.

## Behavior decisions

### Compound snapshot policy

| Operation | Required classes | Additional approval reason |
| --- | --- | --- |
| Camera snapshot | Camera, as today | None |
| Upload snapshot as thumbnail | Camera + Edit | None; thumbnail remains L$0 |
| Upload snapshot as texture and assign thumbnail | Camera + Create + Edit | Any positive upload price always requires approval |

Evaluate the entire set once: any Deny rejects before capture, upload, or inventory changes; otherwise any Ask produces one dialog describing the complete operation; otherwise proceed. A positive texture cost still produces one dialog even if all classes are Allow. An HTTP `confirm` value or sidecar `skip_elicitation` must never bypass this gate. Existing unrelated operations keep their established class mappings.

Normalize dimensions/destination with the same snapshot-frame code used by execution, so the displayed cost and image dimensions describe the actual upload. Check the price again at dispatch; never charge above the accepted quote. If the price rises, reject as a stale approval and require a fresh request. Pass any approved price cap through trusted viewer-owned context, not a caller-controlled field. Preserve the existing free-thumbnail rule.

### Pending approvals and settings changes

- Store the full required-class set on every `Ask`, including classes currently set to Allow, along with the policy generation for diagnostics.
- On a permission settings change, increment the existing generation and notify live bridge state. Reevaluate all queued/displayed asks; immediately finish and dismiss those for which a required class is now Deny. Ignore unrelated class changes.
- Reevaluate again immediately before `showFront()` and immediately before posting an accepted request. Callback ordering must not let an old Yes click execute an invalidated request.
- Changes among Ask and Allow do not silently execute queued requests. An already displayed approval continues to cover the complete operation; it can be accepted if none of its required classes is Deny.
- Retain exactly-once response behavior and existing timeout/disable behavior. Cancelling a notification can call back synchronously: mark/remove the ask before cancellation or otherwise make that callback a no-op.
- This fixes admission of work still awaiting approval. It does not roll back already dispatched mutations. The link replacement executor separately checks a trusted cancellation/session token before each create and before its destructive callback, as specified in its plan.

### Approval contents

Resolve existing objects from viewer inventory, not from labels provided by the caller. Use names with enough folder context to distinguish duplicate names; UUIDs belong in details when helpful. Display the exact requested count, any known ineligible count, and the fact that batch outcomes may be partial. An unknown name must read “Name unavailable” with its ID; never invent a friendly name or claim a folder is fully loaded.

| Family | Required explanation |
| --- | --- |
| Rename / batch rename | Current name → normalized requested name, count, representative pairs and remaining count |
| Move / batch move / restore | Object names, source folders, destination folder; show “multiple source folders” when appropriate |
| Copy / batch copy | Source, destination, count, requested copy policy; if relevant, explain that unique no-copy items need a separate move approval |
| `confirmCopy` | Exact viewer-held plan's no-copy items, old/new folders, count; “These unique items will leave their source folders.” |
| `replaceLinks` | Existing and replacement target names, exact link count, affected folders, and that originals move to Trash only after replacements are created |
| Trash | Name/path and “Can be restored from Trash” |
| Wear / detach | Resolved item/outfit names, count, append/replace behavior; outfit lookup must match existing appearance semantics |
| Snapshot upload | Capture dimensions/viewport choice, target name, thumbnail vs texture, quoted L$ cost, assignment of the resulting image |
| Other classed writes | Specific verb and object names; concise values for description, favorite, camera pose, new item/folder and destination |

Use localized action/consequence templates, wrapping, and readable names; escape or normalize control characters in inventory names. A bounded preview (for example five rows) must explicitly say how many rows are omitted, with an expandable/scrollable detail view for the remaining prepared rows. Do not truncate counts or hide no-copy consequences. Allow/Deny buttons should identify a single request, never imply permanent permission changes.

## Shared internal contract and integration boundary

Introduce a small viewer-only `PreparedAssistantOperation` type, proposed in `fsassistantoperation.h` with implementation in `fsassistantoperation.cpp` if needed. It contains:

1. API/op and normalized request, plus the required class IDs.
2. An LLSD presentation summary: `action_key`, `subject_count`, `subjects` (resolved `{id, name, path, type, before_name, after_name, source, destination}` as applicable), top-level source/destination or replacement target, `consequences`, and optional snapshot quote/dimensions.
3. Minimal scope-validation data. Ordinary UUID operations need only the identities/meaningful values that made the approval intelligible; do not build a general mutation-plan engine.
4. An opaque, viewer-owned prepared selection for `confirmCopy` and `replaceLinks`, whose complete IDs are separate from the bounded display preview.

Keep names/summary/selection under viewer ownership. Add read-only prepare/validate methods to `LLInventoryListener`; these can access `mCopyPlans` and share the link plan's authoritative replacement preflight. The bridge uses them through the live inventory Event API instance. Prefer direct C++ methods to an additional externally callable Event API operation. Appearance and camera summaries use their existing parsers/lookup semantics; avoid a second divergent validator.

Store the prepared operation in `Ask`; renderer output belongs in `LocalAssistantConfirm` or a purpose-built detail floater using localized strings. Rebuild/revalidate scope before showing and again before acceptance. Deleted objects, invalid destinations, changed rename-before values, expired/stale copy plans, higher upload prices, or changed replacement selection invalidate approval; return a readable stale-operation error and require a fresh request/confirmation. Do not automatically retry a mutation or loop through new prompts within the remaining 70-second chain. Cosmetic changes can update labels without changing the operation; conservative rejection is acceptable where material changes cannot be distinguished reliably.

For `confirmCopy`, freeze the viewer-held plan's exact move entries and expiry; never accept client-supplied proposed moves. Existing execution revalidation remains mandatory. For `replaceLinks`, freeze the resolved target and candidate link IDs with parents/linked source. The link replacement plan owns selection validation and execution; this plan owns consumption of that selection for the prompt. Do not rescan and expand the approved set when execution starts.

Pass a separate trusted execution context containing remaining request deadline and a weak session/cancellation token to the link executor. Do not serialize a caller-writable approval token, deadline, candidate list, or price cap into the public bridge protocol. Preserve existing public request/response shapes, with optional error details only.

## Implementation stages and touchpoints

### 1. Permission correction, independently reviewable

- `fseventapibridge.cpp`: extract required-class evaluation and compound decision logic; use it in `dispatchSnapshotUpload()`, `enqueueAsk()`, `showFront()`, and `resolveAsk()` while retaining existing single-class behavior for other operations.
- `fseventapibridge.h/.cpp`: extend the permission-change hook to notify initialized state safely, including when bridge is disabled; update its comment. `llappviewer.cpp` already wires the setting signal and needs only lifecycle adjustments if required.
- `fssnapshotupload.h`: retain the standalone price gate, compose it with permission evaluation, or extract a similarly dependency-light helper for the new gate.
- `mcptools/policy.go`: add a multi-class variant that resolves the viewer and loads status once; keep `openClass()` as a single-class convenience. Update `inventory_extra.go::buildSnapshotUploadParams()` / `inventorySnapshotUpload()` to use both/all three classes.
- Preserve existing `class` in denial details for compatibility; add `required_classes`/`denied_classes` and generation when useful. Viewer remains authoritative even if sidecar policy cache is stale.

### 2. Authoritative operation preparation

- Add the minimal internal prepared-operation type and inventory read-only prepare/validate methods in `llinventorylistener.h/.cpp`. Reuse `object_summary()`, `make_path()`, batch parsing/name normalization, and plan validation rather than guessing from request fields.
- Coordinate replacement preflight extraction with item 9's agent; do not implement two inventories scans with different filters.
- Preparation is non-mutating and does not consume a copy plan. Validate immediate required arguments before displaying an approval; keep existing execution-time RLV/protection checks authoritative.
- Use cached inventory for ordinary names. If authoritative copy/replacement scope is not ready, fail with an inventory-not-ready explanation or reuse a bounded preparation fetch counted against the request deadline; never show a misleading incomplete count. Avoid unrelated inventory reads or asset fetches just to label a dialog.

### 3. Approval presentation and lifecycle

- Replace `target_label()` / generic `action_message()` usage with the summary renderer; retain a specific safe fallback for operations whose display is unavailable.
- Update `State::Ask`, `enqueueAsk()`, `showFront()`, `resolveAsk()`, `denyQueuedAsks()`, and settings-change invalidation together.
- Update `skins/default/xui/en/notifications.xml` and localized string resources for text/buttons. If a details floater is required for bounded batches or replacement lists, add its XUI/controller and CMake registrations as a narrowly scoped approval detail view.
- Integrate accepted prepared selection/trusted execution context with the replacement executor. Existing `confirm` injection occurs only after bridge policy/approval validation.

### 4. Verification and documentation

Update permission/setup documentation in `tools/fs-mcp/README.md` for snapshot requirements. Keep user-facing vocabulary aligned with the separate assistant-preferences plan. Do not change defaults in `settings.xml`.

## Meaningful tests and acceptance criteria

- Table-drive all 9 Camera×Edit combinations for thumbnail and all 27 Camera×Create×Edit combinations for texture, including zero/positive cost. Deny always wins; otherwise exactly one approval when any Ask or positive price; otherwise proceed. Exercise the actual bridge dispatch path with a fake event sink so no rejected case captures/posts an event.
- Regression: queue/show an Ask, change its required permission to Deny, click the old Yes callback. Zero events posted, one forbidden response, dialog removed. Repeat for snapshot's second/third required classes, queued asks behind another dialog, settings Cancel restoring a previous value, and bridge disable. Unrelated changes and Ask→Allow retain a valid approval without auto execution.
- Race/lifecycle: settings invalidation and notification cancellation callbacks in either order; duplicate Yes callbacks; timeout then Yes; disable then Yes. Exactly one terminal response and no resurrected request.
- Summary fixtures: single rename, batch with duplicate names/multiple sources, folder copy policy, authoritative no-copy plan, replacement from A to B across folders, append versus replace wear, texture cost, long Unicode names and embedded newlines. Verify names, exact counts, normalized values, warnings, and honest omitted-row counts.
- Stale-scope cases: object removed/reparented, rename-before value changed, no-copy plan expired/changed, replacement link added/removed/reparented/retargeted, target resolution changed, price increased. No operation executes against unapproved scope; caller receives a clear error requiring fresh confirmation. Lower/equal price does not exceed the accepted cap.
- Sidecar: extend `internal/mcptools/snapshot_upload_test.go` to deny independently on Camera/Edit/Create, load policy once, and still call current viewers once for Ask. Preserve older viewers without a permissions object and existing snapshot result handling. Run `go test ./...` in `tools/fs-mcp`.
- Add focused C++ policy/state/summary checks to the repository's supported test harness; the existing standalone `fssnapshotupload_test.cpp` is useful but does not test bridge lifecycle. A fake inventory/notification/event sink is necessary for the regression above, not tests that merely duplicate the implementation table.
- Manual viewer validation: Ask policies for representative families, readable long names at multiple UI scales, scroll/details access, click after permissions change, partial-batch explanation, and no-copy source/destination clarity. No clipping like the screenshot, no raw UUID-only prompt when inventory name is known, and no duplicate bridge/sidecar approval dialogs.

## Compatibility and failure handling

The API version and permission storage stay unchanged. Permanent delete remains denied before any setting lookup. Older sidecars remain safe because the viewer enforces compound permissions; the updated sidecar retains its old-viewer fallback. Unavailable inventory names degrade honestly for simple fixed UUID operations; authoritative special selections fail closed until ready. A rejected or stale request does not mutate inventory or consume an accepted no-copy move plan. Existing batch partial-success behavior is described rather than converted into transactions. Required permission revocation does not grant authority through saved approval metadata or forged request fields. Implementation should document cancellation after dispatch separately; rollback of completed work is outside these two items.

The setup/diagnostics feature may add a minimal authenticated bridge-health request that works when inventory Read is Never. Keep that request separate from inventory operation classification: it returns bridge/API readiness only, with no inventory records or credentials, while retaining the same token, loopback, and Origin protections. Local preference status uses a local bridge-state snapshot; recent activity distinguishes setup diagnostics from external assistant requests. The assistant-preferences plan owns that endpoint and UI; this plan must not accidentally gate it on Read or introduce a public operation-summary endpoint in its place.
