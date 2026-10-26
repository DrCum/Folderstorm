# Folderstorm MCP script development and testing

Status: diagnostics and Contents reads implemented; later groups follow in separate stacked PRs. Native Windows/simulator acceptance remains pending.

Enable an assistant to complete the native loop of creating or editing LSL, installing it in an object, compiling and running it, exercising the object, reading output, and packaging the result into another object's Contents. The same tools must work with the regular viewer and an explicitly selected managed character.

Source baseline: PR #88, commit `85f620154fe0da09d2a64d7fcc0f9f352f358c49`. Start implementation from current main if it contains the relevant MCP stack, otherwise above the latest applicable published branch. Preserve published history and existing local build patches. Do not pull unrelated theme-editor work into this feature.

## What already exists

The current implementation supports creating inventory scripts; reading, editing, saving and compiling eligible agent-inventory scripts; observing Nearby Chat; and rezzing copyable inventory objects. It does not expose task inventory, task-script editing or control, taking world objects, or the Script Debug feed. Existing notification review opens native UI; it does not answer script dialogs. Existing Nearby sends use channel zero.

The existing rez tools explicitly return `object_id_confirmed=false`. Receiving a successful tool response therefore cannot identify the object to edit next. Reliable lifecycle tracking is part of this work.

Reuse the existing content upload service, native task inventory and script protocols, source identity checks, permission classes, approvals and durable async guards. Keep native UI operations available while assistant work is pending or revoked.

## Intended workflow

1. Select one exact viewer login and a test area; identify the target prim and any source inventory items.
2. Read the prim's Contents and the script's complete source and revision.
3. Create a new inventory script if necessary; save and compile it using the existing inventory tools.
4. Copy that script into the exact prim, initially stopped unless starting it was explicitly requested. Return the confirmed task item identity.
5. Edit and compile the installed script directly on later iterations, preserving its item identity and unrelated contents.
6. Start or reset it deliberately, touch the object or answer its live game dialog, and wait for output from that object's run.
7. Read source-bound runtime diagnostics and expected test messages; revise only after the previous operation's outcome is known.
8. Take a copy, or deliberately take the original, into an exact inventory folder. Confirm the resulting inventory item.
9. Insert that inventory object into a different exact prim's Contents and verify it arrived. This inserts an inventory object; it does not rez it, link it to the container, or attach it.

The agent can orchestrate these primitives independently. A small test-session layer adds repeatability and reporting after the native operations are proven.

## Shared contracts

### Identity and targeting

Every tool requires an explicit `viewer_session`; PID is an optional additional constraint. Bind prepared operations, reads, cursors and receipts to account, grid, login generation, connection authority and, for world work, region generation and simulator host.

Target world objects by UUID and validate the current region/local ID at submission. Use the exact prim UUID for Contents: scripts can reside in linked child prims. Return root UUID, target prim UUID, link number and name for clarity, but never silently substitute the root or another prim with the same name. Local IDs, link numbers and names alone are insufficient identities.

The initial writable scope is loaded, owned, modifiable, unattached volume prims in the current region. Support linked child prims within that scope. Take and take-copy operate on the complete root linkset. Existing build-location, native ownership, permissions, lock, parcel and RLVa checks still apply. Report the exact unsupported condition rather than silently loosening scope.

World mutations require the active managed character, matching current Build behavior. Reads may run for permitted inactive characters. Do not switch foreground ownership as an implicit side effect. Report `activation_required` so orchestration can explicitly use the existing session activation tool.

### Revisions and conflicts

Task inventory reads return a complete snapshot identity, task inventory serial/generation and bounded item records. Incomplete or stale cached contents are not an empty inventory. Source reads add actual asset identity, complete/truncated flags and a content hash.

Mutations require fresh expected target and item evidence. Recheck after approval, after fetch/queue waits and immediately before transport. Native or external script editors with unsaved changes, pending saves, preprocessing or file watchers produce a conflict. Do not open or drive those editors to perform assistant operations.

Native asset and task protocols do not provide a universal atomic compare-and-swap. Describe local preflight protection accurately and expose post-submit conflicts or uncertainty. Do not automatically overwrite a concurrent edit or restore an older source.

### Completion and cancellation

Use one shared operation registry, or a compatible extension of existing registries, for durable receipts. Distinguish queued, submitted, confirmed, failed, cancelled-before-submit and submitted-with-unknown-outcome. Report source save, compilation, running state, source removal and destination arrival as separate facts when applicable.

Every mutation accepts a unique `client_request_id`. A repeated request with the same identity and material arguments returns its existing receipt without resubmitting. Reuse with different arguments is rejected. Reserve capacity and record the operation before sending. Never claim server-wide exactly-once delivery or blindly replay a timeout.

Carry `AsyncTask` ownership through inventory fetches, upload handshakes, queued transport, native callbacks and deferred cleanup. Check actual authority at every server-write boundary. Do not hold thread-local `AsyncScope` across a suspension. Revocation stops unsent stages; it cannot recall accepted server work.

Receipt eviction happens only when admitting new operations. Keep submitted/cancelled receipts while native callbacks remain outstanding. If an outstanding callback never settles, retain its slot for the current login and refuse new work at capacity; timeout alone is not permission to discard the only receipt. Clear private data on source/login or permission changes without allowing late callbacks to mutate a new login. Preserve permitted, content-free completion facts for cancellation accounting where the existing receipt policy allows them.

Status reads and diagnostic polling use source-bound read paths. They must not consume mutation preparation slots or make Stop unavailable. Bound polling and waits without blocking the viewer main thread.

### Permission controls

Add narrowly named, native-enforced classes and expose them in Assistant preferences with existing Apply/Cancel and persistence:

| Permission | Default | Purpose |
| --- | --- | --- |
| `task_inventory_read` | Deny | Inspect an eligible object's Contents |
| `task_inventory_write` | Ask | Insert, replace, rename or explicitly remove eligible contents |
| `script_run` | Ask | Start, stop or reset task scripts; compile requests that resume execution |
| `script_debug_read` | Deny | Read script errors and owner output for permitted objects |
| `object_take` | Ask | Take or take-copy a specified owned linkset into inventory |
| `object_interact` | Ask | Touch a permitted object or answer its live script dialog/text box |

Task-source reads also require existing inventory `read` and native COPY/MODIFY source eligibility. Source writes additionally require existing `content_edit`; new inventory scripts retain `create`. Rezzing retains `build_rez`. Taking an object is not covered implicitly by permission to rez it. Explicit removal of task contents uses the existing irreversible-action approval policy; if that policy cannot express native irreversible deletion clearly, omit removal until it can rather than classifying it as recoverable Trash.

Reuse `chat_read` for general chat access and existing Nearby send permission for channel-zero speech. A bounded, explicitly approved nonzero script-channel send also requires `object_interact`; it must not become a way around chat restrictions or limits. Initial deployments refuse no-copy transfers; a separately tested extension may require existing `nocopy` and native movement rules. Read-only presets must not opt into any new private stream or mutation.

Approvals freeze source, object/prim, item, replacement/removal effects, script running behavior and exact material inputs. Existing Allow permits repeated authorized work; no mandatory prompt for every reset when the user has selected Allow. Do not create an all-powerful development-mode bypass.

Add additive, versioned capabilities such as `task_inventory`, `task_scripts`, `script_diagnostics`, `object_lifecycle`, `object_interaction` and `test_sessions`. Preserve existing capability meanings and reject mismatched binaries clearly. No host/viewer IPC version change is expected unless implementation genuinely adds host IPC fields; these are viewer MCP services.

## PR A Diagnostics and task inventory reads

Deliver useful observation first, plus the read and operation infrastructure required by later mutations.

Proposed tools:

- `object_contents_list`: fetch authoritative Contents for one exact prim; return paginated, consistent snapshots, inventory serial, item types, permissions, asset IDs where native-visible, names and source eligibility.
- `object_script_get`: read complete eligible task-script source using native item/task-bound asset access and return a versioned edit snapshot. No arbitrary asset UUID reads.
- `script_debug_poll`: bounded cursor feed, filterable by permitted object/root and test session, with `start=now` for a clean test baseline.
- `object_operation_status`: inspect a source-bound receipt without executing a mutation or allocating a mutation ticket.

Register RAII task-inventory listeners and wait for fresh simulator contents. Track invalidation and serial changes. Distinguish unavailable, pending, empty and complete. An assistant cancellation unregisters its own interest; it must not cancel another native editor's shared fetch or discard a result needed by native observers.

Capture diagnostics from the native message pipeline after applicable sender/ownership, mute, privacy and RLVa filtering, before presentation-only routing sends a line to Chat or Script Debug. Do not scrape text widgets or require the debug window to exist. Recheck eligibility at delivery as well as capture. Explicit assistant debug consent controls capture independently of whether native errors are displayed in Chat or a separate window; leave the user's display preferences untouched. Audit any pre-routing redaction, secret-suppression and grid-specific handling before choosing the hook, and never republish hidden original text.

Capture runtime errors, warnings when explicitly identifiable, and owned-object `llOwnerSay` output regardless of its presentation route. General Nearby output can reuse the existing chat stream. Include timestamp, event sequence, object UUID/name, root when known, owner when verified, raw native message type and bounded text. Populate script UUID, script name and line number only when the native event actually provides them; otherwise return unknown. A runtime message often identifies an object, not a particular script.

Use a dedicated bounded diagnostic ring, initially 2,048 events and 2 MiB per login, 8 KiB per event, maximum 100 events/64 KiB per response. Apply rate/byte limits and expose dropped counts and explicit gaps. Overflow must never silently turn a test into a pass. Clear readers and retained private payloads on logout, identity change, access revocation or applicable restriction changes. Subscription starts no earlier than the relevant consent epoch.

Acceptance: a native debug window may be closed, and owner output may be routed to either window, while an explicitly authorized agent can still see newly received eligible diagnostics. Unrelated residents' messages or old-login output remain inaccessible. Delayed task reads survive native observer joins and assistant-only revocation correctly.

## PR B Task script editing and object Contents writes

Proposed tools:

- `object_contents_put`: insert an exact eligible agent-inventory item into a target prim; explicit copy mode, source/destination snapshots and running choice for scripts.
- `object_script_preview`: bounded source diff against a complete task-script read.
- `object_script_save_compile`: update an existing task script through `UpdateScriptTask`, explicit raw Mono compiler, source snapshot and running choice.
- `object_script_status`: query native running/Mono state without allocating a mutation ticket.
- `object_script_set_running` and `object_script_reset`: exact item-targeted runtime controls.
- `object_contents_rename` and `object_contents_remove`: explicit item operations, with exact revisions and native policy; removal is conditional on the irreversible-action contract above.

Extract non-UI adapters from native task inventory/script operations instead of invoking global drag/drop or selection-driven handlers. `dropScript`/`dropInventory` contain global drag-state and local inventory/UI side effects and are not safe MCP entry points unchanged.

For insertion, support native-compatible script, object, notecard, sound and texture items where the transfer is natively valid. Keep the source inventory item and its permission propagation. Reject folders, unresolved links, unsupported types and no-copy transfers initially. A type whitelist can expand after native tests; metadata inspection still reports other item types.

Do not identify the inserted task item by name alone or equate its inventory UUID with the source inventory UUID without confirmation. Observe a fresh authoritative Contents response, validate the item/asset and return its task identity. If concurrent changes leave correlation ambiguous, report candidates and uncertainty and stop dependent writes. Local optimistic inventory updates do not constitute simulator confirmation.

Insertion defaults to stopped for scripts; an explicit start request requires `script_run`. Existing-script compile accepts stopped or preserve-running behavior; preserve requires a fresh native running query and the corresponding authority when resuming. Document that compilation may restart execution and reset state even if the requested final running flag is unchanged. Preserve an existing Experience association; do not assign a new Experience silently. Initial source handling is raw LSL, with no hidden includes, filesystem preprocessing or editor preference changes.

Extend existing guarded upload machinery to task uploads, including the capability handshake, source upload, callback and inventory refresh. Preserve native OpenSim payload typing. Return `source_saved`, `compiled`, bounded compiler errors, asset/task-item identities, requested running behavior and separately observed running state. Compilation failure may still save the source. Cache invalidation and clean-editor refresh must leave unsaved native edits untouched.

Extract a native script-running reply dispatcher that serves independent native and assistant observers. Correlate by login, simulator, task and item; serialize/coalesce same-target reads when the wire reply lacks a request nonce. A stopped assistant query must not strand a native script editor. Reset has no universal completion acknowledgment: report transport submission, then use subsequent status/output evidence rather than claiming that execution reached `state_entry`.

Acceptance: create an inventory script, install it stopped, read it back, compile it in place, start/reset it and see its output. Repeated edits retain task item identity and unrelated contents. Test malformed LSL, dirty editors, revoked permissions at every upload stage, late/duplicate callbacks, child prims, lost replies and native UI observers.

## PR C Take copy rez and nested object packaging

Proposed tools:

- `object_take_copy`: save a natively copyable owned root linkset to an exact writable inventory folder while leaving it in world.
- `object_take`: take the specified owned linkset into that folder, deliberately removing it from world.
- Extend existing rez tools with operation receipts and confirmed/candidate object identities when correlation is supported. Keep old clients' result semantics intact.
- Reuse `object_contents_put` to place the resulting inventory object into another object's Contents.

Factor native derez eligibility and packet construction into an exact-target service. Do not take over the user's Build selection, change their camera, or call selection-driven Take handlers. Keep native permission, sit/attachment, lock, parcel, linkset and RLVa rules. Initial scope excludes attachments, someone else's objects, forced take, return and deletion.

Reserve the operation and observe destination inventory changes before submitting. Retain the native transaction UUID and connect it to actual inventory update evidence where the simulator provides it. For Take, track both destination arrival and removal of the original root. Object disappearance alone can be culling or crossing, not successful acquisition. For Take Copy, keep source existence separate from destination confirmation.

Investigate native take/rez correlation with captured SL and OpenSim message traces before promising exact IDs. Prefer actual transaction or item/object association fields. Never call a name/position match a confirmed identity. Use a private operation folder for take results where useful, snapshot before submission and serialize overlapping operations; these reduce ambiguity but do not create a server acknowledgment. For rez, ambiguous new-object candidates require explicit selection/reconciliation or an optional user-approved fixture acknowledgment before dependent writes. Do not automatically inject a helper script, rename existing content or retry the rez to force identification.

An explicitly installed fixture may report its object UUID in a run-tagged owner message; verify the native source UUID, owner, location and run membership before recording that evidence. A cooperative fixture is optional and not a prerequisite for ordinary take or rez tools.

Reconcile confirmed inventory item identity before allowing the agent to insert it into a second prim. Native Contents verification confirms the inventory object is present; its inner contents are not independently inspectable while it remains an un-rezzed inventory object. To verify inner scripts, explicitly rez a copy in the test area and inspect that instance.

Acceptance: save a scripted linkset as a copy, find the confirmed inventory item, place it in a container and verify Contents. Repeat with deliberate Take, and verify world removal separately. Test concurrent inventory arrivals, same-name items, multiple rezzes, grid variations, timeout/cancel, native permission propagation and no duplicate submission after lost acknowledgment.

## PR D Interactions and repeatable test sessions

Proposed tools:

- `object_touch`: native touch on one exact prim, with a validated face/local point where supported.
- `object_dialog_list` and `object_dialog_reply`: inspect and answer live dialogs/text boxes from authorized test objects.
- `object_channel_send`: explicit bounded avatar-origin chat to an approved nonzero script channel.
- `script_test_begin`, `script_test_status` and `script_test_stop`: source-bound run tracking, scoped diagnostics, conditions and cancellation.

Touch uses native grab/degrab semantics, native distance and RLVa eligibility. Validate synthetic pick data and face indices. No physical dragging, fabricated simulator events, avatar movement or sit/teleport support is required here. UDP submission does not prove the script handled the touch.

Dialog replies bind to an actual unexpired native notification, source object/owner, channel, available buttons and exact button identity. Text-box replies bind to the live text box and native byte limits. Revalidate at dispatch and use native response delivery. Revocation or an expired/replaced dialog blocks it. Do not generalize this endpoint to permissions, debit requests, teleports, inventory offers, purchases or other notification types.

Nonzero channel sends originate from the avatar. They do not impersonate object scripts, inject link messages or guarantee one receiving object. Freeze the channel/text and show that it is a channel broadcast. Respect native script-channel limits, reserved channels, RLVa/chat restrictions and bounded rate limits; do not loosen existing public-chat limits. This does not give the viewer a receive feed for arbitrary hidden channels. Use normal owner/chat output or an explicitly installed test fixture for observations.

A test run records permitted prims/items, source hashes, baseline cursors, a bounded timeout, expected output/state conditions and every returned operation receipt. A newly confirmed rez result may join only under an already approved fixture plan with validated provenance. Membership never grants permission to another account, region, unrelated object or new action class.

Implement orchestration as an allowlisted sequence using the same guarded tool services; no arbitrary code evaluation endpoint or second route around approval. Start with presence/absence of exact bounded text, exact object identity, native running state, Contents membership and timeout/error conditions. Source/line filters apply only where actual native evidence exists. Cap sequence length and waits; initially 64 steps, a 10-minute run deadline and one active run per viewer login. Report pass, fail, stopped or inconclusive, with gaps/unknown operations making dependent assertions inconclusive.

Stop cancels unsent steps immediately and keeps accounting for submitted work. Never auto-reset scripts or delete fixtures simply because a test stopped. Stopping a script is a separate authorized action. Cleanup is an explicit reviewed list of exact run-created objects/items; preserve anything with uncertain provenance and avoid permanent deletion as automatic cleanup. Supply an example game-development workflow without requiring a custom LSL test framework.

An optional small LSL fixture convention can emit run/step/result markers through `llOwnerSay`. It is routing and test evidence, not a secret authentication token or proof of correctness. The user can adopt it in their own scripts, and the agent can propose it through ordinary source editing. Existing scripts need no modification to receive basic runtime diagnostics.

Acceptance: the agent installs a small game controller and child script, starts a run, touches a prim, chooses a real dialog option, checks expected owner output, handles a deliberate runtime error, revises source, reruns and packages the object into another object's Contents. The user should no longer need to copy Script Debug output or move scripts between those test objects manually.

## Validation and delivery

For each PR, add focused production-service/controller tests as well as portable models. Register include directories and keep assertions/strict warnings enabled. Cover the actual adapters and callback ordering, not only schema or source-text checks. Run affected MCP Go schema, targeting, permission and dispatch tests, portable native tests, XML checks, relevant source contracts and diff checks. Avoid broad unrelated suites and packaging.

Required regression cases:

- Native observer joins an assistant task-inventory or running-state read; revoke the assistant, then deliver the reply. Native UI still finishes; assistant private delivery does not.
- Approval/fetch/queue/upload callback followed by source switch, logout, teleport, permission loss or object deletion cannot write to a different target or login.
- Status polling exceeds existing preparation capacity, then Stop still succeeds.
- Script compile fails after source save; receipt preserves both facts and does not start it accidentally.
- A cancelled native operation has a late confirmation at receipt capacity; the original receipt remains accessible and no replacement operation is sent.
- Duplicate insertion/take/rez requests return the prior outcome; uncertain or ambiguous acquisition does not trigger automatic replay or dependent writes.
- Child-prim contents remain separate from the root; multiple same-name scripts, roots and inventory objects are never resolved by names alone.
- Closing or hiding Script Debug, changing owner-say routing, overflowing diagnostics and changing receive restrictions preserve correct capture/delivery policy.
- Dialog expires between list and reply; another notification with the same title cannot be answered instead.
- Stop during a wait/run prevents the next queued action and preserves already submitted outcomes.

Perform an incremental native Windows build and live acceptance with matching viewer/MCP binaries after each native feature group. Use disposable owned fixtures on a permitted test parcel. Validate actual capabilities on both SL and an available OpenSim grid; report any untested grid explicitly. Check regular and managed viewers, multiple characters, active-character restrictions, native editor coexistence and user permission changes.

The first live gate is PR A: task inventory freshness and diagnostics with native windows both open and closed. Next gate is PR B: the complete script edit/install/compile/reset loop. Do not represent take/rez identity correlation as solved before PR C's server traces and round trip. PR D completes the automated gameplay loop after those operations are reliable.

A cloud agent can implement the services and focused tests. A local Windows agent with desktop access can build, inspect native state and run the fixture acceptance. An environment without that access should deliver concrete code/test results plus a short native checklist instead of claiming live testing. Do not publish installers/releases or merge PRs without the user's release confirmation.

## Source map

- `tools/fs-mcp/internal/mcptools/content.go`: current agent-inventory source tools and explicit task/execution exclusion.
- `tools/fs-mcp/internal/mcptools/building.go` and `indra/newview/fsbuildlistener.cpp`: exact-source build/rez tools and unconfirmed rez identities.
- `tools/fs-mcp/internal/mcptools/social.go` and `indra/newview/fsassistantsocial.cpp`: existing chat, events, native notification review and channel-zero sends.
- `indra/newview/fscontentlistener.cpp`: complete reads, upload hooks, editor conflicts and source-save/compiler receipts to extend carefully.
- `indra/newview/llviewerobject.cpp`, `.h`, and `llvoinventorylistener.cpp`, `.h`: task inventory listeners, serials, script insertion, updates, removal and move-to-agent machinery.
- `indra/newview/lltooldraganddrop.cpp`: native insertion behavior and global drag-state assumptions to avoid in MCP services.
- `indra/newview/llpreviewscript.cpp`: native task source access, `UpdateScriptTask`, running-state queries/replies, reset and editor state.
- `indra/newview/llviewerassetupload.cpp`, `.h`: task upload completion signature, running flag, Experience association and OpenSim payload compatibility.
- `indra/newview/llfloaterimnearbychathandler.cpp`, `llviewermessage.cpp`, and `llfloaterscriptdebug.cpp`: native debug/owner-say routing, filtering, touch and script dialog behavior.
- `indra/newview/llviewermenu.cpp`: native take/derez eligibility, transaction packing and selection-driven entry points to extract.
- `indra/newview/llinventorymodel.cpp`: native inventory-update transaction evidence for take correlation.
- `indra/newview/fsassistantasync.h`, `fsassistantfeatures.h`, `fsassistantpermissions.h`, and `fseventapibridge.cpp`: durable authority, additive capability registration, permission definitions, approvals and receipt ownership.
- `doc/plans/mcp-content-editing.md` and `mcp-building.md`: existing boundaries and native acceptance requirements to update when the new features land.

## Diagnostics checkpoint

Implemented exact-prim simulator-backed Contents snapshots, item-bound task script reads and a dedicated bounded owner/debug feed. Native cache callbacks distinguish simulator responses from optimistic changes. New private read permissions default to Never; no host IPC change.

Windows acceptance: allow Contents, World read and inventory Read; inspect root and child Contents; read an owned modifiable script. Allow script diagnostics, poll from now, generate owner output and a runtime error with Script Debug closed, then repeat with owner output routed to Script Debug. Change receive restrictions and revoke access while a Contents fetch is pending; native editors must still complete. Use matching viewer/MCP binaries. Native Windows and simulator acceptance is pending.
