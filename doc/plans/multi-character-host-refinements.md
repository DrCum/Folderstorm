# Folderstorm multi-character hosting and interface follow-up

Planning date: 2026-10-09, America/New_York. Status: planned; no source implementation in this planning turn. Continue using separate, reviewable native-build checkpoints and the existing continuous-work authorization. No subagents, large tests, viewer/build/packaging/GitHub builds, unrelated Go tests or automatic merges.

## Baseline and evidence

PR #18 is open against `feat/multi-character-transition`, at `1a3f869d7b52d8aecc6baf380939b39509a06ca1`. Current main was reverified as `56bfe960fb25ec9951f3ca6e39724e56638259cd`. The prerequisite stack remains #10 → #12 → #13 → #14 → #15 → #16 → #17 → #18. Preserve every published head and the user's archived Windows versions. Recheck remote merge/head state before implementation, then use an isolated follow-up branch/worktree based on the complete latest applicable head. Do not put fixes only on main while the prerequisite stack remains unmerged.

User-reported Windows build: PR #18 retained as Nightly 7.2.5.81853, 64-bit AVX2 / NSIS, all six focused tests passed. That archive includes the explicit shellapi.h include now present in the upstream branch.

Native results reported by the user:

- Separate-window switching, keyboard input, native/shared chat and both Warm/Economy work. Resource savings are unmeasured.
- With hosting Off, the bird's-eye transition is visible and works; Escape cancels it successfully.
- Nearby unread increases after Mark read and another incoming message.
- With hosting Off, the inactive monitor works but flickers similarly at 1 and 5 FPS. Do not call its presentation stable yet.
- Hosted world disappears when interacting with host controls or other apps. Character/chat-account dropdowns close/disrupt selection. Host active viewer cannot be unchecked reliably. Hosted transition appearance remains unverified.
- Disconnected status was correct, but there was no discoverable relaunch path. The user does not want an investigation of the isolated Second Life disconnect; plan recovery instead.
- Voice testing is unavailable. Retain voice as explicitly untested; continue independent authorized work.

Read-only source findings: the host timer runs every 100 ms; character/chat-account entries are deleted/reinserted on each tick. Automatic worker focus is permitted while characterControl or embedControl has focus. Worker fitSurface hides on unrelated foreground apps. updateMonitor invalidates on every host tick, and WM_PAINT clears the visible DC before drawing the image. These are confirmed source behaviors; the precise native flicker/visibility root cause still requires the changed Windows build.

## Checkpoint 1: stable hosted interaction and reversible hosting

Deliver a dedicated stacked draft PR for native comparison before the interface redesign.

1. Give character and conversation choices stable slot/conversation identities. Update controls only when the displayed list or actual selection changes. Preserve current selection, drafts, edit caret and keyboard navigation. Defer ordinary list replacement while a dropdown/menu is open; apply pending changes when it closes. Session loss or new restrictions must still immediately disable/redact invalid choices, and committed actions recheck their source generation. Updating unrelated status must not rebuild dropdowns.
2. Make worker focus a deliberate viewport interaction or appropriate completed-switch action. Remove automatic focus transfers caused merely by activating the host, opening a selector, clicking the hosting checkbox, or typing in shared chat. Track active dropdown/menu/compose interaction so an asynchronous focus request cannot steal its mouse-up, typing or IME input. Revalidate the actual foreground window and input lease on the native worker thread. Keep exactly one input/microphone owner.
3. Keep the active hosted surface visible while the host controls are in use and as an ordinary background window when another app is foreground. Hide on host minimize or actual standby/detach, rather than every loss of focus. Manage the surface relative to the controller's z-order without covering its dropdowns, menus, dialogs or unrelated apps, and without activating it to make it visible. Preserve the worker-owned unowned top-level surface; no foreign SetParent/window-owner embedding, synthetic focus flags, keyboard injection or always-on-top workaround.
4. Make checked and unchecked hosting transitions symmetrical. Restore original native style/placement on unhost without losing login, current input ownership, live floater geometry or background policies. If a handoff/modal transaction prevents the change, queue a bounded explicit request or explain the refusal and reflect the acknowledged state. Never silently commit Preferences. Make the separate-window action a clear reversible unhost action; retain full detach as a distinct, clearly named action.
5. Ensure outgoing and incoming visual transition legs remain visible during a host-initiated switch, while host controls remain usable. Preserve existing successful camera restoration and exact-request Escape cancellation; do not change camera choreography in this checkpoint.

Focused checks: changed interaction/deferred-update and source/session contracts where portable helpers are actually used; protocol checks only if the wire contract changes; source review of z-order/focus and git diff --check. Any added IPC command remains bounded/authenticated and versioned, with matching host/viewer binaries. Do not rerun unaffected suites.

Windows acceptance: click and navigate all dropdowns slowly, use arrow keys/Escape, click into shared compose and the hosting checkbox, toggle hosting repeatedly, Alt-Tab, minimize/restore, open native dialogs, and switch with transitions On/Off. The world remains visible, menus stay usable, disabling hosting works and another app retains its own foreground input. Check two UI scales/DPI settings where available. Keep this head separate for tracing regressions.

## Checkpoint 2: explicit Restart character recovery

Deliver a separate lifecycle PR so recovery changes can be tested independently of hosting.

1. Add a discoverable per-character Restart action for disconnected/closed workers, with a source-labelled state and explanation when the old process still needs to close. Reuse the native normal quit flow; cancellation aborts restart. Leave the other character connected and usable.
2. Bind the operation to the selected worker ID, PID, account/grid and generation before sending Quit. Check those bindings on both sides. Cancel stale requests, monitor state and queued actions when the source changes; do not act on a newly logged-in character by accident.
3. Use an explicit closing → waiting for process/profile release → launch fresh worker state machine. Relaunch only after process exit and successful profile lease acquisition. Do not spawn a competing writer, steal the lease, terminate an unrelated viewer, force-close a cancelled native dialog or start an automatic login/retry loop. Timeouts give a recoverable explanation and manual close option.
4. Open normal native login in the existing private slot profile. Preserve saved account-local settings/workspaces and the other slot. Assign a fresh worker identity/generation; invalidate old conversation/action/surface/frame bindings. Pending or uncertain sends, gestures, approvals and movement are never replayed. Existing native remembered-credential behavior is retained without putting credentials in host IPC/settings.
5. A disconnected worker must offer a reachable ordinary window for close/recovery; hidden disabled windows must not strand the character. Cancelled logout leaves an honestly labelled usable standalone worker. Recovery must tolerate control-channel loss without assuming the worker accepted Quit.

Focused checks: restart-state transitions, stale source cancellation, cancelled quit, delayed process exit/lease release and preservation of the other slot; affected protocol/identity checks if necessary. No forced live disconnect required to investigate the earlier incident.

Windows acceptance: close/restart one slot normally, cancel a native close prompt, reopen the slot and verify normal login and account-local state. If a natural disconnect occurs, try the Restart path. Verify the healthy character, its compose draft and active input remain available. No old actions send after the new login.

## Checkpoint 3: stable monitor painting

Deliver a separate preview-presentation PR. The equal flicker at 1/5 FPS makes host repainting a strong first target, not a confirmed explanation.

1. Invalidate the image only for a new accepted frame, a meaningful state change, resize or exposure. Update the title only when its name/status/whole-second age actually changes. Do not redraw unchanged imagery at every 100 ms host tick.
2. Compose the image, letterbox and footer into a back buffer and present once per paint. Handle background erase consistently so the displayed DC is not visibly cleared before StretchDIBits. Manage GDI resources and resize/minimize paths with bounded allocation and cleanup.
3. Retain the last complete validated image while waiting for a new frame or a nonblocking read lock. Atomically accept complete metadata/pixels; never show partially written data. Label stale/paused imagery clearly. Clear immediately on account/session loss, restrictions or other privacy-invalidating state; frame retention must not bypass those guards.
4. Keep the current actual low-resolution render caps, latest-frame transport, read-only image behavior and independent native session maintenance. Do not compensate for flicker by increasing capture FPS, reading back the full viewer window or adding an unbounded frame queue.
5. If native flicker persists, distinguish host paint flicker from alternating captured images: briefly hold a known valid frame in a local diagnostic and inspect successive accepted frames. Only then adjust the affected offscreen capture/restoration path. Keep this bounded; no broad graphics redesign or permanent diagnostic UI.

Focused checks: existing frame/identity tests only if acceptance/transport changes; source review of paint/erase/single-present and resource cleanup; git diff --check. Native acceptance: compare 1 and 5 FPS, Warm/Economy, resize/expose/minimize and active promotion. The monitor remains steady between captures; stale/restricted/session-ended states are honest and no input is forwarded from its image.

## Checkpoint 4: compact host and detachable panels

After the interaction fixes, deliver an interface PR, splitting condensed chrome and pop-outs into separate commits or PRs if that makes native comparison easier.

- Normal, condensed and collapsed chrome modes. Condensed mode uses one slim line for the active character, essential attention/state and small actions; detailed per-character counters/policies remain in an expandable panel/menu. Collapsed chrome leaves the hosted world directly below the title bar, with a clearly reachable restore command in the title-bar/system menu.
- Shared Chat can dock or pop out into its own resizable window. Preserve account/conversation-qualified drafts, send-as label, unread state, caret and normal keyboard/IME focus across docking. Closing the chat window hides the panel without closing characters or losing drafts.
- Character switching can dock or pop out independently. Keep character names/state obvious; provide tooltips for icon buttons. Closing that window must leave a reachable switch/restore command on the host.
- Use one model/controller for all presentations so pop-outs do not create duplicate polling, sends, state or focus ownership. Register host auxiliary windows/dialogs with the same foreground/visibility policy; opening them must not hide the world or activate a worker unexpectedly.
- Persist bounded host presentation choices only through the existing explicit Save host choices action, with backward-compatible defaults. Preserve native worker Preferences OK/Cancel and account-local workspace behavior.
- Define/test minimize, docking, DPI/monitor moves and close behavior. Pop-out controls must not close workers accidentally, stretch the viewport, trap input or strand the collapsed interface.

Focused checks: changed host-option parsing/defaults and shared controller/identity/draft contracts. Windows acceptance covers docking/undocking while composing, switching through either selector, each chrome mode, restoration controls and DPI/resize with two skins. Host shell appearance stays separate from the worker's own skin implementation.

## Checkpoint 5: five-character support and multiple monitors

Implement immediately after checkpoint 4. The user requires at least three simultaneous characters and prefers five; target **five supported characters**, with a three-character native-testing checkpoint followed by completion at five. Publish separate stacked PRs for those two useful testing points and continue to five without another confirmation. Initial grid/platform boundaries remain distinct accounts on the same supported grid, Windows.

Replace fixed two-slot assumptions with a registry capped at five managed workers. Audit selectors, handoff/rollback, duplicate-login checks, lost-input-owner recovery, chat polling/drafts/unread, per-worker background policy, restart/logout, workspace status, transition cancellation and host options for assumptions such as `1 - index`. Preserve the existing Character1/Character2 private profiles and saved choices; new slots have separate private profiles/caches/leases. No implicit extra-account login at startup. Keep one full-rate active renderer and one input/microphone owner regardless of slot count.

Use fair bounded per-worker IPC servicing so five logins do not starve source-bound sends, lifecycle actions or monitor-policy updates. Retain one outstanding request per worker, bounded histories/queues and explicit uncertain-send handling. Runtime/memory costs of the additional live simulator sessions must be measured; background render suspension does not remove those costs. Update host-choice parsing with backward-compatible defaults for new slots, without credentials or chat content.

Support multiple explicitly selected read-only monitors with bounded aggregate buffers/frame rate. When a monitor promotes character B over active character A, that same monitor should become A's preview only after the handoff succeeds. Clear old imagery, rebind to A's current identity and update the label before accepting its frames. On failure/cancellation, retain the original assignment. Other monitors keep their choices; avoid duplicate assignments and never retarget an ended session to a different login silently. This must work with compact/pop-out controls and private worker lifecycle, not by multiplying single-monitor globals.

At five logged-in characters, support up to four distinct inactive-character monitors. Preserve the existing small resolution caps and explicit per-monitor FPS targets; impose a documented aggregate preview budget and fairly distribute capacity, showing any effective rate limit instead of silently overscheduling all workers. Closing/minimizing a monitor stops its capture independently; session/restriction loss clears that source's image and does not retarget another login.

Focused checks: registry/handoff ownership with three and five workers, all-pairs switches and rollback, duplicate account/grid reservations across all slots, stale-generation restart/actions, fair bounded servicing, old/new host-option formats and monitor swap/assignment/budget contracts. Reuse affected portable tests rather than adding a broad suite. Native three-character checkpoint: log in three distinct accounts, switch every pair, reply as either inactive character, restart one slot, and exercise two monitors. Native five-character checkpoint: repeat with five accounts, four monitors, middle-slot disconnect/close/restart and cancelled transitions; verify the remaining characters stay healthy and measure aggregate CPU/GPU/RAM/VRAM and foreground impact. Voice ownership source guards remain mandatory while live voice acceptance remains explicitly untested until the user can test it.

## Delivery and validation limits

Each completed implementation checkpoint gets a separate stacked draft PR, with actual focused checks and native acceptance steps recorded, attached to this chat when the app tool responds. Continue authorized independent implementation without waiting between features. Do not merge automatically. Earlier archived builds and published heads remain available for comparison. Native build success and the user's reported passed paths must be distinguished from pending Windows acceptance; voice remains untested. No new implementation, build or live action is required to finish this plan.

## Implementation delivery status (2026-10-09)
Source checkpoints 1–4 are published as separate stacked drafts: #19 hosted interaction, #20 restart, #21 monitor painting, #22 compact chrome, #23 detached panels. #24 is the three-character registry/scheduler checkpoint. The final five-character follow-up implements four independent monitors and exact-session successful-handoff exchange (selector or monitor footer), per-monitor right-click size/rate controls and a 10 FPS aggregate cap. Capacity accounting reserves acknowledged/in-flight rates before increases. Native three-character testing may use #24 for slot/lifecycle isolation; two simultaneous monitors are available in the final follow-up, not #24. Actual checks and remaining native gates are recorded in multi-character-validation.md.

Native fixes, visual stability, resource savings, multi-character runtime and UI/DPI behavior have not been validated here. Voice runtime remains unavailable. Defaults, explicit Save host choices, native Preferences and standalone launches remain guarded; no live logins, sends, viewer/build/packaging/GitHub builds or unrelated tests were performed.
