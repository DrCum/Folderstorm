# MCP for hosted character sessions

Status: implementation in progress; exact-session routing checkpoint implemented. Managed enablement and host UI follow in dependent checkpoints.
Reviewed 2026-10-15 against PR #41 head `41a694f9be`.

## Recommended design

Use one existing `fs-mcp` connection to address each character's existing authenticated viewer bridge directly. Keep permissions, inventory operations and approvals in their owning viewer process. The session host supplies account-bound lifecycle/settings status and access to pending approvals.

Every managed account-data or action request must identify an exact login using an opaque `viewer_session` reference. Selecting a character for MCP does not change the active world. Existing inventory, outfit, camera and workspace tools gain character targeting through shared resolution code.

The existing per-profile Local Assistant enable setting remains the source of truth, with its existing Off default. Private profiles retain independent permissions and session-only settings. Host preferences expose the same setting, rather than creating another permission database or requiring two independent enable switches.

## What already exists

| Area | Current implementation | Follow-up needed |
| --- | --- | --- |
| Viewer bridge | `indra/newview/fseventapibridge.cpp` binds `127.0.0.1` on an ephemeral port, authenticates bearer tokens, rejects Origin headers and writes atomic per-PID discovery files. | Replace the managed-session refusal only after explicit session binding and lifecycle handling are available. Port allocation already supports separate processes. |
| Sidecar discovery | `tools/fs-mcp/internal/discover/discover.go` finds per-PID files and supports explicit files, directories and path lists. | Discover hosted profiles through a stable, bounded registry, without scanning arbitrary private profile directories. |
| Target resolution | `tools/fs-mcp/internal/mcptools/server.go` supports `viewer_list`, `viewer_select` and `viewer_pid`. | Selections are PID-based; a missing selected viewer can fall back to the sole remaining viewer. Managed targeting must reject that fallback. |
| Policy/confirmation state | `internal/mcptools/policy.go` caches by PID; `confirm.go` stores PID-bound plans and accepts a PID override. | Bind caches, plans and continuations to the exact login. A managed confirmation cannot change its source account. |
| Permissions and approvals | `fseventapibridge.cpp`, `fsassistantoperation.*`, `fsassistantapproval.*` already implement native permission classes, prepared-operation validation and revocable execution guards. | Add the managed session to every request and asynchronous continuation; integrate hidden-character approval presentation. |
| Preferences | `fspanelpreferencelocalassistant.cpp` and `fsassistantsettings.h` preserve runtime/saved layers, Apply/Cancel and external-change handling. | Reuse these rules for host edits. Permission changes remain worker-owned. |
| Host lifecycle | `fssessionworker.*`, `fssessionprotocol.h` and `tools/session-host/main.cpp` already guard worker/account/grid/generation and serialize activation. | Carry only bounded assistant status/settings/review metadata over this channel. |

## 1. Exact session identity and request routing

- Each viewer login receives a new opaque `viewer_session`, combined internally with its process/launch identity, worker ID, account UUID, grid and login generation. The public reference is not the simulator login-session UUID and is not a bearer credential.
- Add a versioned managed-session capability to authenticated health/discovery metadata. Include actual username, display name, friendly profile label, grid, managed/standalone state, readiness and available-operation state. Friendly labels, card positions, names and PID alone are never routing keys.
- Extend existing `viewer_list`/`viewer_select` tools to return and validate these identities. Add `viewer_session` to every existing account-data/action argument schema, including preview, execute, copy confirmation, legacy continuations and history/undo paths.
- Managed account calls require that explicit session reference, even when only one character remains. `viewer_pid` remains compatible for ordinary viewers and may accompany a managed reference only when both resolve to the same session.
- Resolve the endpoint once per request and carry that binding through policy checks, operations, callbacks and responses. Recheck it inside the viewer immediately before execution and after asynchronous waits. Discovery/health inspection is insufficient by itself because the login can change between inspection and a write.
- Preserve ordinary single-viewer compatibility. Once a target has been selected or a request/plan bound to it, its disappearance produces `target_unavailable`/`session_changed`; it cannot silently select the remaining viewer. A never-selected ordinary single-viewer session can retain initial auto-selection.
- Keep an unavailable selection invalid until explicit selection or an explicit new target argument replaces it. Concurrent requests carry independent targets and do not mutate a shared selection as a routing side effect.
- Key policy caches, copy plans, request continuations and operation references by session identity plus their existing generations. Managed endpoints require the new capability; legacy elicitation or a caller-supplied PID cannot redirect a managed plan. Include the resolved character identity in results and useful errors.

## 2. Stable discovery and enabling managed profiles

- Managed workers publish their own `fs-mcp-<pid>.json` records in a protected, installation-independent directory beneath `%LOCALAPPDATA%\FolderstormSessions\Prototype-v1\mcp-discovery`.
- Each worker owns its discovery file and bearer token. The host never forwards bearer tokens through its control pipe or stores them in host preferences, session sets, logs, exported workspaces or client configuration.
- Reuse the existing ephemeral loopback listener and native Event API implementation. Inventory payloads and tool requests travel directly between `fs-mcp` and the selected viewer; the 4 KiB host control protocol carries only bounded lifecycle/settings/review metadata.
- Extend default discovery with this known directory. Apply bounded file/count/field validation, current-user protection, canonical path checks, authenticated identity verification and stale/PID-reuse rejection. Avoid recursive scans of account data.
- Respect explicit `FIRESTORM_MCP_DISCOVERY` scopes. Update copied configurations to include the appropriate stable hosted-discovery directory; configuration that intentionally restricts discovery is not silently broadened. A previous explicitly scoped configuration may need to be copied once again.
- Preserve token redaction and loopback/Origin checks. Publish only after enablement and compatible session initialization. Disable, logout and ownership loss invalidate pending requests and credentials and remove the advertised record. Re-enabling gets a fresh binding/token.
- Keep the existing per-profile enable preference and class permissions. Profile copying continues to exclude assistant access/permission state. Ordinary saved choices and native session-only overrides retain their established behavior.
- On detach, retire the managed binding and invalidate its work. A still-enabled standalone viewer can publish a fresh standalone binding in the same known registry until it exits, so it remains discoverable after the host closes. Require fresh selection; do not continue an old managed request under that new identity.

## 3. Permissions, background operations and approvals

Continue using the owning viewer's permission classes, restrictions and native validation. A permission granted to Sodie grants nothing to Maddy. Permanent-delete operations retain their existing hard refusal.

| Operation | Initial managed behavior |
| --- | --- |
| Inventory reads/searches and outfit/worn-item reads | Available on a ready Warm/Economy character when its policy permits them. |
| Inventory edits/copy/move/Trash and appearance changes | Available where the existing native operation is background-safe, with the same permissions, restrictions, server confirmation and approval rules. Audit each path before enabling it. |
| Workspace/layout list, status and fitted preview | Available without activating the character; return account-local data only. |
| Camera state read | Available when ready and native restrictions permit it; report stored camera state without implying a freshly rendered frame. |
| Camera changes, snapshots and workspace/layout application | Initially require the explicitly targeted character to be active, ready and outside a conflicting transition/modal/Preferences transaction. Return a clear `requires_active_character` or busy result before side effects. |

No tool silently promotes a character, enables background rendering, upgrades Economy to Warm, grants keyboard/microphone ownership or moves an avatar to satisfy an operation. Later background-render support can be a separately reviewed enhancement.

For Ask permissions:

- Hold a bounded pending request in the owning viewer while it is inactive. Do not open/focus its hidden approval floater.
- Show a dedicated **Local Assistant requests** entry/count in host Attention, independently of Viewer Notifications and chat-category visibility. Show the character/account and action summary with bounded, restriction-appropriate text.
- **Review** explicitly activates that character through the existing serialized handoff, then opens its native approval dialog. Review is not approval.
- Show actual account/grid identity in the approval UI. Keep the owning viewer's native Allow/Deny controls as the decision point.
- Preserve overall request deadlines. Review, switching or a hidden window does not extend a request indefinitely. Timeout, denial, disabling access, disconnect, account change and controller loss invalidate the request and its continuations.
- Extend existing execution guards to all exposed operations, including reads and operations that currently bypass prepared long-chain execution. Cancel invalidated work before the next side effect; report already-submitted/uncertain outcomes honestly without replay.

## 4. Host setup and everyday controls

Add **Character settings → Local Assistant** using the existing selected-character control:

- **Enable Local Assistant**, synchronized with that private profile's existing native setting.
- Effective status, actual account/grid and a concise permission summary.
- **Permissions…**, opening the owning viewer's existing editor; **Check connection**; **Copy MCP configuration**; pending-request **Review** when present.
- Optional **Pause all MCP access** as a reversible runtime host action, preserving each profile's enable choice and permissions. Pausing revokes advertised access and pending work; resuming uses fresh bindings.

Keep setup text short and use tooltips for targeting/background rules. Applied host edits use existing autosave. Cancel discards unpublished drafts. Worker-owned settings actions reuse the native saved/session-only layer and external-change rules; the host does not edit private settings files or overwrite a native Preferences draft. Validate only account drafts actually changed, so an unrelated disconnected profile cannot block global Preferences changes. Report partial per-character failure without pretending a cross-process Apply was atomic.

Diagnostics show enabled/ready/paused state, identity mismatch, unsupported versions, discovery scope, operation eligibility and pending approval status. They do not show bearer tokens, simulator session UUIDs, conversation content or inventory contents.

## Reviewable implementation checkpoints

1. **Session-aware discovery and targeting.** Add identity/capability contracts, the stable registry and shared sidecar target resolution. Update every tool schema and session-bind caches/continuations. Retain the managed enable refusal until the following checkpoint. Verify existing ordinary MCP clients remain usable and stale targets never fall back.
2. **Managed execution and native approvals.** Enable private profiles through their existing opt-in setting, apply the background-operation matrix and session guards, and integrate bounded approval Attention/Review. Add assistant metadata/settings/review commands to the private host protocol together. Bump its current version once for this wire change; choose the next version at implementation time and require matching host/viewer binaries.
3. **Host setup, diagnostics and acceptance.** Add the integrated controls, configuration generation, connection checks and reversible pause behavior. Complete migration/error handling and the native test matrix, then revise any defects in their owning checkpoint.

Publish separate stacked PRs at useful build/testing checkpoints without merging or rewriting earlier published history. At implementation start, verify current main and the merge state of #41; branch above its latest fixes if still unmerged, otherwise use updated main. Coordinate changes with any newer Local Assistant/permission work before modifying shared files.

## Focused verification

Source inspection alone establishes this plan; no tests or builds are needed for the plan-only change.

During implementation, run focused tests of the changed sidecar discovery, routing, HTTP client, policy and confirmation code. These Go tests are relevant because this feature changes those packages; avoid unrelated Go tests, viewer/packaging builds and GitHub builds. Use portable C++ checks for session bindings, lifecycle eligibility, bounded assistant metadata and settings transactions.

Required cases:

- Two/five hosted accounts, duplicate friendly labels, reordered slots, different grids and a simultaneous ordinary viewer.
- Exact routing of every tool family; discovery metadata never exposes credentials. Empty, malformed, stale, oversized or out-of-scope records give a useful refusal.
- Selected Sodie disconnects while Maddy is the only endpoint left: old references and implicit calls must fail. Repeat with restart/PID reuse, same-process relogin and detach.
- Change active character or selected MCP character during a long request; it retains its original account. Concurrent explicit targets remain independent.
- Policy caches and confirmation/preview/execute/undo state do not survive a session change or redirect through a PID override.
- Different Allow/Ask/Never policies, permission revocation while awaiting approval, native restrictions, Read disabled, timed-out approvals and submitted operations with lost replies.
- Inventory work in Warm and Economy without changing draw/input/voice ownership. Active-only tools refuse before side effects; handoffs and bird's-eye transitions retain their camera state.
- Approval requests remain visible with Viewer Notifications hidden. Background requests never steal focus; Review activates the exact owner and native confirmation identifies it.
- Native and host Preferences Apply/Cancel, session-only settings, external edits, touched-account validation and persistence across host restarts/install upgrades.
- Managed/standalone discovery after normal exit, crash, watchdog detachment and leaving viewers logged in; old managed work never resumes under a fresh standalone binding.

Native Windows acceptance requires matching host/viewer/sidecar binaries from each checkpoint and real two/five-account interaction. Portable checks cannot establish Windows listener/ACL behavior, hidden-window focus, native approval presentation or GPU/camera correctness.

## Checkpoint 1 actual validation

Implemented exact `viewer_session` tool arguments and request-local pinning, missing-target/PID-reuse refusal, session/connection-bound policy and confirmation state, authenticated managed health checks, bounded discovery parsing and stable registry search, and viewer login-reference/token rotation. Managed bridge enablement remains disabled until checkpoint 2 supplies execution/approval handling. Native strings/credentials are not copied into MCP configuration.

Focused Go checks passed for `internal/discover`, `internal/viewerapi` and `internal/mcptools`, including all tool schemas, stale targets, concurrent explicit targets, authenticated identity mismatch, copy continuations and authoritative HTTP session fields. HTTP fixtures required local-network sandbox access; their initial sandbox socket refusal was resolved without changing the tests. The portable C++ assistant-session/active-operation check passed with strict C++17 warnings/assertions. Diff hygiene passed. No native/broad builds, packaging, unrelated tests or GitHub builds ran. Native compile/runtime acceptance remains pending.

Checkpoint 1 Windows check: use matching viewer/sidecar with the ordinary viewer, list its opaque login reference and exercise inventory reads through that reference. Confirm re-enabling the bridge or relogging invalidates an old reference; a disconnected selected viewer must not fall back to another ordinary viewer. Managed profiles remain disabled at this checkpoint by design.

## Checkpoint 2 actual validation

Managed viewers can opt in through their existing native preference. They publish in the protected stable registry; enabling, logout/relogin, detach and pause retire old access. Requests and deferred inventory callbacks retain native login, permission and deadline guards. Camera mutation, snapshots and workspace application require the active character. Pending native approvals appear separately in host Attention; reviewing activates their owner and never approves them. The private host protocol is now **16**; rebuild matching host and viewer.

Focused discovery/HTTP/sidecar tests passed again, including metadata bounds and exact targeting. Strict portable assistant/session tests passed for active-operation classification, stale account/generation status, bounded approval metadata and new command encoding. Diff hygiene passed. Windows ACLs, compilation, background inventory behavior, hidden approval focus and permission revocation during native callbacks remain native acceptance items.

Windows checkpoint: enable two profiles using their native Local Assistant page; list and explicitly target each login. Exercise permitted reads in Warm/Economy, refuse background camera changes, enqueue an Ask on the inactive account with Viewer Notifications hidden, then Review through Attention. Deny, time out, revoke and disconnect while queued. No old reference should work after re-enable, logout or detach. Native Allow/Deny is the only approval path.
