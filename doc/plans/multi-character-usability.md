# Multi-character usability follow-up

Status: IMPLEMENTED_SOURCE_NATIVE_PENDING. The user subsequently authorized all six features in one new PR #27. They are implemented in separate reviewable commits on `feat/multi-character-usability`, based on PR #26. The source/model checks below passed; native Windows behavior is still pending the user's build. No feature is claimed to have passed native acceptance.

Planning baseline: PR #26, `fix/session-host-review`, head `127ea2eb872a3c03a27710eb8e9ae3b198b5f899`. The plan-only branch is `docs/multi-character-usability`. Implementation reverified main at `56bfe960fb25ec9951f3ca6e39724e56638259cd` and PR #26 open/unmerged at its recorded head. PR #27 is stacked on `fix/session-host-review`; no published history was rewritten.

Related plans: [sessions](multi-character-sessions.md), [host refinements](multi-character-host-refinements.md), and [validation](multi-character-validation.md). Managed-workspace mirror: `/workspace/plans/folderstorm-multi-character-usability.md`.

## Delivery order

| Checkpoint | Feature | Result for the user |
|---|---|---|
| 1 | Character profiles, colors and remembered shell positions | Recognize the sending character immediately and arrange the shell once. |
| 2 | Pinned shared-chat tabs | Keep frequent conversations within easy reach across characters. |
| 3 | Attention inbox | See which character needs attention and review it in the owning viewer. |
| 4 | Separate notification sounds | Hear selected IM alerts without bringing back background world/media sound. |
| 5 | Transition controls | Adjust the bird's-eye effect or choose an optional fade. |
| 6 | Character-switch shortcuts | Switch directly or cycle ready characters without affecting typing. |

Colors and aliases come first. Checkpoint 1 can use separate commits for identity styling and shell geometry; publish a testing checkpoint after identity styling if native feedback makes that useful. Later checkpoints reuse the same account identity and presentation model. The latest user instruction combines these six features into one follow-up PR #27; keep their implementation commits separate inside that PR. Earlier checkpoint PR heads remain unchanged.

## Shared foundations and boundaries

- Stable presentation profiles belong to an account UUID plus canonical grid identity, never merely to a slot number, alias, display name or character-list selection. Live actions additionally require the current worker ID, generation and existing process/session checks.
- Keep three identities distinct: the active world character, the character selected for management, and the character selected to send Chat. Changing the first or second must not silently change the third, move a draft or send a message.
- Reuse the existing five-character registry, shared `ChatView`, bounded event stream, restart/lifecycle controller, monitor model and handoff APIs. Presentations share these models rather than creating independent polling or sending controllers.
- Preserve one managed input/microphone owner, native restrictions, native workspace behavior and Preferences OK/Cancel. Host settings use an edit draft with Apply/Cancel, and explicit persistence through Save host choices. Opening or cancelling a dialog must not write configuration or alter worker preferences.
- Persistence contains presentation metadata only: aliases, RGB colors, panel rectangles, resolved pin references, alert choices, transition preferences and shortcut assignments. Never save passwords, tokens, chat transcripts/drafts, notification payloads, Inventory contents, filters or selections here. No automatic additional-character login, monitor capture or replay of an uncertain send.
- Current `HostOptions` uses a bounded 512-byte `FSH1` single-digit format. Do not squeeze strings or rectangles into that format. Add a separate versioned, bounded presentation store with atomic writes and transactional parsing; retain compatibility with existing host choices. Initial proposed limits: 64 KiB, 32 remembered account profiles, 24 pinned references total, and seven shell-window rectangles (host, Chat, character panel and four monitor positions).
- Validate UTF-8, string lengths, duplicate identifiers, numeric ranges, file versions and complete records before applying anything. Proposed alias limit: 64 UTF-8 bytes. Accept legitimate negative coordinates on multi-monitor desktops; fit rectangles to available work areas instead of rejecting them. Unknown/future incompatible data leaves current settings unchanged and reports a useful explanation.
- Persisted metadata is never sufficient authority for a live command. Bind it to an explicitly logged-in matching account; expired sessions show unavailable state. Reusing a slot for another account must not inherit that account's alias, color, pins or alert policy.
- Native worker skins stay intact. Style the Windows host and its own controls with readable identity accents; do not recolor viewer textures or assume native Win32 controls use the viewer's skin. High-contrast mode, focus indicators and readable text take priority over a custom color.

## 1. Character profiles and identity colors

### User experience

Offer a small Character appearance dialog for an explicitly logged-in character: friendly alias, account color, Reset, Apply and Cancel. Choose distinct default colors for accounts without a customization; save an assigned color by stable account/grid identity so it survives a restart or a different slot assignment. Keep the actual account name available beside the alias or in its tooltip.

Apply the same identity accent to the character selector, status row, monitor header/footer, pinned tabs and attention entries. Shared Chat gets the strongest cue: a colored strip or border around the composer plus a clearly readable **Send as: alias (account)** label. Keep text and the editor background readable; avoid filling the entire history with a saturated account color. Names remain visible so color is never the only cue.

Docked and detached Chat use the same appearance and source binding. Selecting a different chat account saves the old account/conversation draft and restores the new one before updating the composer color. Switching the active world alone leaves the chat sender unchanged. Monitor exchange changes the displayed account name/color only after a successful handoff and rebind; window position can remain unchanged.

Remember host, detached Chat, detached character controls and monitor-window positions through explicit Save host choices. Restore panel placement on opening the same presentation, fitted to current monitor work areas and DPI. Persist monitor window geometry without reopening a previous account's capture automatically. Keep this separate from account-local workspaces, which already own native viewer floater geometry.

### Implementation

Add a bounded account-profile model and one appearance lookup used by all host presentations. Keep live source keys separate from saved profile keys. Reuse existing control layout/DPI and panel-dialog routing; use owned drawing resources with explicit cleanup and avoid recoloring unrelated controls. Add shell-geometry helpers using current work areas/DPI and existing fitting conventions, including monitor removal and minimized/maximized placement.

### Focused acceptance

Check profile parsing, alias/color reset, cancelled edits, cross-grid separation, slot reuse and fitted geometry. Native testing: assign clearly different colors to two accounts; switch world and chat independently while drafts exist; dock/undock Chat; exchange a monitor; restart into another account; move between DPI scales or remove a display. Verify readability with light/dark viewer skins and Windows high contrast. No send may change owner because a color or alias changed.

## 2. Pinned shared-chat tabs

### User experience

Pin/unpin the selected Nearby or IM conversation. A compact tab strip shows the conversation, owning character's alias/color and unread badge. Allow reordering and provide an overflow menu when space is limited; the existing conversation dropdown remains available. Selecting a tab changes both chat account and conversation deliberately, preserving each conversation's draft. It need not switch the active world character.

### Implementation

Use the current account/grid/worker/generation-qualified `ChatView` and conversation IDs for session-local pins. Start with a strict cap of 24 pins, at most eight for one account, within the existing conversation limits. Deduplicate pins by source and conversation; selecting or drawing a tab does not mark a conversation read.

Persist a pin only when its durable destination can be established from native data: Nearby, a peer UUID or a group UUID with its owning account/grid. Do not persist a transient IM session UUID as if it survives relogin. Ad-hoc conferences remain session-only unless native durable resolution is proven. Reconnect saved pins only to compatible existing native conversations; unresolved pins show unavailable state rather than starting conversations or replaying text. Explicitly opening a destination uses the native restriction-checked path.

Keep displayed titles restriction-aware and purge inaccessible live content when sharing restrictions change. Session end disables the pin's live actions; a replacement login cannot inherit its old source key or draft.

### Focused acceptance

Check deduplication, order, caps/overflow, draft ownership, unread behavior, unavailable/stale pins and durable-reference resolution. Native testing: pin chats owned by different accounts, type separate drafts, switch tabs and world character, dock/undock, restart one character and toggle restrictions. Confirm every Send still uses the visibly selected account.

## 3. Attention inbox

### User experience

Provide an optional compact list of characters needing attention: unread conversations and supported native offers/requests. Each entry shows the character's alias/color, a permitted description and a **Review in viewer** action. A chat entry can also select its existing shared conversation. Keep the panel usable when the world or shared Chat is collapsed.

Review activates the owning viewer and its native conversation/notification UI. It never accepts a teleport, permission request, payment or Inventory offer. Dismissing an inbox reminder does not decline an offer or change native read state. Label expired, disconnected and restricted entries honestly.

### Implementation

Current worker notices are generic, session-local attention signals with a boolean badge; they do not expose an offer ID, category or expiry. Do not infer actionable requests or counts from their text. First audit native notification templates, IDs and add/change/remove callbacks. Add an explicit allowlist for supported categories and a bounded structured attention event only where the native model provides a reliable identifier/lifecycle.

Bind each entry and Review request to its worker/account/grid/generation and native notification or conversation ID. Revalidate the object and restrictions in the worker immediately before opening it. Propagate expiration/removal; restriction changes flush/redact sensitive entries without resetting monotonic event IDs. Unknown templates retain a generic review reminder.

Keep notification attention separate from native absolute IM unread counts and the shell-local Nearby unread counter. Aggregate unread summaries from current models rather than summing every delivered event. Proposed queue cap: 128 attention entries; duplicate updates replace the same source/object entry. A retention gap tells the user to check native notifications rather than implying completeness. No notification contents are persisted.

Protocol changes require a version bump and matching host/viewer, with the stack's existing strict mixed-version rejection. Review-only actions must not share a transport path that accepts native offers.

### Focused acceptance

Check source/object deduplication, expiry, retention gaps, unread aggregation, dismiss versus native acknowledgement, restricted content and stale Review refusal. Native testing: IMs plus supported teleport/permission offers for inactive accounts; expiry before Review; disconnect/restart; restriction changes during switching. Verify Review opens the correct character and still requires its native response.

## 4. Separate notification sound controls

### User experience

Allow per-character notification alerts while background world, UI and media sound remain muted. Offer Off, IM alerts, and supported attention alerts with a small global volume control. New alert playback defaults Off; existing audio/voice choices retain their behavior. Muted contacts and native quiet/Do Not Disturb choices must remain respected.

### Implementation

Audit native IM/notification sound dispatch before changing it: the current background policy masks world/UI/media gain together. Simply restoring UI gain would also restore unwanted sounds. Prefer a bounded host-local notification tone driven by accepted eligible events, unless a native alert-only path cleanly preserves all policies.

Choose one alert producer for each event so active native alerts and host alerts do not double-play. Carry minimal eligibility metadata from the native worker if needed to honor mute/quiet rules; if eligibility cannot be established, skip the alert. Trigger only after event deduplication; catalog refreshes, historical lines, own echoes, repeated polls and reconnects are silent. Apply a short global/per-account cooldown and coalesce bursts without hiding unread counts.

Keep audio policy distinct from microphone/voice ownership and background render mode. No new voice enablement, PTT transfer or simulator work is required for an alert. UI previews are explicit and do not count as notification events. Per-account settings use the profile model and Apply/Cancel semantics.

### Focused acceptance

Check eligibility, deduplication, cooldown/coalescing, own echoes, persisted choices and cancelled edits. Native testing: mute background media/world, receive an eligible IM, verify one alert; test quiet/muted contacts and active-account native alerts; repeat in Warm/Economy. Live voice acceptance remains separately pending while the user cannot test voice.

## 5. Transition controls

### User experience

Expose transition style: Instant, Bird's-eye and Fade. Add a simple duration control and bird's-eye height control with conservative defaults and Reset. Instant remains the default for new profiles; map the existing saved cinematic preference to its current bird's-eye behavior. Explain that Escape skips the visual effect and completes the normal switch, matching the current implementation.

### Implementation

Replace the fixed `TransitionClock::Duration` of 1100 ms with a validated finite duration captured at transition start. Proposed duration range: 250–2000 ms per leg; fit host deadlines to the maximum bounded two-leg effect. Define height as a bounded camera offset above the saved view, clamped by native camera limits. Confirm a practical range in native visual testing before fixing the final UI numbers.

Extend the existing renderer-only camera path; never move the avatar, teleport it or change ownership timing to make an animation work. Snapshot exact camera state and restore it on completion, interruption, detach, disconnect, region change and restrictions. Setting edits during a transition apply to the next switch. Source/generation and exact-request cancellation guards remain unchanged.

Implement Fade inside the world presentation path so it works with hosting On and Off. Fade only the world viewport, leaving native dialogs, chat and host controls usable. Do not simulate it by screenshots of desktop windows, a full-desktop overlay or extra full-rate rendering of inactive characters. If the render mode cannot support it, switch instantly with a useful status. Reduced-motion or camera restrictions use the permitted instant fallback.

### Focused acceptance

Check timing bounds, captured versus edited settings, eased progress, timeout arithmetic, source cancellation and camera restoration. Native testing: each style with hosting On/Off, minimum/maximum duration, Escape, region changes, restriction changes and interrupted handoffs. Avatar position and ordinary camera state must remain unchanged after the visual effect.

## 6. Character-switch keyboard shortcuts

### User experience

Optional configurable shortcuts switch directly to one of the five character positions, or cycle forward/backward through ready characters. Show current assignments in the character menu. Shortcuts default disabled and do not launch an unavailable account. A saved assignment must be explicit about whether it refers to a slot or a named account; the first implementation uses the visible slot numbers with account names beside them.

### Implementation

Route host/detached-panel shortcuts through one command dispatcher and the existing handoff controller. Add native viewer integration through its existing keybinding system so it also works while the world has focus; do not use global OS keyboard hooks, synthetic keystrokes or process-focus tricks. Viewer-originated switch intent needs a bounded authenticated, source-bound message to the host, with freshness/deduplication and current ownership checks. The worker requests a switch; it never promotes another worker itself.

Suppress switching while a text editor, IME composition, modal dialog, open dropdown/menu or another consuming control is active. Use existing viewer typing/focus rules and corresponding Win32 host checks. Reject conflicting assignments instead of overriding established controls. Account/chat composer selection is unaffected by world-switch shortcuts.

Cycle only connected, ready, eligible slots in a deterministic order. Unavailable direct targets provide a short status and leave ownership unchanged. During an in-flight handoff, drop/coalesce intents according to one documented bounded policy; never queue an unbounded chain or replay an intent after session replacement. Escape retains its existing transition behavior.

### Focused acceptance

Check binding conflicts, disabled defaults, typing/IME suppression, cycle order, unavailable targets, source freshness and repeated input during handoff. Native testing: hosted and standalone viewers, detached Chat/character panels, login/Preferences text fields, composed drafts, IME, dropdowns, disconnected slots and three/five-account switching. Existing camera, movement and chat keybindings must keep their behavior.

## Review and validation policy

The latest user instruction requests all six features together in PR #27, with separate reviewable commits. Preserve earlier buildable PR heads and reverify branch/merge state before publishing. Newly reported PR #26 regressions take priority if they arrive. Do not automatically merge.

Run only affected portable/model tests and focused source/UI checks; keep assertions enabled in Release test configurations. Document what actually ran. No viewer builds, host Windows builds, packaging, GitHub builds, unrelated Go tests or subagents in the managed workspace. Native Windows behavior, accessibility, skins, DPI, sound and voice must not be described as validated by portable tests.

Planning verification inspected PR #26's shared-chat source binding, notification adapter, event model, host settings format and transition clock. Implementation completion and actual checks follow. Scheduled resume checks should stay quiet once the follow-up is published unless a new regression or unfinished authorized task appears.


## Completed implementation and usage

- [x] **Character profiles and colors:** Session → Character appearance and alerts edits the selected management character's alias, exact RGB accent and background-alert policy through Apply/Cancel. Account/grid identity owns the profile. Distinct available default colors are assigned for newly observed accounts; existing/custom colors stay stable. Selectors, status rows, monitors, pinned tiles, attention rows and the Send as stripe share its color. Names remain visible, and high contrast uses system accents. Save host choices persists metadata and host/Chat/character-panel/four-monitor rectangles; placement is fitted to current work areas/DPI on first opening, without automatically opening a monitor or logging in an account.
- [x] **Pinned shared Chat:** Pins… pins/unpins the selected conversation, lists overflow, and provides removal/reordering for every pin, including unavailable ones. The first six pins have compact color-marked tiles and full tooltips. Switching a pin restores that account/conversation's draft without changing world ownership. Limits: 24 total, eight per account. Save host choices persists Nearby and confirmed peer/group references only; conferences remain session-only. Restored peer/group pins wait for a matching existing native conversation and never initiate a chat or replay text. Restricted conversations are unavailable and retained lines are removed from the shared view.
- [x] **Attention inbox:** Session → Attention inbox opens an independent keyboard-navigable host panel containing unread conversations and allowlisted teleport, permission and Inventory offers. Structured add/change/remove events identify native objects without copying their sensitive payloads. Review switches to the owning ready account and then opens native Chat or notification UI; it cannot accept an offer. Expired requests disappear or return a clear native refusal if clicked during a race. Dismiss hides a shell reminder without changing native read/offer state; a newer unread event returns the reminder. Source changes, restrictions, gaps, disconnects and failed handoffs clear or invalidate relevant actions. Limits: 128 reminders; gaps explicitly direct the user to native notifications.
- [x] **Separate alert sounds:** Character appearance enables Off/IM/IM-plus-supported-attention policies per account; Session → Notification alert volume sets 0–100. All policies default Off. A short host-owned PCM tone plays only for fresh deduplicated eligible incoming events from background accounts whose world/UI/media audio is masked. Native quiet/mute and contact restrictions remain respected; native IM sound preferences also gate IM eligibility. Active native alerts remain the only active-account producer. Native eligibility that cannot be established produces no tone. Global/per-account cooldowns coalesce bursts, and counts still increase normally. Microphone/voice ownership is unchanged.
- [x] **Transition controls:** Session → Transition style, duration and height offers Instant, Bird's-eye and Fade, 250–2000 ms per leg and 16–96 m above-avatar bird's-eye height, fitted to the existing far-distance/camera bounds. Existing saved cinematic choices migrate to Bird's-eye; new choices default Instant. Settings are captured at switch start; editing them affects the next switch. Fade draws over the actual world viewport before native 2D controls, with hosting On or Off. Camera/reduced-motion restrictions fall back to instant switching. Escape skips the effect and completes the normal handoff. No avatar movement or simulator camera changes are introduced.
- [x] **Character-switch shortcuts:** Session → Character shortcuts enables/configures host chords (initial suggestions Alt+Shift+1–5 and Right/Left); they are Off by default. The Switch character submenu shows their current assignments. While the native viewer has focus, assign the seven Character actions through Preferences → Controls and its existing conflict handler; these bindings intentionally have no imposed defaults and share the host's enable policy. Direct switching and ready-only cycling use the existing handoff controller. Editors, IME composition, open menus, modal/Preferences focus, repeats, stale/wrong-owner intents and in-flight handoffs cannot trigger replayed switches. Chat sender/draft selection does not follow world shortcuts automatically.

Persistence uses the separate versioned `host-presentation.dat` store (64 KiB, 32 profiles, 24 durable references, seven rectangles), atomic replacement and transactional validation. Existing `FSH1` host options remain compatible. No credentials, transcripts/drafts, notification payloads or Inventory contents are written. The private protocol is **10**: build/install the matching host and viewer together; mixed checkpoint binaries are rejected.

### Actual focused checks

All **14** portable session checks compiled and passed with C++17, `-Wall -Wextra -Werror -pedantic -DNDEBUG -UNDEBUG`: protocol, chat, contact policy, options, preview, transition, restart, registry, monitors, controller, presentation, usability, alerts and shortcuts. Four new tests cover persistence/fitting, pins/attention/privacy/drafts, alert eligibility/deduplication/freshness and switch-intent/cycling contracts. Transition tests now cover configurable duration limits. All are registered in the existing focused regression project with Release assertions enabled; CMake is unavailable here, so CMake/CTest integration was inspected rather than executed.

XML parsing and source checks verified the seven new Controls rows against registered keybinding functions, matching protocol/header adapters, preview exclusion, original Quit/restart bounds, account/session guards, Win32 dialog/notification API declarations, drawing before native UI, panel ownership/DPI/navigation, resource cleanup and required Windows link libraries. `git diff --check` passed. No viewer, host Windows, packaging, installer or GitHub builds, unrelated Go tests or subagents were run.

### Remaining native Windows checks

1. Build the matching PR #27 host/viewer and installer with MSVC; verify no local patches are needed. Launch `folderstorm-session-host.exe` from that package.
2. Assign different colors/aliases, cancel then apply edits, switch world/chat independently with two drafts, dock/undock, exchange monitors and relog/reuse a slot. Verify the displayed sender and actual sending account agree. Test high contrast plus light/dark viewer skins.
3. Save/open shell positions across DPI scales and monitor removal. Check menus, tiles, tooltips, color picker, attention list, resizing, Tab/Shift+Tab and IME for clipping/focus regressions.
4. Pin Nearby/private/group/conference chats, reorder/remove available and unavailable pins, save/relaunch, open the native conversation and verify durable resolution. Confirm no automatic chat, draft transfer or send replay occurs.
5. Receive offers and unread messages on inactive accounts; review the correct native object, expire/dismiss/mark read, receive another message, apply restrictions, restart or interrupt a switch. Confirm no offer is accepted by the host and stale UI cannot act on another session.
6. Enable a background alert, leave world/media muted, verify one short tone and normal badges. Test own echoes, bursts, native quiet/mute/contact settings, each policy/volume and Warm/Economy. Live voice remains separately untested because it is unavailable to the user.
7. Test all transition styles with hosting On/Off, duration/height bounds, UI scale, Escape, region/restriction changes and failed handoffs. Verify world-only fade, native UI usability, unchanged avatar position and camera restoration.
8. Enable host shortcuts and assign native Controls actions; test direct/cycle commands with three/five characters, unavailable slots, editors/IME/menus/Preferences and repeated keys during a handoff. Confirm existing controls and source-bound drafts are unaffected.
