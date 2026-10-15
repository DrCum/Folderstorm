# Character sessions (experimental, Windows)

The current stack supports **five isolated character workers**, one active world/input owner, source-bound shared Chat, Warm/Economy background policies, optional fitted hosting, account-local workspaces, safe restart, four low-resolution inactive monitors and optional bird’s-eye transitions. Host controls can be Expanded, Condensed or recoverable Minimal; shared Chat and character controls can pop out independently. Native Windows acceptance remains pending for the latest refinements and additional-character count. Earlier PR/build heads are preserved for comparison. See [host refinement plan](../../doc/plans/multi-character-host-refinements.md) and [actual checks/native acceptance](../../doc/plans/multi-character-validation.md).

Normal viewer startup never launches this tool or extra accounts. The Windows viewer target builds `folderstorm-session-host.exe` beside its matching viewer and the Windows manifest copies it into the installation. Launch that executable explicitly when testing. The manifest supplies a bounded installation-local filename for the renamed packaged viewer; build-directory launches fall back to the filename compiled by CMake. The controller never searches PATH for a viewer.

Checkpoint 2 adds a background-mode dropdown beside each character: **Warm** retains initialized buffers; **Economy (experimental)** releases disposable screen/shadow render targets on the GL display path. Both keep normal session/teleport/texture maintenance, with no saved graphics downgrade. Switching back or detaching rebuilds the targets through the normal allocator. The status reports when targets are released; working-set RAM is not a VRAM measurement. Compare GPU memory and switch recovery on Windows before assuming a useful saving. Modes are controller-session choices; normal launches remain unaffected.

## Try the prototype

1. Open `folderstorm-session-host.exe` from the new Windows build/installation. Leave **Host active viewer** unchecked initially.
2. Choose **+ Character**, then log in through the ordinary viewer login screen. The first ready character becomes active automatically.
3. Choose **+ Character** again and log in a different character. Repeat explicitly for up to five slots; Open login also offers individual slot logins. Its login/loading UI remains visible until it is ready, then it enters warm standby.
4. Use the character dropdown. Switching releases old input, hides its view and prepares the target. The target grants input after two normal buffer swaps; its account/session must still match the request.
5. Send an IM to the background character, switch to it and check the message arrived. Test in the same region first, then different regions and a teleport that completes while backgrounded.
6. After separate-window switching works, enable **Host active viewer**. Its borderless native window follows the controller's viewport bounds, while remaining an unowned top-level window in its own worker process. Its own menus, toolbars, floaters and skin remain intact. Click a chat field and test typing, switch characters, then Alt-Tab away and return. The active status reports **keyboard focused/unfocused** from the viewer's normal focus callbacks; polling can lag briefly. Disable the checkbox to return to separate windows while retaining management/standby.
7. **View → Show active viewer separately** reversibly unhosts while retaining management/standby. **Characters → Stop managing** detaches all workers and leaves them logged in. Closing the controller offers native logout confirmation or leaving separate windows; it waits for detachment and never deliberately destroys a live hosted viewer window. Close a detached viewer before relaunching its slot.

An initialized worker keeps its render context. Warm standby uses the existing viewer hidden-window path for teleport progress, texture/material cleanup, pick cancellation and other maintenance, while suppressing world drawing, reflection/snapshot side work and foreground input. Normal networking, event/callback processing and Inventory observers continue. This deliberately reuses the native minimized/hidden-viewer behavior instead of treating `HeadlessClient` as a reversible runtime mode.

Local Assistant/MCP is opt-in for each character under **Preferences → Character settings → Local Assistant**. Use its native permissions editor, copy token-free client configuration, and check the exact login connection there. Every managed tool call must name its `viewer_session`. Pending Ask requests appear independently in Attention; Review activates their owner for native Allow/Deny. **Pause all MCP access** temporarily revokes managed access without changing saved choices and also applies to newly launched characters; it ends when management ends. Camera changes, snapshots and workspace application require the active character. Native creation and appearance callbacks now retain durable assistant authority. Pause/revocation stops further steps; already submitted simulator changes may complete. Creation avoids editors and gesture activation. Read an operation receipt through assistant_operation_status; never retry an uncertain mutation. Matching host/viewer protocol **16** and the updated sidecar are required. **Voice follows active character** is optional and Off by default; if enabled, only the acknowledged active input owner can enable its ordinary voice preference or microphone. Old PTT/tuning state is revoked before promotion; background voice is disabled. **Mute background world/UI/media sound** is On by default and changes runtime gains only. Their saved choices remain intact. Temporary launch-policy overlays are restored on detachment, preserve deliberate saved changes, and are kept out of Preferences Cancel's saved layer. The shared chat section lets you choose the sending character independently of the world view, then Nearby chat or an existing native IM/group/conference. Open a new conversation through that character's native viewer; it appears in the shared dropdown on the catalog refresh. Drafts belong to their character/conversation and world switching leaves them there. **Send** accepts up to 1023 UTF-8 bytes, through native public-channel chat or that initialized native IM session. No gestures/chat commands are interpreted by the shell. **Mark read** clears native IM unread state; nearby/notification badges are shell-only. **Review in viewer** switches to the owning character so native dialogs can be handled there. Offers/payments/permissions are never automatically accepted. Shared messages are a bounded, live, in-memory window rather than a combined transcript file; earlier/native history stays in that character's native chat. Reconnect clears old session data, and history gaps are explicitly reported. Uncertain sends are never automatically retried. Incoming dialogs may require switching to their owning character. An open Preferences transaction or native modal dialog prevents switching; apply/cancel and close it in that viewer first. Switching never silently accepts Preferences. The **Chat panel** checkbox collapses the shared panel and gives its space to the world view; drafts, incoming chat and badges remain available.

## Separate data and ownership

Private profiles live below (the first two names remain unchanged):

```text
%LOCALAPPDATA%\FolderstormSessions\Prototype-v1\Character1\
%LOCALAPPDATA%\FolderstormSessions\Prototype-v1\Character2\
%LOCALAPPDATA%\FolderstormSessions\Prototype-v1\Character3\
%LOCALAPPDATA%\FolderstormSessions\Prototype-v1\Character4\
%LOCALAPPDATA%\FolderstormSessions\Prototype-v1\Character5\
```

Each has separate `Roaming` and `Local` roots, used by the worker's native directory initialization. The first open offers **Copy from main profile…**, **Keep this profile's settings**, or Cancel. A new profile starts with defaults if you keep its settings. Existing profiles are offered setup once after this feature is installed; keeping settings never resets them. Chat logs, workspaces, boards, caches and saved credentials are not migrated. Profiles persist for subsequent launches, and normal per-account/grid subdirectories continue to separate different logins within a slot. Do not manually configure both profiles to use the same writable cache path.

An exclusive file handle is inherited by the worker to reserve its profile. The lease lasts through native viewer cleanup/process exit, including after detachment or controller closure. Another controller cannot reopen that slot while its worker is still running. The controller does not adopt unrelated/existing viewers, silently autologin, or terminate processes.

Workers receive only their connected pipe, profile-lease and bounded preview mapping/mutex handles through an explicit Windows inheritance list. The pipe has a current-user ACL and rejects remote clients; managed control opens no network listener. Bootstrap identity/handle variables are removed before ordinary child processes launch, and handles lose their inherit flag. Commands are fixed-size, versioned, account/session-generation bound and sequenced. Credentials are neither pipe payloads nor command-line arguments. Child environment formation preserves inherited runtime/proxy/trust variables without printing them or changing the controller's environment. Detachment reapplies ordinary voice/assistant choices; normal multiple-viewer port conflicts can still appear in assistant diagnostics.

Native credential IDs are reserved before authentication to catch duplicate managed logins. The null-safe native startup API supplies `first_last` for agent credentials (including `Resident`) or `account_name` for account credentials; normalization and grid qualification remain in place. These keys are not display names or passwords. This cannot discover arbitrary external viewer sessions or every possible server-side alias. Use distinct accounts in each managed slot. Account UUIDs in ready status remain qualified by grid; a disconnected reservation is released.

The host tracks worker process health, ordinary rendered swaps per second, main-loop iterations per second, keyboard-focus state and working-set MiB. These are diagnostics, not claimed CPU/GPU/VRAM savings. Compare with two ordinary viewers, including an ordinary minimized second viewer, because the viewer already reduces drawing when hidden/minimized. Measure aggregate processes using native tools.

Hosted focus requests use the viewer's existing native window-thread API, only after an explicit hosted viewport click. Requests are bound to the current session/input owner and a foreground controller; the native thread rechecks the hosted root, enabled/visible state and foreground window before applying a queued request. Other applications and native dialogs keep their focus, and no keystrokes or application-focus flags are synthesized. Ordinary focus callbacks control normal foreground/background yielding. Native confirmation of this follow-up remains pending. Rebuild the controller and viewer together: the private protocol is now version 11, and mixed versions are deliberately rejected.

## Recovery and current native risks

A broken pipe or expired worker heartbeat returns that worker to an ordinary window and releases input/microphone state. Stale commands cannot apply to a new session. Failed target promotion invalidates its control channel before attempting to restore the previous worker. Before promoting another managed character, the controller waits for a lost peer to remove its managed-input lease from its PID-validated window; a frozen peer can keep this recovery waiting. This window property is only a liveness hint, not a command/authentication channel. A disconnected character releases managed foreground control, and repeated automatic promotion retries are avoided.

Window hosting remains opt-in pending Windows acceptance. This checkpoint replaces cross-process child/owner parenting with a fitted unowned top-level surface. A controller exit can no longer destroy the viewer via a foreign native parent/owner; its watchdog restores ordinary styles and placement. The worker aligns with the host viewport, hides on host minimize and stays visible as an ordinary background surface for other apps, preserves native owned dialogs, and retains hidden client geometry during standby so switching does not repeatedly reshape/clamp live floaters. Chat/selector focus in the foreground host keeps world rendering at ordinary scheduling without pretending the viewer has keyboard focus. Native focus is requested on the viewer's window thread with a foreground recheck; clicking the world restores its normal keyboard/IME behavior.

The compact character dropdown and optional Chat panel use DPI-scaled Windows shell controls; the hosted viewer keeps its own skin and UI scale. Fullscreen uses separate windows. Native z-order, minimize/restore, owned file/color dialogs, DPI/monitor changes, host forced-crash recovery and large UI-scale/skin behavior still need Windows tests. This source change removes the foreign-parent destruction path, but is not evidence that all platform behavior is validated.

All seven source checkpoints are implemented in the PR stack. Native hosting, transition appearance, resource savings and monitor cost remain unverified or unmeasured.

## Focused checks

The portable protocol/handoff test needs no viewer dependencies:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -I indra/newview \
    tools/session-host/tests/test_protocol.cpp -o /tmp/folderstorm-session-protocol-test
/tmp/folderstorm-session-protocol-test
```

Actual checks and pending native acceptance are in [prototype validation](../../doc/plans/multi-character-validation.md). No viewer/build/packaging/GitHub builds or unrelated Go tests were run during implementation.

## Host Preferences and automatic saving

The compact toolbar uses the selected Lucide icons. Character selection, Login, Chat, monitors and Preferences are left aligned; quick actions are centered; pop-out and Expanded/Condensed are right aligned, with density last. Chat has an icon instead of visible text; monitors has no extra arrow. Dividers appear only when groups are crowded. Narrow regions put additional main controls in More. Detached controls may use a second quick-action row. Attention is a removable default quick action, showing its count when compact. Click toggles its inbox; right-click reveals filters/navigation. Chat toggles its panel. Dock and Always on top buttons live on their panels. Character cards/selection retain concise lifecycle/recovery access.

**Layout & theme → Layout → Docked controls area** offers Automatic, Full window, halves and thirds. Automatic uses the right half for broad two-monitor spans and the middle third for three; small overlaps across a screen boundary stay full width. Explicit areas override detection. Placement changes host controls, without changing the viewer's world viewport.

**Controls → Quick actions → Button display** offers Icon and text, Text only and Icons only, retaining the existing saved modes. Labels collapse before ordered overflow. Stable per-panel tooltip entries keep hover descriptions available while status updates, panels move and themes change.

**Preferences** uses sidebar categories and top tabs. Character settings shares one profile selector across Profile, Sounds, Viewer settings and Session. Chats & notifications separates Conversations, Attention and Reading. Controls contains Quick actions (with a preview) and Keyboard shortcuts. Detailed explanations and numeric ranges use hover tooltips.

**Apply** validates and saves without closing; **OK** saves and closes. **Cancel** discards edits since the last accepted Apply. Accepted choices, quick toggles, docking, modes, pins and window positions save automatically. **Retry saving** appears under Advanced → Files & recovery when a save has failed. Warnings remain visible; closing offers retry, close without saving or Cancel. Preferences stays owned by the main host while panels move.

The bounded transaction and backup remain in `%LOCALAPPDATA%\FolderstormSessions\Prototype-v1`, independent of install path. Legacy choices migrate without removing profile folders. Updated experience/chat formats retain toolbar migration state and custom WAV display filenames; downgrading to an older host is not supported.

**Character settings → Viewer settings → Copy settings** explicitly captures its destination and offers save/discard/Cancel for current edits. Copy groups include movement/WASD/quick jump, camera/view, avatar/beams/LookAt, RLVa, account UI, appearance, bindings and toolbars. Known presets supply omitted defaults. Account UI/toolbars apply once after successful login, with backups and ownership guards. Copy reports remain private and bounded; credentials, contents, live restrictions and unrelated security policies remain excluded.

See [Lucide toolbar refinements](../../doc/plans/host-lucide-toolbar.md), [host UI refinements](../../doc/plans/host-ui-refinements.md) and [profile copying](../../doc/plans/host-preferences-and-profile-copy.md). The Lucide toolbar follow-up retains protocol **14** and does not change viewer behavior. Native Windows UI/build acceptance remains pending.

Viewer notices now have their own bounded conversation/history budget, so hidden notifications cannot consume IM slots. Managed inactive IM floaters no longer clear native unread merely because they retain internal focus. **Advanced → Diagnostics → Chat and alerts → Show report** provides the latest per-character/category alert result and live queue/count information without message text or saved diagnostics. Host alert eligibility and rate limits remain; the report distinguishes a skipped alert from a Windows playback request. See [chat delivery fixes and the focused Windows checklist](../../doc/plans/host-chat-delivery.md).

## Workspace and lifecycle controls

**Characters** and card action menus refer to the management target and name it in their actions. **Workspaces** opens that character's existing workspace switcher, offering an explicit switch first when inactive; its native save/load/HUD review and Preferences rules still apply. Status reports the account-local workspace name, a `*` when modified, and an open Preferences transaction. World switching preserves live camera/HUD/Inventory/board state and does not auto-apply a named workspace. Configure per-character startup restore through that worker's existing Preferences; native UI/Inventory/account/session readiness gates are reused.

**Close character** returns it to its ordinary window for the normal viewer logout confirmation. Cancelling that native confirmation leaves it logged in as a separate viewer; close it before relaunching the slot. Host close offers logout, leave connected in separate windows, or Cancel. Logout waits for native process exit; closing the host again can leave remaining native windows logged in. Failed/stale logout requests are not replayed on a new login.

Accepted host preferences save automatically, including background modes, voice/background-mute, hosting and Chat panel choices in a bounded transaction. It never saves credentials, chat text, Inventory contents or named workspace data. Defaults remain voice/hosting Off, background mute On. One controller reserves the private profile slots/settings; detached viewers continue holding their own profile lease until native cleanup finishes.

Managed workers always use their private default texture/object/sound cache directories, even when copied settings name a shared custom cache. Managed startup skips migration/purging of that external custom path. Saved custom choices stay intact for ordinary launches; detaching keeps the already-open private runtime cache. **Open selected profile folder** exposes the selected slot's files without adopting another running viewer.

## Optional read-only character monitor

Choose **View → Monitors → character N** after that inactive character logs in. The monitor is Off by default, displays only an inactive character and never forwards image clicks as viewer input. Its labelled footer switches to that character. Opening the active character is refused; choose an inactive account. Closing/minimizing the monitor, minimizing the controller, switching or ending that session pauses capture. A new login must be explicitly selected again.

Default target: **320 × 180 at 1 FPS**; optional caps 480 × 270 / 640 × 360 and target rates 0.5 / 2 / 5 FPS. Geometry fits the character's current world viewport without stretching. Rates limit preview rendering/readback, not simulator servicing. Capture uses a bounded offscreen PBR target, never a full-window render shrunk afterward. Unsupported target/graphics modes show unavailable status instead of that fallback. Economy releases disposable buffers after each capture; Warm rebuilds its retained buffers. Native post-effects and allocator work can still cost GPU/CPU time; no performance benefit is claimed without measurements.

Each worker retains one latest BGRA frame in its bounded inherited local mapping. A nonblocking mutex prevents partial frames; busy samples are dropped. Worker/account/grid/generation, dimensions and sequence are checked before display. UI/HUD and snapshot files/animations/sounds are excluded; native snapshot cleanup restores the live viewport/projection/debug options. The monitor shows frame age, stale/waiting/paused/unavailable state and never stores images. Save host choices retains only size/rate, never monitor enablement, an account or image.

## Optional bird’s-eye switching

Enable **Settings → Host settings → Switching → Bird’s-eye** (Off by default). A switch rises above the outgoing avatar for about 1.1 seconds, performs the acknowledged handoff, then descends to the incoming character's own live camera for about 1.1 seconds. The height is bounded by view range. This is a temporary render-camera pose; avatar movement, agent camera controls, saved camera/graphics choices and workspace modified state are untouched. Normal camera/frustum state is restored after every frame, including early display returns, before native snapshot/reflection/idle work.

Press **Escape**, or choose **Session… → Switch instantly**, to skip a pending visual leg and finish the switch. Cancellation targets the exact worker/account/grid/generation and pending mode-request sequence; it never retargets a later switch or grants input. Camera restrictions, mouselook, scripted/follow camera, flycam, a long-range/invalid camera, native modal dialogs, minimized/other-app focus and Windows reduced-motion settings skip the effect. Region/session/disconnect changes end old visual work. Rollback uses ordinary instant promotion. Input/microphone stay revoked during visual work, and incoming input still waits for normal frame readiness. Selecting shared chat does not animate or change the visible character.

Save host choices can retain the effect preference; older saved choices load it as Off. Native animation appearance/timing and platform recovery still need Windows testing. The shared-chat timeout notice also now keeps its uncertain-delivery/no-retry explanation visible.

## Shared-chat review fixes

Restriction changes clear retained worker events without restarting their IDs. A host that misses a brief restriction on/off response can still receive later messages; genuinely discarded unseen events produce a history gap, including when the retained queue is empty. Event IDs restart only with a new login generation.

Nearby unread is a shell-local count of accepted incoming live events. Duplicate event IDs, the character's own message echoes and catalog refreshes do not increase or reset it. The count continues for inactive characters and while Chat is collapsed, saturates rather than wrapping, and clears only through successful source-bound **Mark read** or normal session/privacy cleanup. There is no automatic clear merely for selecting the conversation. IM/group/conference badges continue to use native absolute unread counts. Notification attention is a boolean badge, cleared by Mark read; native offers are never accepted by this action.

Native compose, shared compose and typing use the same live FSData support-contact policy. A `NO_SUPPORT` sender cannot send or type into a direct support contact, while ordinary contacts and the existing group/conference behavior are retained. Shared sends return an explicit refusal and keep the original conversation draft. Other native readiness and RLV checks remain in force. Protocol layout/version stays at 8; build the matching host and viewer from the review-fix branch.


## Hosted control follow-up

Host selectors keep their choices stable while open; ordinary status polling no longer reconstructs the lists. Clicking host controls does not transfer keyboard focus into the world. Click the world for normal native input. The hosted surface stays visible behind other apps and hides when the host is minimized. **Separate windows** reversibly disables hosting and preserves management/background policies; **Characters → Stop managing** explicitly detaches the workers. Close Preferences/modal interactions before changing hosting. Native Windows z-order, dropdown/IME, hosting reversal and hosted animation checks remain pending.


## Restart a character

Choose its name, then **Session → Restart character**. Confirm the ordinary native close if requested. The host waits up to one minute for the old process/profile to release, then opens fresh native login in that slot; the other character stays connected. Native Cancel or **Cancel pending restart** stops relaunch. The latter does not cancel a native logout already accepted. For an already detached or lost-control viewer, close its ordinary window and use + after exit. Queued messages/actions are not replayed at the new login. Requires matching protocol 9 binaries; native recovery acceptance is pending.


## Monitor presentation

The monitor retains its last complete valid image between frames and presents each paint through a bounded back buffer. It no longer clears/redraws unchanged images at the host's 10 Hz polling rate. Source/restriction invalidation still clears the image immediately. Compare 1/5 FPS and Warm/Economy on Windows to verify the reported flicker is resolved; capture-path flicker is not ruled out by source checks.


## Compact host controls

Choose **Session → Normal / Condensed / Collapsed**. Condensed uses a single slim control strip; full character details remain in Session. Collapsed removes host chrome and docked Chat from the world area. Right-click the title bar (or open its system menu) to restore controls or open Character sessions; leaving collapsed restores the prior docked Chat preference. The mode saves automatically. Older settings default to normal. Native sizing/DPI/restore acceptance is pending.

### Detached shared panels
View offers **Pop out shared Chat** and **Pop out character controls**, independently of host chrome. Dock either from View. Closing Chat hides it and keeps its source-qualified draft; closing character controls docks them. The host system menu exposes Session even when chrome is collapsed. Panels reuse the existing controls/controller and can resize and move between DPI scales; they do not create another chat transport. Docking preferences save automatically. Minimizing the host hides its owned panels, and restoring it restores eligible panels.

### Three-character native checkpoint
The registry currently supports three independently launched logins. **+ Character** opens the next free slot; Session offers individual slot login/monitor actions. Character1/Character2 profiles and old host choices are preserved; Character3 uses its own profile/cache/lease and defaults to Warm. All ready characters can send source-bound shared chat, switch, restart and use account-local workspaces. One active input/voice owner remains. Periodic events/catalog/workspace/poll requests rotate per worker so a busy chat cannot starve other updates. No extra login starts automatically. This checkpoint retains one monitor; simultaneous monitors and the final five-character cap follow separately.

### Five characters and four independent monitors
The final capacity is five logins and four inactive-monitor windows. Each additional character has a private profile, caches and process lease; no login starts implicitly. Slots share the serial acknowledged handoff and source-bound lifecycle/chat/workspace rules. A successful switch to a monitored character (from its footer or the selector) exchanges that particular monitor to the previously active character; it clears old pixels and rebinds the exact account/grid/worker/generation before reading. Failure/rollback retains the assignment. If the old session ended, that monitor stops rather than adopting another login. Other monitor assignments stay unchanged.

Right-click a monitor for its own size/FPS target or close action. Targets are 0.5/1/2/5 FPS, caps 320×180/480×270/640×360. Visible monitors share **10 FPS aggregate**, rounded down to supported equal-share rates: three/four 5 FPS targets get 2 FPS each, shown as a budget limit in their titles. Lowering policies must acknowledge before a new/increasing lane can consume reserved capacity, including in-flight requests; lost but live workers conservatively retain their rate reservation. Closing/minimizing pauses only that monitor. Host minimize/transition pauses all. Enablement, assignments, imagery and per-monitor choices are session-only; Save host choices stores only defaults.

At the largest cap, five latest-frame mappings use about 4.4 MiB and four retained display frames about 3.5 MiB (excluding metadata, native GL and bounded paint buffers). Simulator state and capture/readback still cost resources; CPU/GPU/RAM/VRAM improvements require native measurements. The capture path remains genuinely offscreen at the small resolution, and image clicks never forward input.

### Review fixes: recovery and panel navigation
The world selector also selects the character for Session actions. An unavailable/disconnected/separate character stays selected for Restart/Cancel, even while another character owns the world; the actual active character is marked in the list. A successful handoff or rollback synchronizes the selection to its acknowledged owner. Polling does not replace an unavailable management target.

Disconnected native recovery UI may redraw without reacquiring managed simulator input/microphone ownership. Full Stop managing and host-close detachment cancel queued/pending relaunches; a native Quit already accepted can still finish, but cannot cause another viewer to launch afterward. Only a new explicit launch/restart resumes management. Detached Chat/character panels route Tab and Shift+Tab through their own control hierarchy, with normal dispatch for unrelated windows. Native UI validation remains pending.


## Character colors, pins and attention (PR #27)

Use **Session → Character appearance and alerts** for the selected character's friendly name, RGB accent and optional background alerts. The color follows the account across selectors, shared Chat and monitors; **Send as** remains tied to the selected chat account rather than the active world. Automatic saving keeps those metadata choices and fitted host/pop-out/monitor positions. Apply accepts and saves edits; Cancel discards them.

**Pins…** adds/removes/reorders chat pins, including unavailable entries. Six compact tiles show the first pins; the menu lists overflow and tooltips give the full character/conversation. Nearby and known peer/group destinations can be saved; conferences remain session-only and restored IM pins wait for an existing native conversation. Named profiles never contain messages or drafts.

**Session → Attention inbox** lists unread chats and supported offers/requests with account colors. Review activates the correct viewer and opens native chat/notification controls. It never accepts an offer or permission. Dismiss hides a reminder locally; it does not change native read or offer state. Expired/restricted/session-ended actions are refused. A retention gap means native notifications may contain additional items.

Optional alert sounds default Off per character. Enable them in Character appearance, adjust **Settings → Host settings → Audio → Alert volume**, and retain background world/media mute. Eligible fresh background events produce one short local tone; active accounts use their existing native alerts. Native quiet/mute/contact and IM sound choices are respected. Voice ownership is unaffected.

**Settings → Host settings → Switching** selects Instant, Bird's-eye or a world-only Fade. Defaults are Instant, 1100 ms per leg and 64 m (camera-fitted); duration is 250–2000 ms and height 16–96 m. Escape skips animation while completing switching. Reduced-motion/camera restrictions use instant fallback.

**Settings → Keyboard shortcuts** enables optional host chords; the character submenu displays their assignments. Host shortcuts work in the shell/pop-outs, outside editors, IME and menus. For the focused native world, assign the seven Character actions in **Preferences → Controls** using its normal conflict checking; no viewer defaults are imposed. Both use the host's enable policy, skip unavailable cycle targets and drop keys during a handoff. World switching preserves Chat's selected sender and drafts.

The current matching private protocol is **12**; do not mix the host/viewer from earlier checkpoints. New presentation metadata is a bounded atomic `host-presentation.dat` file beside existing host options, without credentials or conversation contents. See [the completed usability roadmap and native checklist](../../doc/plans/multi-character-usability.md).


### Optional Chat in each monitor

Right-click a monitor's image or footer and check **Show chat**. It is Off by default and remains a session-only choice for that monitor window. The compact panel includes a conversation dropdown, retained shared history, **Send as** with the account's accent color, a message field, Send and Read (Mark read). It supports Nearby and that character's existing private/group/conference chats. The image stays read-only and retains its existing low-resolution/FPS budget; sending does not activate the character or change microphone/audio ownership.

When a successful character switch exchanges a monitor, Chat follows the account now displayed and starts on its Nearby conversation. Drafts remain in their original account/conversation, shared with the main/pop-out Chat panel; they are never copied to the new account. Hiding/closing the panel keeps session-local drafts. Disconnects, ended/replaced sessions and restrictions clear or disable visible controls. Native refusal/uncertain-send behavior still preserves the original draft without retry. Read updates only the selected account/conversation's badge. Tab/Shift+Tab routes through the monitor controls independently of the main host.

Native Windows checks: send distinct Nearby/IM messages from two monitors while a third character stays active; verify actual senders, shared drafts, independent unread/Read behavior, stable dropdowns, failed send/timeout retention, swap/rollback/relogin safety, restriction cleanup, hide/reopen, resizing/DPI, IME and Tab navigation. The monitor's Show chat choice does not start monitors or logins automatically.


### Host icon and installer shortcuts

The host EXE and its main, detached-panel and monitor windows use the original winged Folderstorm mascot, embedded at seven Windows icon sizes with transparency. The installer adds **<viewer shortcut name> - Character Sessions** beside the regular viewer's Start menu entry and desktop shortcut. Start menu opt-out also skips this entry, and updater `/marker` installs follow the regular viewer's desktop-shortcut preservation rule. Links target the matching installed `folderstorm-session-host.exe`; uninstall removes them. No extra account is launched by installation or ordinary viewer startup.

### Copy from main profile

Save/close the normal viewer so its latest preferences are on disk. The first open of each profile offers setup. You can also select a closed character and use **Characters → Copy from main profile…** or **Settings → Copy from main profile: character…** later. The dialog starts at `%APPDATA%\Folderstorm_x64\user_settings` for 64-bit; its source selector can browse another installation or native settings backup. Check the groups to copy: graphics, interface/skin, colors, bindings, quick preferences, toolbar layout, movement, camera, avatar/LookAt, RLVa and reviewed account UI. A toolbar-only copy is supported. Choose the source account for toolbars if the source contains multiple accounts, then review the source/destination confirmation and open normal login. Copying never logs in automatically.

The first-open choice is recorded in the private profile only after its viewer launches successfully. Cancel, a cancelled picker or confirmation, and a failed launch leave setup available next time. The setup dialogs keep other workers serviced while blocking overlapping launches/switches; ending management or cancelling/replacing a session-set opening invalidates its pending launch. Dialogs belong to the active host panel so they remain accessible from popped-out/pinned controls. The existing profile lease prevents changes while another viewer owns that profile.

The worker reads a bounded preference file (4 MiB, at most 8192 records) before UI/login initialization, only when graphics or interface settings are checked. It accepts recognized, correctly typed, persistent native controls that permit backup and belong to explicit graphics/appearance families or listed UI controls. Credentials, account/grid identity, paths/caches/history, voice, local assistant permissions, browser access policies, automatic acceptance and permission/debit defaults remain separate. Checked `colors.xml`, `key_bindings.xml` and `quick_preferences.xml` are validated and copied independently of those preference groups. Inventory/chat contents, workspace/HUD references, boards, filters, credentials and caches are excluded. Ignored dialogs and warning choices stay in the destination profile. Copying never enables the managed local assistant.

Toolbar copying snapshots the chosen source account's `toolbars.xml` (up to 64 KiB) before login, then installs it only in the first successfully logged-in destination account, immediately before native toolbar loading. Button order, left/right/bottom placement, display mode, alignment and layout style are retained. Native toolbar parameters are validated; commands must be registered in the destination viewer, duplicates and oversized lists are refused, and unavailable commands are skipped and reported. An explicit successful copy overrides the destination's first-run toolbar reset. It is consumed once and discarded if management ends, so it cannot apply to a later account. An existing named startup workspace can still replace toolbars through its normal restoration rules.

Existing replaced global/UI files go into `user_settings\settings-import-backups\<timestamp>-<pid>` inside the selected private profile; toolbar originals go into the destination account folder's own `settings-import-backups` directory. Writes stage in a newly created backup directory, validate the native settings save, then replace the destination atomically; a failed global save restores the prior controls. Individual unavailable/invalid UI files are skipped and counted. Toolbar refusal keeps its original file. The host reports import results; the native log retains summaries. **Open selected profile folder** provides access to backups. To copy into an existing profile, close its viewer first and repeat the same confirmed action.

Windows acceptance pending: build matching host/viewer, check EXE/taskbar/panel icons, desktop/Start menu links with opt-out and updater paths, and uninstall cleanup. Copy from a non-ASCII path into an unused slot and an existing closed slot; compare skin/theme/graphics/bindings, then restart and verify persistence, source files unchanged and backups present. Confirm active profiles are refused, Cancel leaves preferences alone, invalid/oversized files report refusal, and saved main-viewer login/assistant/cache choices are not adopted.


### PR #27 restriction and reminder review fixes

Queued group messages recheck both group and sender receive permission immediately before host delivery. Blocked events become a redacted restriction gap with no text, names, destination, unread count or alert eligibility. The shared model purges old conversation lines, remembers restrictions even before the first catalog entry, and rejects subsequent chat without an authoritative unrestricted catalog response. Main/pop-out Chat hides restricted history and disables compose/read; monitor Chat retains its existing restriction guard. Drafts remain session-local and hidden rather than being reassigned or sent.

Dismissing an unread reminder updates the existing source-bound watermark each time. A later incoming message restores it, including when a native unread count is unchanged. Dismissal still affects only the shell reminder, not native unread state or offers.

The reported Windows scope/constness fixes are upstream. Profile destination paths now use the viewer's explicit UTF-8-to-wide conversion once and reuse that native path, avoiding C++20 `u8path` deprecation under `/WX`. Pin binding also avoids its reported shadowed local without weakening strict warnings.


### Host recovery checkpoint
Minimal chrome now reserves a Show controls strip outside the fitted viewer. Restore controls and Recover positions are available in the host/pop-out/monitor system menus and the notification-area icon (including Explorer restart registration). Display changes re-fit existing windows. Closing detached character controls in minimal mode reveals Condensed controls. Recovery does not change accounts, ownership, drafts or saved preferences; manual position recovery clears native topmost flags for the session. Focused portable recovery checks pass; native hosting/DPI/tray/close paths require Windows acceptance.


## Folderstorm appearance and session tools

The new default is a quiet Condensed strip. Characters, View, Settings, Open login and Attention replace the long Session menu; More exposes them in narrow windows. Expanded optionally shows reordered character cards. Settings supplies Midnight, Daylight or Follow Windows, Compact/Comfortable density, diagnostics and up to twelve ordered quick actions. High Contrast, per-window DPI and Windows text scale affect presentation. Apply accepts and saves a settings draft; Cancel discards it.

Pin each detached Chat, character-controls, Attention or monitor window independently using Pin or its system menu. Docking suspends that window's pin; recovery clears pins for this session. Monitor Exchange, Chat and Options remain explicit; Options can make current size/rate the saved defaults after Save host choices.

Characters → Session sets saves selected private profiles/background policies, shell arrangement and optional monitors. Open reviews the set and opens ordinary login windows without submitting credentials. Update confirms replacement and retains capture choices; Rename/Duplicate/Delete manage configuration only. Cancel opening, replacement Open and lifecycle loss revoke remaining follow-ups. Unrelated characters remain logged in, and a completion report identifies skips.

Attention → Next/Previous navigates filtered pending items without world activation, Mark read or offer acceptance. Native offers precede unread conversations by default; Nearby is an optional filter. Settings → Keyboard shortcuts supplies optional recovery and attention bindings in the host and the corresponding native Controls actions. Shared Chat sender/drafts remain source-bound.

The added versioned stores are bounded `host-experience.dat` and `session-sets.dat`, alongside existing host options/presentation. No credentials, transcripts, drafts or Inventory contents are saved in these stores. See the [completed-feature checklist, command map and focused Windows tests](../../doc/plans/session-host-experience-completion.md). The current protocol **15** requires the matching host and viewer. Native visual/focus/IME/DPI/topmost/voice acceptance remains pending.


### Chat behavior, reading and character sounds

Build the matching protocol **13** host and viewer for this follow-up. Managed text input uses physical key release and per-origin quarantine rather than a global held-key text veto. Missed releases quietly clear only stale logical-key state, preserving fresh physical aliases. Per-frame scans match all forward key mappings instead of the last message's origin. Posted input cannot cross a handoff ownership epoch; native four/five-character keyboard/IME acceptance is still required.

Main, detached and monitor Chat open at the latest message. Scroll back or select text to pause following; Latest resumes it. Enter sends from the focused composer, Shift+Enter adds a newline, and an IME-commit Enter does not send. Draft/account guards and uncertain sends remain unchanged. A pending send leaves the editor focused with temporary read-only backpressure.

Settings → Chats and notifications → Read behavior offers Mark displayed chats read (On initially) and Auto-read active character (Off initially, after 60 seconds, adjustable 5–3600). Displayed reading requires a visible, non-minimized painted tail view. Scrolling back, hiding or collapsing the surface pauses it. The optional active timer resets on ownership/session/focus/visibility changes and grants newer messages a fresh delay. Automatic acknowledgements cannot dismiss newer unseen arrivals and never accept offers or clear other characters' attention.

Characters → Sound and notifications, or Settings → Sounds: selected character, offers independent Direct IM, Group, Conference and supported-notification checkboxes plus Choose WAV, Preview and Reset. These control eligible **background-character** alerts; active characters retain native viewer notifications. Native DND, explicit master/UI mute and restrictions still apply; native friend/non-friend/group/conference sound switches are independent. Custom sounds support mono/stereo PCM WAV, 8/16-bit at 8–48 kHz, up to 10 seconds/4 MiB. Imported assets live in the host profile's sounds folder, use host alert volume, and fall back to the built-in tone if unavailable. Sounds load once when an account becomes Ready or its choice changes; playback uses the cache, capped at 20 MiB.

Apply accepts and saves changes; Cancel discards the dialog choices. The bounded host-preferences.dat transaction includes chat/sound choices and its prior-copy backup. Missing new settings inherit existing character alert policies and preserve the original host-presentation/host-experience formats. No credentials, transcripts, drafts or Inventory contents are stored. See [the completed roadmap and focused native checklist](../../doc/plans/session-chat-polish.md).


### Refined Chat, notifications, profile labels and custom-viewport menus

This checkpoint uses matching **protocol 13** binaries. Settings → Chats and notifications has Show in Chat, Attention, Read behavior and Characters pages. Direct IM, Group, Conference, Nearby and Viewer Notifications have separate visibility choices; Attention also lists teleport, permission and inventory offers. Sound settings remain independent. Chat → Show conversations, View → Show conversations and monitor Options provide quick visibility controls. Filters change presentation and eligible badge totals, without marking anything read or changing native subscriptions.

Shared Chat's Chat… menu contains Close conversation in host; monitor Chat has × with a tooltip. Nearby remains permanent. Closing preserves its session-local draft and hides it across surfaces. Only a newer incoming message reopens it when visible; catalog polling, queued older events, system updates and outgoing echoes do not. Real native session removal also hides it, while minimization/tab changes do not. Host Close never leaves a native group or ends a call.

Viewer notifications use individual IDs and descriptions and disappear when native notification removal/expiry is observed. Turn them Off in either visibility or Attention. Review and response stay native; host Read or Dismiss only affects the host's indication.

Characters → Profile name and character color… (or Name this profile… before login) keeps a private profile's label visible while offline. Labels seed from known names, stay with the unchanged CharacterN folder when cards move, and are stored automatically. Ready Chat still identifies its actual account. FSC3 host-chat.dat reads FSC2 and migrates existing combined Attention choices; labels/filters do not contain account credentials or conversation contents.

Background host alerts no longer require native IM sound switches. Their ownership is independent of background world/media muting and suppresses duplicate native immediate/deferred UI sounds while inactive. Active viewers keep native sounds. Host volume/categories, native master/UI mute and UI volume, DND, muted senders, restrictions and burst/freshness limits still apply. Monitor visibility does not suppress alerts.

Settings dialogs keep tabs/actions fixed and show scrollbars only for field overflow, with Midnight/Daylight/High Contrast rendering. Custom-viewport status pulldowns now anchor to actual icons and fit the full window. See [the complete checklist, actual focused checks and Windows acceptance matrix](../../doc/plans/session-refinements.md).

## Friends, New IM and monitor tools

New IM is available in shared and monitor Chat. Friends opens a right sidebar; its pop-out button opens an independent friends window. Filter friends or use Search names, select a resident, then Open IM. Sending remains explicit.

The monitor camera menu includes Open all inactive monitors, Tile as grid, Arrange as row and Cycling monitor. Cycling defaults to ten seconds and does not change the active world. Pause to use Chat, or change Advanced → Require pause to chat. Four fixed windows and the cycling window share the existing ten-FPS budget; duplicate views share a source stream.

This follow-up uses private protocol **15**. Build the host and viewer together. See [completion, limits and focused Windows acceptance](../../doc/plans/host-friends-and-monitor-tools.md).
