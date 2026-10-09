# Multi-character prototype validation

Branch: `feat/multi-character-prototype`, [PR #10](https://github.com/DrCum/Folderstorm/pull/10) against main after PR #9 merged. Scope: checkpoint 1 source implementation and Windows hosting experiment. Native runtime feasibility gates are not passed yet.

## Added in this checkpoint

- [x] Explicit Windows session-controller executable, built/copied beside the matching viewer; normal launches remain unmanaged.
- [x] Two ordinary render-capable worker processes with explicit native login and isolated persistent profile/cache/log roots.
- [x] Exclusive inherited profile lease through worker process cleanup/exit; concurrent controllers cannot reuse a live slot.
- [x] Authenticated inherited local pipe capability, current-user ACL, remote-client rejection and restricted handle inheritance.
- [x] Fixed 512-byte validated/versioned protocol; worker IDs, sequences, generations, bounded UTF-8 fields and owned-process surface checks.
- [x] One pending request/reply per worker and nonblocking overlapped read/write with owned buffers and cancellation drain.
- [x] Canonical native login-identity reservations before contacting the login service; explicit denial for a duplicate managed reservation.
- [x] Warm rendering policy using existing hidden-viewer teleport/texture/material/pick maintenance; normal session/network/Inventory callback processing continues.
- [x] Serialized old-input revocation and target promotion after two normal buffer swaps; stale-generation refusal, rollback and a lost-peer input-lease fence.
- [x] Clear transient key/mouse/agent controls and reset joystick deltas; block keys held during promotion until release, including repeated text.
- [x] Gate background key/mouse handlers and direct joystick flycam/build paths; retain existing automatic avatar actions and account restrictions.
- [x] Temporary settings overlays; managed voice and MCP listener disabled; normal values restored on detachment.
- [x] Optional hosting of the active worker's existing Windows surface with PID/host ownership checks, resizing and standalone restoration.
- [x] Explicit detach/ordinary-close behavior and local transport watchdog; no arbitrary process adoption or forced termination.
- [x] Per-worker working-set, presented-frame and main-loop counters for the user's measurements.

## Actual focused checks

- Portable C++ protocol/handoff test compiled with C++17, `-Wall -Wextra -Werror -pedantic`, and ran successfully. It covers wire byte order/round-trip, malformed/unsupported frames, zero identity/sequence, invalid UUID/UTF-8, bounds, failed-decode snapshot preservation, serial revoke/promote, delayed-frame refusal, wrong-worker/stale-generation refusal, disconnected-old-session handling, rollback, duplicate grid-qualified reservations and installation-local executable filename validation.
- Source review traced normal startup/directory isolation, profile-handle ownership through exit, bounded pipe operation lifetimes, input reset/physical-key suppression, native voice/assistant enable paths, actual swap acknowledgment, failure/close flows and main-loop callbacks.
- Source review retained the existing hidden-window `display()` path. Its teleport state updates are necessary to signal arrival, and its texture/material/pick upkeep must continue; standby gates world drawing and post-display render-dependent work rather than skipping maintenance wholesale.
- Python AST syntax passed for `viewer_manifest.py`. Focused execution of its actual host-staging method passed for configuration-specific output, build-root fallback, configuration `.` and missing-host refusal; no packaging/build was invoked.
- CMake source/header/host-target and manifest-guide registration checks, documentation links/code fences and `git diff --check` passed.

No native Windows compiler/runtime is available in this managed Linux workspace. No viewer, build, packaging, GitHub build, graphical/native login, performance benchmark or unrelated Go suite was run. The portable test does not establish MSVC compilation, pipe/kernel behavior, renderer correctness, crash recovery or performance savings.

## Reviewed local-build findings (2026-10-09)

The user supplied a local build-agent patch and reported successful Windows viewer/controller compilation, the protocol test under MSVC `/std:c++17 /W4 /WX`, and NSIS file-list verification after local fixes. These are reported local results, not builds or runtime tests performed in this workspace. The patch was reviewed against the APIs before applying changes:

- Prototype: retain `LLControlVariablePtr` ownership when accessing temporary settings; reuse null-safe `LLStartUp::getUserId()` for credential-key reservations; write the matching viewer guide as UTF-8 bytes and register its generated file for installer inclusion.
- Inherited gesture-board fix: use `LLStringUtil::format_map_t` and decimal string arguments for the localized page count.
- Inherited manifest fix: resolve both plugin compatibility manifests from the absolute source location, supporting separate-drive/junction builds and relative source arguments.

Focused follow-up checks passed: the existing portable protocol/handoff test, with added native credential-key reservation/release cases; actual filename-generation and NSIS inclusion/removal logic for packaged, UTF-8 and build-directory names; actual compatibility-manifest copying for absolute/relative source paths with an unrelated build root; changed Python syntax and `git diff --check`. No native renderer/input policy, session isolation, restriction guard or Preferences save/restore behavior was bypassed. This was build-fix validation; runtime observations follow below.

## Reported Windows runtime and hosted-focus follow-up (2026-10-09)

The user reports that warm `draws/s` immediately settles to `0.0`, IM delivery works and switching is quick. Hosted switching and mouse input also work, but hosted keyboard input is absent and the active viewer draws at its usual background rate. Disabling hosting and clicking the standalone viewer restores keyboard input and the higher draw rate. These are user-reported observations, not aggregate performance measurements or completion of the full native checklist.

Source review found that reparenting and custom-drawn UI clicks did not request native client keyboard focus. The follow-up requests focus through `LLWindowWin32::focusClient()` on its owning native thread, on embedding, viewer clicks and versioned controller focus commands after switching/reactivation. Worker requests require readiness, active input ownership, the actual hosted parent and the PID-validated foreground controller. The native queue rechecks the root, foreground and enabled/visible state, including when a queued request's window has since detached. It does not forward keystrokes or force `gFocusMgr` flags. Normal focus callbacks restore normal foreground scheduling and preserve background yielding when another app has focus.

The controller reports the viewer's actual callback-derived keyboard-focus state. The private protocol version is now 2; both binaries must be rebuilt together. The focused portable test passed with focus command/status round-trips and rejection of the old protocol version, alongside the existing identity/handoff tests; source review and `git diff --check` passed. No native build or window-thread execution was run here. Hosted typing, movement, switching, Alt-Tab, native dialogs and focus indicators need Windows confirmation before this defect is considered resolved. Standby teleport completion and normal worker/controller-close recovery remain particularly useful tests on the already-running build.

## Windows acceptance before proceeding through the roadmap gates

1. Build the Windows viewer normally on the user's machine; verify the controller is present next to its matching viewer in the build and installed package.
2. Start one and then two distinct same-grid characters through normal login. Confirm fresh independent profiles, subsequent profile persistence and no edits to the normal installation's user files. A second controller must fail to launch an occupied slot, including while its detached worker remains open.
3. Switch repeatedly in separate-window mode. Verify the old worker stops presenting normal world frames while its loop counter continues, and the target resumes without relogging. Check minimized target restoration, long first-frame preparation, keyboard held across switching, mouse capture, joystick/flycam/build behavior and a modal dialog.
4. Send IM/nearby/group messages to both accounts during standby; switch back and verify delivery and native identity/logging. Shared cross-account conversations are not present yet. Check Inventory offers and restriction-controlled dialogs without assuming background auto-acceptance.
5. Test same/different regions, region crossing and a teleport completing in standby. Verify its arrival state clears, avatar behavior and view recover. Leave a worker backgrounded long enough to observe connection health and memory trends.
6. Try duplicate canonical login names and login failure/retry; confirm the second managed reservation is rejected before authentication and a cancelled/disconnected reservation can be retried. Use distinct accounts: external sessions/server aliases cannot be exhaustively preflighted.
7. Confirm voice remains unavailable in all managed workers and cannot be enabled through Preferences while managed. Confirm local assistant enablement cannot bind a worker listener. Detach and verify ordinary behavior/settings return with no automatic message/gesture/action replay.
8. Close/disconnect one worker, break its control connection and close the host normally. Verify the other worker remains usable, detach preserves login, a hosted child is not destroyed by normal host close, and the profile lease is only released after worker cleanup/exit.
9. Enable optional hosting after separate-window tests pass. Check normal and larger UI scales, two supported skins, IME/Unicode, dialogs/popups, Inventory drag/drop, resize, DPI/monitor changes and fullscreen. Verify each character retains its own unsaved windows, theme, camera and Preferences transaction. Forced host-crash behavior with a hosted child is an explicit open release blocker; it may need another hosting mechanism or additional native recovery work.
10. Measure aggregate CPU/GPU, RAM/VRAM, network traffic, foreground FPS and typical/slow switching delay against one viewer, two visible viewers, and a minimized second viewer. Warm retains initialized resources; no economy or monitor savings are claimed.

## Remaining roadmap

Source implementation covers all seven checkpoints in PRs #10 and #12–#17. Native feasibility/acceptance and measured resource/monitor cost remain outstanding; these source-complete drafts are not a claim of release readiness. Use the individual checklists below and the completion table in multi-character-sessions.md.

## Checkpoint 2: background policy (follow-up branch)

- [x] Independent Warm/Economy selection for each managed character; active rendering uses ordinary configured quality.
- [x] Serialized handoff acknowledges the selected standby mode; rollback still prepares the old character as Active.
- [x] Economy releases disposable screen/shadow targets on the current-context display path, including targets recreated by settings callbacks; rebuild uses the ordinary configured allocator. Scene/texture/session state and required maintenance remain live.
- [x] Defer hidden-window resize/shadow allocation until rendering resumes; both standby modes retain frequent main-loop servicing.
- [x] Status identifies experimental Economy and released targets; no measured saving is claimed.
- [x] A refused standby policy change resets the dropdown rather than endlessly retrying a blocked modal action.

Actual checks: portable protocol tests passed, including Economy wire round-trip, invalid mode rejection, chosen-policy acknowledgment, stale identities and rollback to Active. Source review traced GL-context placement, deferred resize, normal buffer reconstruction and unchanged saved graphics settings. `git diff --check` passed. Protocol version 3 requires matching controller/viewer builds. No native/viewer/build/packaging tests were run.

Windows checkpoint: compare Warm and Economy VRAM in the same scene; switch back repeatedly, check shadows/reflections, resize/change graphics while inactive, test a background teleport and normal detachment. Confirm chat/loop counters continue, graphics choices persist and no black first frame/stuck input appears. Hosted focus remains a pending PR #10 native test. These checks do not stop the authorized next source checkpoint.

## Checkpoint 3: source-bound shared chat, attention and audio

- [x] Character/conversation selectors, bounded read-only live history, source-labelled compose box and per-conversation drafts independent of viewport switching.
- [x] Receive native Nearby/IM/group/conference messages; enumerate existing native conversations without opening floaters or reading transcript files.
- [x] Native public-channel/initialized IM send, explicit accepted-versus-delivered status, native restriction checks and account/grid/generation binding captured before queuing. One pending send; no uncertain replay.
- [x] Source-bound P2P typing/stop with native support-account restrictions; native IM Mark read, shell-only Nearby/attention badges.
- [x] Generic notification attention and Review in viewer; native offers/approvals stay inside the owning viewer.
- [x] Bounded 256-event worker ring, 512-line/64-conversation host view, deduplication and gap reporting; session changes clear prior data.
- [x] Restriction changes prevent raw cached names/locations entering shared chat; receiver restrictions are rechecked before export.
- [x] Optional voice follows the acknowledged active owner (Off default), old PTT/tuning revocation, backend/volume/tuning/mic gates, and optional temporary background sound/media muting (On default).
- [x] Host chat controls keep their keyboard focus; automatic hosted-focus requests do not steal focus from the compose box.

Checks actually run: strict portable C++17 protocol/handoff and chat-model tests passed. They cover multiline UTF-8 bounds, reserved bytes, unsupported topics, source/grid/account/generation binding, retained-window gaps, event deduplication, bounded history, draft persistence across world mode changes and invalidation at a new login. Source review traced LLIMModel/FSNearbyChat restrictions and native echo, P2P typing checks, notification signal types, voice gates, audio gains and UI request ownership. `git diff --check` passed. No native/viewer/build/packaging/GitHub tests were run. Protocol version 4 requires matching binaries.

Windows testing: reply as the inactive character without switching world view; test Nearby, P2P, groups and conferences, separate drafts and unread/Mark read; switch while composing/queued send and verify sender/recipient; reconnect with an unsent draft and ensure it cannot send to the new login; check restrictions and transport-loss uncertain-send wording. Confirm host compose focus survives updates and viewport clicks return native typing. Test voice Off, then On with distinct accounts, PTT/toggle/tuning during switching (including open-mic preference), background silence and gain restoration after detach. Verify notification Review switches to its account and never accepts the offer. Shared history begins after worker readiness; open earlier history through native chat. Renderer/host crash/DPI/skin checks remain pending.

## Checkpoint 4: fitted surface and compact shell

- [x] Worker-owned unowned top-level borderless surface fitted to the controller viewport, replacing cross-process child parenting. No SetParent or foreign HWND owner assignment remains in the worker adapter.
- [x] Native own-thread guarded activation/focus, host-chat foreground rendering policy without synthetic input/focus flags, other-app/minimize hiding and owned-dialog preservation.
- [x] Fit before first-frame promotion, retain hidden client geometry across standby, restore ordinary style/placement on explicit unhost/detach/watchdog.
- [x] Compact named character dropdown, optional collapsed Chat panel, DPI-scaled shell geometry/fonts and per-monitor DPI handling.
- [x] Preferences/modal switching guard: no implicit transaction commit, clear separate-window/fullscreen fallback.

Checks: existing strict portable protocol/handoff and chat-model tests passed with protocol 5; focused source assertions confirm no foreign parent/owner mutation, retained-geometry revoke, native guarded focus and Preferences/DPI paths; `git diff --check` passed. Source review followed first-frame ownership, popup z-order and host-close/watchdog recovery. No viewer/build/native/packaging tests ran.

Windows checkpoint: compare PR #13 and this hosting mechanism for keyboard/IME, native dialogs, chat compose focus and foreground draw rate. Move/resize/minimize/Alt-Tab the host, use 100–200% DPI and two skins, and verify unsaved floater positions persist across switches. Close/force-crash the controller (with disposable logged-in test accounts) and confirm both native viewer windows recover without relogging or destroying their surfaces. Test fullscreen through separate-window fallback. Native acceptance remains pending; the removed destruction path is a source finding, not a claim of proven crash reliability.

## Checkpoint 5: workspace/lifecycle and cache ownership

- [x] Native per-character workspace status/modified/Preferences indicator and explicit workspace-switcher action, with source/session/active-owner guards. Existing named/Last startup readiness, live Previous, HUD review, Inventory and board ownership remain native to each worker.
- [x] Private default runtime caches enforced for managed workers; copied custom paths cannot introduce shared writers or trigger migration/purge of the external path. Saved custom choices are untouched.
- [x] Source-labelled close-character action, native logout confirmation, host close logout/leave-connected/Cancel choices and process-exit waiting. Stale logout/session changes stop the action rather than replaying it.
- [x] Explicit bounded atomic host-choice persistence, Off-by-default voice/hosting, controller settings lease and existing worker profile lease through cleanup. No credential/chat/Inventory content in manager settings.

Actual checks: strict portable host-options and updated protocol/handoff checks passed. Options tests cover defaults, valid round-trip, bounds, duplicate/unknown fields and failed-parse atomicity. Source assertions/review covered native switcher registration, account/session/Preferences/active ownership, cache path/migration guards and native logout/lease lifetime. `git diff --check` passed. No native/viewer/build/packaging tests ran.

Windows checkpoint: use different workspaces/cameras/graphics/HUDs/boards in each worker, move unsaved floaters and verify independent state after switching; Preferences Cancel and switching refusal while open; per-character named/Last startup restore and missing-folder/HUD reports. Copy a custom cache path into a managed profile and confirm runtime cache remains private and the external cache is not purged/migrated. Save/reopen host choices, relaunch after native logout, cancel logout and confirm its separate-window state. Disconnect/reconnect during pending menu/logout/chat actions; no stale actions should replay, and the other character should stay usable.

## Checkpoint 6: bounded read-only monitor

- [x] Explicit Off-by-default inactive-character monitor, 320×180 / 1 FPS default, optional 480×270 / 640×360 and 0.5 / 2 / 5 FPS targets.
- [x] Actual bounded offscreen world render/readback, no full-window fallback, no ordinary presentation or first-frame ownership credit. Preview cadence remains independent of native simulator maintenance.
- [x] One bounded latest-frame mapping with explicit inherited handles, nonblocking mutex, partial/abandoned-frame rejection and account/grid/generation/PID/sequence/geometry validation.
- [x] Aspect-fit display, freshness/stale/waiting/unavailable indicators, no image input, explicit switch footer, pause/stop on active/switch/minimize/close/session loss and no automatic retarget at a new login.
- [x] UI/HUD and snapshot files/sounds/animations excluded; native viewport/projection/cursor/debug state restored; size/rate-only host-choice persistence remains backward compatible.

Actual checks: strict portable protocol, preview and changed host-options checks passed. New checks cover resolution/rate policy, landscape/portrait/extreme/overflow geometry, frame metadata round-trip and source/account/grid/generation/PID/ownership rejection; options cover backward compatibility and invalid preview choices. Source review traced inherited mapping/mutex bootstrap, bounded pixel copy, snapshot RAII cleanup, forced small target and no full-window fallback, offscreen-only render gates and two-normal-frame promotion. `git diff --check` passed. No viewer/build/packaging/native/GitHub tests ran. Protocol version 7 requires matching binaries.

Windows checkpoint: open a monitor for the inactive character in Warm and Economy; compare scene/aspect/color/orientation with its active view. Test all size/rate targets, teleport/region crossing, restrictions, monitor/controller minimize/close, active switching, stale frames and disconnect/relogin without retargeting. Ensure native snapshot UI/HUD/projection and active viewer buffers/shadows recover, no hidden window presents, image clicks cause no action, and the switch footer names/promotes the expected character. Measure aggregate GPU/CPU/VRAM and network while Off versus 1 FPS; bounded resolution is source-verified but allocator/post-effect/readback cost and visible results remain unmeasured. Unsupported Forward graphics should show unavailable status.

## Checkpoint 7: optional finite bird’s-eye transition

- [x] Off-default ~1.1-second outgoing rise and incoming descent; serialized native handoff, finite clock, bounded height and no effect for chat selection.
- [x] Renderer-camera pose only; per-frame RAII restoration of native pose/frustum before other GL work, without agent camera/position/settings changes.
- [x] Escape and instant-menu cancellation bound to exact source/account/grid/generation/pending mode-request sequence; no reply/retry/action replay or input acquisition from cancellation.
- [x] Reduced-motion, camera restrictions/modes, modal/Preferences/flycam, invalid/far camera, other-app/minimize, region/session/disconnect/detach/watchdog guards; rollback remains instant.
- [x] Input/PTT revoked before visual work; no ownership while animating, incoming native frames still required, background audio temporarily muted during legs.
- [x] Bounded saved effect preference with backward-compatible Off default, named switching status and retained uncertain-send timeout wording.

Actual checks: strict portable C++17 transition, changed protocol/handoff, preview identity and host-options tests passed. Tests exercise finite clock/easing bounds and monotonicity, generation/time/permission expiry, exact cancellation nonce/account/grid/worker binding, stale later-switch rejection, Transitioning ownership refusal and preview restriction/transition refusal. Source review traces every start/finish/cancel path, original camera/frustum restoration, native modal/camera/restriction/foreground guards and absence of agent camera/position/settings mutation. `git diff --check` passed. No viewer/build/packaging/native/GitHub builds or unrelated Go tests. Protocol 8 requires matching binaries.

Windows checkpoint: enable the effect in hosted and separate-window modes; verify rise above the outgoing avatar and descent to the incoming character's own live camera in same/different regions. Check unsaved camera angles/zoom, workspaces staying unmodified, HUDs/chrome and shadows/reflections, modest draw distance, sitting/moving avatars and slow preparation. Escape/instant in each leg, rapid repeated selection, Alt-Tab/minimize, disconnect/relogin, native dialogs and controller loss must leave normal camera and one usable input owner. Check mouselook/scripted/follow/flycam/restrictions and Windows reduced-motion instant fallbacks. Ensure shared chat remains source-bound, voice never overlaps, monitor pauses across promotion and ordinary snapshots after switching use the normal camera. Visual polish and stability need native results; source checks alone cannot establish a finished release.

## Follow-up: three shared-chat review findings

- [x] Restriction flushes preserve monotonically increasing event IDs; new-login generation is the only reset. Discarded unseen events produce an explicit gap even with an empty queue.
- [x] Deduplicated, saturating shell-local Nearby unread count; no own-echo/catalog increment, source-bound Mark read and immediate dropdown refresh. Active/standby and panel visibility do not gate counting.
- [x] Native IM counts remain absolute; notification attention is explicitly boolean.
- [x] Shared FSData support-contact helper used by native compose/typing and shared compose/typing, returning a shared refusal before native transport and preserving the draft. Existing session/RLV checks remain.

Actual checks: recreated the two defect reproductions against the exact original #17 chat header (host cursor 3 / worker latest 1 / None; Nearby unread 0). Strict C++17 changed chat-model regression, contact-policy and protocol tests passed with `-Wall -Wextra -Werror -pedantic`. New regressions cover restriction on/off without intervening host responses (as during a pending handoff), missing-event/empty-buffer gaps, new-generation restart, duplicate delivery, standby modes, Mark read/new message, stale-account read refusal, own echoes, saturation, native absolute IM counts, preserved drafts and boolean attention. Contact cases cover flagged/unflagged sender against support/ordinary contacts and unchanged non-direct scope. Focused source/type review confirms shared policy calls before send/typing, correct sender/contact mapping, sender UUID forwarding, immediate badge refresh and error/draft behavior. `git diff --check` passed. No viewer/build/packaging/GitHub builds, unrelated Go tests or subagents.

Native Windows acceptance remains outstanding: matching MSVC host/viewer build and installer staging, brief names/location restriction changes during both handoff legs, Nearby incoming/duplicate/own-echo badges with inactive account and collapsed Chat, Mark read then another message, and authorized flagged/unflagged test accounts against support/ordinary contacts. Confirm refused shared sends retain the draft and produce no native transport/typing action. These are source/test fixes; portable tests cannot prove Win32 runtime behavior. The existing focus/IME, Economy, voice/PTT, lifecycle, monitor and interrupted-camera-transition checklist still applies. Keep the stack in draft.

### User-reported Windows build, 2026-10-09

The user reports that PR #18 built successfully as retained Nightly **7.2.5.81853**, using **64-bit AVX2 and NSIS**, and that **all six focused tests passed**. Earlier versions remain preserved for comparison. The archived build includes a one-line local `shellapi.h` include fix; the follow-up source commit adds that explicit include to `tools/session-host/main.cpp`, which calls `ShellExecuteW` and already links `shell32`. This is a declaration fix with no runtime behavior change.

This is user-reported native build/test evidence, not a build performed in this managed workspace. The archived build used PR #18 plus the local include fix; the source follow-up also documents the result. Two-account graphical/runtime acceptance remains pending, including hosting/focus/IME, restriction changes during handoff, Nearby unread/Mark read, support-contact refusal and draft retention, voice ownership, Economy recovery, lifecycle, monitor output and transition cancellation. Keep the stack in draft until those native checks pass.
