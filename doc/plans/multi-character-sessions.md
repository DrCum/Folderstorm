# Multi-character sessions implementation plan

Status: checkpoint 1 is published in PR #10; checkpoint 2 adds per-character Warm/Economy choices in PR #12; checkpoint 3 adds shared source-bound chat/audio/voice. On 2026-10-09 the user authorized continuous implementation, a separate PR at every useful Windows testing checkpoint, and an optional bird’s-eye camera transition as the final checkpoint. Native acceptance gates below remain explicit outstanding checks, not requirements to pause source implementation awaiting the sleeping user. Do not claim native reliability or resource savings without measurements. See [usage](../../tools/session-host/README.md) and [actual checks/native acceptance](multi-character-validation.md).

Original implementation baseline: PR #9 (`feat/gesture-board`, `54092832ed`), containing the recent workspace/context and gesture-board work. PR #9 subsequently merged into `main` at `56bfe960fb` on 2026-10-09. Prototype [PR #10](https://github.com/DrCum/Folderstorm/pull/10) incorporates that main and targets `main`; its published history is preserved. The original plan-only commit `c8e20429b9` remains on the separate `docs/multi-character-sessions` worktree and is carried into the isolated prototype branch. Existing feature branches and user edits are preserved. Recheck merge states before later checkpoints.

## Outcome and initial boundaries

Users log in two different characters, keep both connected, and switch the visible character without relogging. One interface shows character status and makes every connected character's conversations available. The active character renders normally; other characters use their chosen background mode. Each retains its own account settings, Inventory, camera, restrictions and workspace state.

Start with two distinct accounts on the same supported grid and Windows, where the user builds and tests. Include grid identity in all contracts from the start. Additional accounts, mixed grids and native macOS/Linux hosting follow measured evidence. A second managed session for the same account/grid is rejected rather than silently replacing its login.

Keep the feature experimental and Off by default until native feasibility gates pass. Adding a character uses explicit normal login; startup does not silently sign in extra accounts. Ordinary viewer launches remain usable. A controller with separate worker windows is an acceptable prototype, but not completion of the one-interface experience.

No performance numbers or delivery dates are promised yet. Only one full-rate world view is intended, but every account still needs its own live simulator/session state. CPU, memory, bandwidth and GPU savings must be measured; process isolation does not eliminate these costs.

## Why workers fit this viewer

The viewer has one global/current agent, camera, Inventory model, object list and rendering pipeline, singleton chat and voice models, and a global login lifecycle. Making those multi-account inside one process would touch most of the viewer. A normal viewer process per character keeps these systems isolated and reuses their existing behavior.

Verified touchpoints at the planning baseline:

| Existing code | Implementation implication |
| --- | --- |
| `llagent.h`, `llagentcamera.h`, `llinventorymodel.cpp`, `llviewerobjectlist.h`, `pipeline.h` | Each worker owns its agent, camera, Inventory, scene and pipeline. The host never swaps these globals between accounts. |
| `llstartup.cpp`, `llviewerregion.cpp`, `llviewermessage.cpp` | Login, region circuits, capabilities, event polling and agent/session validation stay in the owning worker. |
| `llimview.*`, `llvoicevivox.h` | Adapt native chat and voice through account-bound commands/events, rather than changing their singletons into multi-account models. |
| `llappviewer.cpp` input scanning, `idle()`, `display()` and post-display snapshot/reflection work | Separate rendering schedules from network/session maintenance and input ownership. Audit dependent GL maintenance rather than skipping the whole display block. |
| `llviewerwindow.cpp`, `llviewerdisplay.cpp`, `indra/llwindow/llwindowwin32.*` | Test initialized render contexts, surface hosting, focus, sizing and recovery before choosing the final host mechanism. |
| `llappviewer.cpp` cache initialization | Secondary instances currently use read-only texture/object caches. Do not introduce simultaneous writers to one cache. |
| `fsworkspacecontroller.*`, `fsworkspacecontextadapter.*`, `fsgestureboardcontroller.*` | Existing workspace/context and gesture operations stay inside their worker, with readiness, account/session and restriction guards. |
| `fseventapibridge.*`, assistant operation/policy code | Reuse messaging/lifetime patterns where useful. Internal worker control stays separate from the user-facing assistant/MCP connection. |

`HeadlessClient` is a startup mode that also affects window/input initialization, not an existing hot-swappable standby mode. Initially, workers initialize a normal render-capable viewer and transition through a new scheduling policy.

## Architecture and ownership

```text
Session host/controller
  character selector, shared chat, notifications, worker lifecycle
      |
      +-- authenticated local IPC -- Character A viewer worker
      |                              normal active rendering
      |
      +-- authenticated local IPC -- Character B viewer worker
                                     warm / economy / optional monitor
```

The long-term host is independent of any character worker. Losing the first character must not destroy all other characters or their controls. A small Windows controller is enough for the prototype; choose the production shell/UI framework after testing surface hosting. Separate platform-neutral session/protocol logic from Windows hosting code.

For the first unified interface, prefer hosting the **active worker's existing viewer surface**, with a small shared character/chat shell around it. This preserves native menus, floaters, Inventory and workspace behavior. A renderer-only surface with recreated viewer chrome is a later choice, not required initially.

Compare native window/surface embedding with frame transfer early. Verify OpenGL contexts, child-window focus, DPI, input methods, popups, drag/drop, resize, minimization and fullscreen behavior on Windows. GPU texture sharing or CPU frame transfer is a fallback to evaluate, not assumed portable or free. Do not choose permanent full-resolution CPU readback without measuring its cost. Retain a safe separate-window fallback and report the limitation if hosting is unsupported.

Proposed modules, names to settle during implementation:

- A session registry/state machine and bounded versioned protocol, independent of viewer globals.
- Host process/lifecycle and platform surface adapter.
- A worker adapter dispatched on the viewer main thread.
- Worker render/scheduling policy.
- Chat/notification adapters and account-bound host UI.
- Later, bounded monitor-frame transport and monitor window.

### Session and IPC contract

Each worker has an opaque worker ID, grid/account identity after login, and a session generation that changes on login/reconnect. Commands, replies, chat events, surfaces and frames carry their owning identity/generation. Conversation names or IDs alone are not enough to identify the sending account.

Use a private local transport, initially authenticated Windows named pipes or an equivalent restricted channel. Authenticate spawned workers with a per-launch capability delivered through a restricted channel/inherited handle; no passwords or reusable tokens in process arguments. Validate protocol version, payload size/type, sequence, deadlines and session. Dispatch viewer actions on its main thread. Reject stale replies, unsupported commands and unowned surfaces.

Track login, connected, active, warm, economy, promoting, disconnected and failed states. Promotion is serialized, not two workers independently claiming to be active. Worker liveness is separate from simulator connectivity. Bound queues and apply backpressure. Status/old preview frames may be coalesced; a chat send cannot silently disappear. Transport failure reports an uncertain send outcome rather than resending text that might already have reached the server.

Keep this protocol internal initially. Basic switching/chat does not require MCP. Audit worker assistant-listener startup to avoid port collisions: temporarily disable it for managed workers initially, or use an explicitly configured independent endpoint. Never route existing assistant requests implicitly to whichever character is active. Future multi-account MCP needs explicit target identity plus existing permission/operation guards.

### Files, settings and credentials

One worker owns account/grid preferences, chat logs, workspace files and board definitions. Isolate runtime/configuration paths where needed so common settings, lock files, crash/update state and launch dispatch cannot race. Define host-shared preferences versus character preferences; do not merge arbitrary whole settings files on shutdown.

Use the worker's existing supported login/credential facilities. No credentials in workspace exports, manager configuration, diagnostics or IPC logs. Retain account-private conversation logs; the shared view does not create a persistent combined chat archive by default. Begin with safe read-only shared cache access or isolated writable caches; audit ownership before considering a shared writable cache.

## Background modes

| Mode | Intended behavior | Tradeoff to measure |
| --- | --- | --- |
| Active | Normal rendering and foreground input. | Normal configured scene cost. |
| Warm standby | Suppress continuous world drawing; retain useful scene/render state and a resumable context. | Faster return, higher retained memory. |
| Economy standby | Suppress continuous drawing; trim reclaimable GPU/scene resources and unnecessary asset work while retaining a healthy live session. | Lower resource target, slower rebuilding. |

Both standby modes continue necessary transport, capabilities/event queues, timers, chat, offers, teleports, avatar/session updates and orderly logout. Do not suspend the process or slow its whole main loop to a monitor's frame rate. Background region crossings and teleports must work without a visible viewport.

Implement warm first. Audit texture fetching, mesh/scene maintenance, reflection/probe work and snapshot callbacks individually. Economy preserves enough state to reconstruct the view without relogging. If trimming yields no meaningful measured saving, keep economy experimental rather than presenting it as proven.

Standby is temporary policy over normal settings: no saved quality downgrade, named-workspace change or false modified indicator. Promotion restores that character's configured graphics/camera settings. Expose scene staleness honestly instead of suggesting a continuously current background view.

## Safe switching, chat and notifications

### Switching transaction

1. Validate the target connection/session and incompatible login/shutdown transitions.
2. Revoke the old foreground input lease. Clear transient held movement keys, mouse capture and joystick input; do not undo persistent actions such as sitting or rewrite saved settings.
3. Prepare the target surface/context, size and rendering policy. Show a named loading state if needed; label any retained old frame as stale.
4. Receive target readiness/first-frame acknowledgment, bind its surface and grant exactly one input owner. Transition input is held/discarded appropriately, never replayed to another character.
5. Put the old worker into its chosen background mode. On failure, return to the old connected worker or show a recoverable disconnected state. Never leave two input owners or a permanently black view.

Chat remains usable independently of viewport promotion. Selecting a conversation does not move an avatar or start a camera/render transition. Queued commands stay bound to their original account even if the visible character changes.

### Shared conversations

- Put the sending character's name beside the conversation and compose box; color/portrait cannot be the only identity cue.
- Keep compose boxes and drafts bound to a character/conversation. World switching never retargets typed text. Preserve native nearby-chat, IM and group-chat distinctions.
- Send typing/read/chat actions through that worker's native model and restrictions. Report blocked/unavailable errors to the same conversation without trying another account.
- Use account/session-qualified IDs, bounded event/history windows, acknowledgments and deduplication. Recover a short missed window where supported; report history gaps honestly.
- Show per-character unread/connection badges. Offers, permissions, payments and teleport requests belong to their source character. Explicit responses recheck current restrictions/session; do not auto-accept them.
- Honor logging/privacy choices and restrictions on exposed information. The host does not fetch another character's private Inventory/chat state merely because it is the active renderer.

### Voice and sound

Start with one explicit microphone owner, following the active character, and no background voice listening. Revoke old transmission before granting the target; background workers never auto-transmit. Show the owner. A pinned microphone owner or background listening can follow once native voice-session behavior is proven.

Offer independent background world/UI sound muting as temporary session policy; restore normal settings on promotion without rewriting volume preferences. Audit native voice/audio helper-process ownership too: viewer process isolation alone does not establish microphone exclusivity.

## Existing Folderstorm features and recovery

Keep workspace/layout profiles, graphics/camera context, selected HUD restoration, Inventory windows and gesture boards account-local. Preserve live unsaved arrangement on promotion rather than automatically applying a named profile. Later per-character startup/profile choices reuse `FSWorkspaceController` after existing UI/Inventory readiness gates. Preserve HUD review, Preferences OK/Cancel, Previous-arrangement semantics and restrictions.

Never silently commit an open Preferences transaction. If switching with that transaction cannot preserve it safely, explain and retain the current view. Host controls stay outside worker workspace capture. Other-account conversation content never enters a named workspace.

Crashes/disconnects are independent: keep the host available, mark the failed character unavailable, cancel pending actions and offer normal reconnect. Reconnect changes the generation and invalidates old conversation/action/surface handles. Never replay old gestures, movement, attachments, messages or approvals.

Define host close/crash behavior. Ordinary close offers managed-session logout choices with bounded graceful shutdown. Only host-created/owned workers may be terminated by that lifecycle; unrelated viewers are never adopted or killed. Host failure revokes input/microphone leases and uses an agreed fallback, such as revealing ordinary worker windows. Prevent orphaned invisible sessions and repeated automatic login loops.

## Later feature: inactive-character monitor

After reliable switching and both standby modes, add an optional small **read-only** monitor for a chosen background character. Users watch for visual changes; automated image analysis/alerts are a separate future feature.

- Proposed sizes: 320 × 180, 480 × 270 or 640 × 360, properly aspect-fitted. Default 1 FPS; consider bounded choices such as 0.5, 1, 2 and 5 FPS after measurement. These are targets, not guaranteed performance.
- Render that worker's own camera/scene under its restrictions. Prefer an actual low-resolution target over full-window drawing followed by shrinking. Reuse viewport/aspect work where applicable.
- Label character, connection and last-frame age. Make stale/disconnected frames obvious. Provide **Switch to character**; preview world clicks/movement are disabled initially.
- Schedule occasional drawing separately from network/session maintenance, with necessary GL work between frames. Economy plus a monitor needs additional resources; communicate the different cost.
- Frames carry worker/session generation, dimensions and monotonic time. Keep a bounded latest-frame buffer; drop obsolete frames instead of building delayed video. Stop old requests on promotion, disconnect, resize and context recovery.
- Bound simultaneous monitors, aggregate frame rate and buffers. Pause a hidden/closed monitor while its worker remains connected in its chosen mode. Make minimized-host behavior explicit.
- Measure shared textures versus asynchronous readback. Opening a monitor does not record, take screenshots, play gestures or automate character actions.

Illustrative host layout, not the final skin design:

```text
[Alex: active] [Sam: warm · 3 unread] [+ Log in character]
-------------------------------------------------------
| Active character's normal viewer                    |
|                                                      |
-------------------------------------------------------
Chat: [Alex] [Sam · 3]       Send as: Sam
[Sam's selected conversation and bound compose box]

Optional monitor: Sam              [Switch to character]
| low-resolution world view |       1 FPS · updated 1s ago
```

Use existing skin colors and controls where practical, with compact host chrome. Character tabs/dropdown suffice initially. A GTA-style selection wheel is polish after reliable switching, with clear names/status and no ambiguity about the chat sender.

## Delivery checkpoints and acceptance gates

Each checkpoint is a reviewable implementation/PR boundary. Related controller/worker changes may share a commit; avoid one unverified rewrite. No subagents are requested.

### 1. Two-character feasibility prototype

Build a minimal independent controller and authenticated worker handshake behind an Off-by-default option. Launch two normally initialized workers through existing login. Add input ownership, reversible render suspension and health/resource measurements. Run a small native-hosting spike in this checkpoint to reveal the largest platform risk early; separate windows remain the test fallback.

Gate: both characters receive chat while one is undrawn; the background character teleports/crosses regions and resumes rendering without relogging. Test same/different regions and no leaking held movement. Record whether surface hosting works on the user's Windows build and the costs/limits of alternatives. This does not yet complete the full interface.

### 2. Warm and economy policies

Complete rendering/GL maintenance separation, expose per-character warm/economy choices and trim only reconstructible resources. Preserve settings and add acknowledged transitions/timeouts/recovery.

Gate: warm resumes reliably; economy shows a measured saving or stays experimental. Chat/session health stays independent of rendering through idle periods, region changes and reconnect. Entering standby never modifies persistent graphics/workspace state.

### 3. Shared chat and source-bound events

Add qualified conversations, unread indicators, drafts, send/read actions and bounded event streaming. Route supported native requests without auto-acceptance. Enforce microphone/input ownership and temporary background audio policy.

Gate: receive/reply as either character without switching the view. Typing, pending sends and dialogs cannot change sender/recipient during switching. Handle restrictions, duplicate/stale events and uncertain sends. Only the chosen account can transmit voice.

### 4. One visible interface and reliable promotion

Turn the successful hosting spike into the production adapter. Add character tabs/dropdown, loading states, serialized handoff and an explicit fallback. Preserve native UI, focus, dialogs and workspace geometry. Consider the wheel after this path works.

Gate: rapid switches, resize, larger UI scale, DPI, minimize/restore and worker failure do not cause black views, stuck input or mixed-account dialogs. Switching needs no relogin; shared chat stays usable. Validate supported skins on the user's Windows build.

### 5. Workspace integration and lifecycle polish

Audit saved/live arrangements, graphics/camera/HUD context, gesture boards, startup readiness, Preferences, settings/cache ownership, reconnect and graceful shutdown. Add clear failure messages and guards to all delayed host actions.

Gate: each character retains unsaved windows and profile state. Cancel still rolls back its edits. Missing HUD/Inventory references use existing restore reports. One worker failure leaves the other connected; reconnect cannot execute old-login actions. Files/listeners do not collide.

### 6. Optional low-resolution monitor

Add actual low-resolution rendering, default 1 FPS scheduling, freshness labels, bounded latest-frame transport and explicit promotion. Account for additional warm/economy cost and pause hidden monitors.

Gate: chat/network health continues between frames. Aspect/identity remain correct through resize/reconnect. Obsolete frames stop and promotion safely takes over context/surface ownership. Measure the added cost. Opening the monitor never moves an avatar or sends chat.

### 7. Optional bird’s-eye character transition

Default Off. Animate the old character’s camera upward to a bounded bird’s-eye view, perform the existing acknowledged input handoff, then descend from the target character’s bird’s-eye view to its own saved live camera. Keep this visual effect separate from chat selection and avatar movement; never teleport or change avatar position. Restore each original camera exactly on completion, cancellation, restrictions, disconnect or host detachment. Respect camera restrictions, mouselook, scripted/follow cameras and reduced-motion/instant switching. Bind delayed animation steps to the account/session and cancel stale operations. Expose a skip/instant fallback; never wait indefinitely for an animation or stream unbounded frames.

Gate: same/different regions, camera modes and restrictions, rapid switches, cancellation, delayed first frames, disconnect and Alt-Tab do not leave a changed camera or input owner. Native visual quality and timing need Windows testing.

## Verification and measurements

The initial planning task used documentation/source inspection only. The subsequent checkpoint 1 implementation has focused portable checks, with actual evidence and remaining native acceptance recorded separately in [prototype validation](multi-character-validation.md). No native behavior or performance saving has been verified in the managed Linux workspace.

During implementation, use focused checks for changed contracts: stale generations, bounded payloads/queues, single input/microphone ownership, promotion rollback, sender binding, uncertain sends and frame identity. Fake workers are appropriate when testing these real contracts. Parse changed XML, inspect native API/types and run `git diff --check`. No unrelated Go tests, viewer/build/packaging/GitHub builds or broad suites. Native feasibility/acceptance needs the user's Windows build/test cycle and must be reported separately.

Compare the same scene/camera/graphics/account conditions:

| Configuration | Measurements |
| --- | --- |
| One ordinary viewer | Baseline CPU/GPU, RAM/VRAM, bandwidth and foreground FPS. |
| Two ordinary viewers | Existing multi-character cost. |
| One active + warm | Aggregate resources, background health, time to usable first frame. |
| One active + economy | Same metrics, rebuild/staleness and retained-memory differences. |
| One active + background 1 FPS monitor | Extra rendering/transport cost, frame age and foreground impact. |

Measure aggregate worker/host memory and GPU allocations, not just the host. Record typical/slow switch times, missing/duplicate messages, disconnects and scene correctness. Include same/different regions, background teleport, DPI changes, voice ownership, crashes and logout. Increase beyond two accounts only when the evidence supports it.

Definition of done: two supported characters share one interface, communicate through clearly identified conversations, switch with isolated live state and functioning background modes, and optionally expose a truly low-resolution read-only monitor. State actual savings, unsupported paths and remaining native tests explicitly. A paused renderer, a chat multiplexer or several hidden viewer windows is an intermediate checkpoint.
