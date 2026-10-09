# Multi-character prototype validation

Branch: `feat/multi-character-prototype`, stacked on open PR #9. Scope: checkpoint 1 source implementation and Windows hosting experiment. Native feasibility gates are not passed yet.

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

Checkpoint 1 needs native feasibility evidence. Checkpoints 2–6 remain: proven warm/economy resource policies, source-bound shared chat/events and microphone ownership, production one-interface switching, complete workspace/lifecycle integration, and the optional genuinely low-resolution 1 FPS monitor. Keep the hosting mechanism decision and economy performance claims gated on native measurements.
