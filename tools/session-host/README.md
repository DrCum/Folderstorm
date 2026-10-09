# Character sessions feasibility prototype (Windows)

This is the first checkpoint of [the multi-character roadmap](../../doc/plans/multi-character-sessions.md), in [PR #10](https://github.com/DrCum/Folderstorm/pull/10) against main after PR #9 merged. It launches two isolated viewer workers and tests reversible warm standby plus optional native window hosting. It is not the completed shared-chat/economy/monitor interface.

Normal viewer startup never launches this tool or extra accounts. The Windows viewer target builds `folderstorm-session-host.exe` beside its matching viewer and the Windows manifest copies it into the installation. Launch that executable explicitly when testing. The manifest supplies a bounded installation-local filename for the renamed packaged viewer; build-directory launches fall back to the filename compiled by CMake. The controller never searches PATH for a viewer.

Checkpoint 2 adds a background-mode dropdown beside each character: **Warm** retains initialized buffers; **Economy (experimental)** releases disposable screen/shadow render targets on the GL display path. Both keep normal session/teleport/texture maintenance, with no saved graphics downgrade. Switching back or detaching rebuilds the targets through the normal allocator. The status reports when targets are released; working-set RAM is not a VRAM measurement. Compare GPU memory and switch recovery on Windows before assuming a useful saving. Modes are controller-session choices; normal launches remain unaffected.

## Try the prototype

1. Open `folderstorm-session-host.exe` from the new Windows build/installation. Leave **Host active viewer** unchecked initially.
2. Choose **Launch 1**, then log in through the ordinary viewer login screen. The first ready character becomes active automatically.
3. Choose **Launch 2** and log in a different character. Its login/loading UI remains visible until it is ready, then it enters warm standby.
4. Use the character dropdown. Switching releases old input, hides its view and prepares the target. The target grants input after two normal buffer swaps; its account/session must still match the request.
5. Send an IM to the background character, switch to it and check the message arrived. Test in the same region first, then different regions and a teleport that completes while backgrounded.
6. After separate-window switching works, enable **Host active viewer**. Its borderless native window follows the controller's viewport bounds, while remaining an unowned top-level window in its own worker process. Its own menus, toolbars, floaters and skin remain intact. Click a chat field and test typing, switch characters, then Alt-Tab away and return. The active status reports **keyboard focused/unfocused** from the viewer's normal focus callbacks; polling can lag briefly. Disable the checkbox to return to separate windows while retaining management/standby.
7. **Separate windows** detaches both workers from management, returns their ordinary windows and leaves them logged in. Closing the controller asks to do the same; it waits for detachment and never deliberately destroys a live hosted viewer window. Close a detached viewer before relaunching its slot.

An initialized worker keeps its render context. Warm standby uses the existing viewer hidden-window path for teleport progress, texture/material cleanup, pick cancellation and other maintenance, while suppressing world drawing, reflection/snapshot side work and foreground input. Normal networking, event/callback processing and Inventory observers continue. This deliberately reuses the native minimized/hidden-viewer behavior instead of treating `HeadlessClient` as a reversible runtime mode.

The local assistant/MCP listener remains unavailable while managed, regardless of its ordinary Preferences checkbox. **Voice follows active character** is optional and Off by default; if enabled, only the acknowledged active input owner can enable its ordinary voice preference or microphone. Old PTT/tuning state is revoked before promotion; background voice is disabled. **Mute background world/UI/media sound** is On by default and changes runtime gains only. Their saved choices remain intact. Temporary launch-policy overlays are restored on detachment, preserve deliberate saved changes, and are kept out of Preferences Cancel's saved layer. The shared chat section lets you choose the sending character independently of the world view, then Nearby chat or an existing native IM/group/conference. Open a new conversation through that character's native viewer; it appears in the shared dropdown on the catalog refresh. Drafts belong to their character/conversation and world switching leaves them there. **Send** accepts up to 1023 UTF-8 bytes, through native public-channel chat or that initialized native IM session. No gestures/chat commands are interpreted by the shell. **Mark read** clears native IM unread state; nearby/notification badges are shell-only. **Review in viewer** switches to the owning character so native dialogs can be handled there. Offers/payments/permissions are never automatically accepted. Shared messages are a bounded, live, in-memory window rather than a combined transcript file; earlier/native history stays in that character's native chat. Reconnect clears old session data, and history gaps are explicitly reported. Uncertain sends are never automatically retried. Incoming dialogs may require switching to their owning character. An open Preferences transaction or native modal dialog prevents switching; apply/cancel and close it in that viewer first. Switching never silently accepts Preferences. The **Chat panel** checkbox collapses the shared panel and gives its space to the world view; drafts, incoming chat and badges remain available.

## Separate data and ownership

The two test profiles live below:

```text
%LOCALAPPDATA%\FolderstormSessions\Prototype-v1\Character1\
%LOCALAPPDATA%\FolderstormSessions\Prototype-v1\Character2\
```

Each has separate `Roaming` and `Local` roots, used by the worker's native directory initialization. First launch starts with a fresh profile; your normal viewer settings, chat logs, workspaces, boards and saved credentials are not copied or migrated. Choose each worker's skin/settings normally. Profiles persist for subsequent launches, and normal per-account/grid subdirectories continue to separate different logins within a slot. Do not manually configure both profiles to use the same writable cache path.

An exclusive file handle is inherited by the worker to reserve its profile. The lease lasts through native viewer cleanup/process exit, including after detachment or controller closure. Another controller cannot reopen that slot while its worker is still running. The controller does not adopt unrelated/existing viewers, silently autologin, or terminate processes.

Workers receive only their connected pipe and profile-lease handles through an explicit Windows inheritance list. The pipe has a current-user ACL and rejects remote clients; managed control opens no network listener. Bootstrap identity/handle variables are removed before ordinary child processes launch, and handles lose their inherit flag. Commands are fixed-size, versioned, account/session-generation bound and sequenced. Credentials are neither pipe payloads nor command-line arguments. Child environment formation preserves inherited runtime/proxy/trust variables without printing them or changing the controller's environment. Detachment reapplies ordinary voice/assistant choices; normal multiple-viewer port conflicts can still appear in assistant diagnostics.

Native credential IDs are reserved before authentication to catch duplicate managed logins. The null-safe native startup API supplies `first_last` for agent credentials (including `Resident`) or `account_name` for account credentials; normalization and grid qualification remain in place. These keys are not display names or passwords. This cannot discover arbitrary external viewer sessions or every possible server-side alias. Use two genuinely distinct accounts when testing. Account UUIDs in ready status remain qualified by grid; a disconnected reservation is released.

The host tracks worker process health, ordinary rendered swaps per second, main-loop iterations per second, keyboard-focus state and working-set MiB. These are diagnostics, not claimed CPU/GPU/VRAM savings. Compare with two ordinary viewers, including an ordinary minimized second viewer, because the viewer already reduces drawing when hidden/minimized. Measure aggregate processes using native tools.

Hosted focus requests use the viewer's existing native window-thread API, including on embedding, host reactivation and clicks in the viewer. Requests are bound to the current session/input owner and a foreground controller; the native thread rechecks the hosted root, enabled/visible state and foreground window before applying a queued request. Other applications and native dialogs keep their focus, and no keystrokes or application-focus flags are synthesized. Ordinary focus callbacks control normal foreground/background yielding. Native confirmation of this follow-up remains pending. Rebuild the controller and viewer together: the private protocol is now version 5, and mixed versions are deliberately rejected.

## Recovery and current native risks

A broken pipe or expired worker heartbeat returns that worker to an ordinary window and releases input/microphone state. Stale commands cannot apply to a new session. Failed target promotion invalidates its control channel before attempting to restore the previous worker. Before promoting another managed character, the controller waits for a lost peer to remove its managed-input lease from its PID-validated window; a frozen peer can keep this recovery waiting. This window property is only a liveness hint, not a command/authentication channel. A disconnected character releases managed foreground control, and repeated automatic promotion retries are avoided.

Window hosting remains opt-in pending Windows acceptance. This checkpoint replaces cross-process child/owner parenting with a fitted unowned top-level surface. A controller exit can no longer destroy the viewer via a foreign native parent/owner; its watchdog restores ordinary styles and placement. The worker aligns with the host viewport, hides on host minimize/other-app focus, preserves native owned dialogs, and retains hidden client geometry during standby so switching does not repeatedly reshape/clamp live floaters. Chat/selector focus in the foreground host keeps world rendering at ordinary scheduling without pretending the viewer has keyboard focus. Native focus is requested on the viewer's window thread with a foreground recheck; clicking the world restores its normal keyboard/IME behavior.

The compact character dropdown and optional Chat panel use DPI-scaled Windows shell controls; the hosted viewer keeps its own skin and UI scale. Fullscreen uses separate windows. Native z-order, minimize/restore, owned file/color dialogs, DPI/monitor changes, host forced-crash recovery and large UI-scale/skin behavior still need Windows tests. This source change removes the foreign-parent destruction path, but is not evidence that all platform behavior is validated.

Workspace lifecycle polish, the low-resolution 1 FPS monitor and optional bird’s-eye transition remain later checkpoints. Do not infer their performance or completion from this prototype.

## Focused checks

The portable protocol/handoff test needs no viewer dependencies:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -I indra/newview \
    tools/session-host/tests/test_protocol.cpp -o /tmp/folderstorm-session-protocol-test
/tmp/folderstorm-session-protocol-test
```

Actual checks and pending native acceptance are in [prototype validation](../../doc/plans/multi-character-validation.md). No viewer/build/packaging/GitHub builds or unrelated Go tests were run during implementation.
