# Folderstorm host experience: recovery, presentation and session tools

Status: PLANNED, NOT IMPLEMENTED. Prepared 2026-10-10 for the next implementation sequence. This document is the complete scope, including choices made so implementation can proceed while the user is unavailable. It does not claim native Windows acceptance or change runtime code.

Baseline inspected: PR #27, `feat/multi-character-usability`, `bb9ec4a7e4d21e3e9679f1c850643fe9e1fc4714`, based on PR #26. Preserve published history and local build patches; recheck remote heads/merge state before starting implementation. Use an isolated follow-up from the latest applicable cumulative head. Do not assume main contains this stack. Related documents: [usability](multi-character-usability.md), [hosting](multi-character-host-refinements.md), [validation](multi-character-validation.md).

## Goals and delivery decisions

Make the host a quiet, cohesive Folderstorm interface that remains recoverable in every presentation. Include every proposed feature: dark/light theming; cards and compact character switching; stronger chat identity; quiet status with expandable diagnostics; monitor polish; named session sets; attention navigation; reorganized commands; per-window Always on top; configurable power-user controls. Repair collapsed-control recovery first.

Implement continuously in the checkpoint order below when coding begins. Do not stop for cosmetic choices or routine questions: use the defaults in this plan, expose the useful alternatives, and record deviations. Publish separate stacked draft PRs at useful native-testing checkpoints and attach them. Do not merge. Native acceptance can remain pending while independent work proceeds. Existing user edits, private profiles, archived builds and published heads must survive.

The user permits compilation for visual inspection if the environment already supports it. No native Windows/Wine preview toolchain was found in the initial environment inspection; do not require the user to install one or treat a Linux mockup as native evidence. Use inexpensive affected-path checks, no packaging/GitHub builds or unrelated Go tests. At completion request an explicitly authorized **gpt-6-astra, high** review of the cumulative work, fix actionable findings, and report the exact reviewed head and native gaps. This is a specific exception to the earlier no-subagent preference; do not add other implementation agents.

## Evidence and boundaries

- Five-character handling, monitor exchange, hosted focus and detached/redocked panels worked in the user's PR26 testing. PR27 was briefly tested with local compiler patches; its latest privacy/import/dismissal fixes still need native testing.
- `tools/session-host/main.cpp::sessionMenu()` currently mixes lifecycle, profile copying, voice, shortcuts, attention, transition controls, monitor launch/rate/size, chrome modes, telemetry and docking in one long popup.
- Collapsed `layout()` hides docked controls/chat and gives the viewer the host client area. Main-window system-menu commands `0xA100` through `0xA130` offer Normal/Condensed/Collapsed and Session. Those commands already exist; simply adding another copy is insufficient.
- Closing detached character controls docks them; with collapsed chrome the docked controls become hidden. Placement is restored only when creating a panel, so a later monitor removal can strand an existing panel. These are source-confirmed vulnerable paths. The exact cause of the user's lost UI remains unconfirmed without native reproduction.
- The hosted renderer is an independent native top-level window fitted by `FSSessionWorker::fitSurface()`. A child overlay placed inside its rectangle can be covered. Current fitting also treats topmost predecessors specially. Recovery and pinning must be designed around this architecture, not assume an embedded child renderer.
- Reuse five-character registry, lifecycle controller, handoff, bounded ChatView/AttentionBook/PinBook, MonitorSet and profile leases. Keep active world, management selection and chat sender distinct. A control's current slot number is never enough authorization for an action.
- Preserve worker/PID/generation/account/grid validation, bounded authenticated IPC, native restrictions, single input/microphone ownership, explicit normal login, uncertain-send handling and native Preferences OK/Cancel. New menus and cards dispatch existing guarded commands. Do not cache an executable action beyond its identity generation.
- Named host session sets contain configuration/references only. No credentials, transcripts, drafts, notification payloads, Inventory data, selections, filters or remembered simulator actions. Viewer workspaces and account-local data remain owned by the existing systems.

## Checkpoint 1 — Recover controls and misplaced panels

Ship this independently before the redesign so a native build can test the reported problem in isolation.

1. Add one idempotent `Restore host controls` action. Restore/minimize state to a visible usable host; switch to Condensed, show character switching and the primary menus, fit placement to an available monitor, and optionally bring existing panels back into reachable work areas. Preserve logged-in workers, current active character, drafts, host/separate mode and native floater state. Recovery never logs out or starts accounts.
2. Collapsed mode reserves a small host-owned reveal strip outside the fitted viewport, with a labelled/focusable **Show controls** affordance. Target 20–24 logical pixels high with a larger accessibility option. Account for it in actual fitted geometry, not an overlay the viewer can cover. This deliberately replaces literal title-bar-only mode with recoverable minimal chrome; update labels and migrate the existing value without losing settings. Do not auto-hide the only mouse recovery affordance.
3. Keep Restore host controls and Recover window positions in the host's system menu; add the same recovery entries to every detached panel's Window menu. Provide a notification-area icon with Show controls, Recover windows and explicit Exit host, re-registering after Explorer restarts. Do not add implicit minimize-to-tray or close-to-tray behavior. The tray is a backup, never the sole recovery path.
4. Add a configurable recovery shortcut, initially unassigned. The current host/viewer binding machinery supports exactly seven switch commands and gates dispatch on busy/ready/restriction state; extend its allowlisted command model rather than treating recovery as an eighth switch index. Give recovery and attention distinct stable command IDs, bounded persisted bindings, migration from existing seven-key records and conflict checks. Host-local recovery must work during handoff and with no logged-in/ready character. Command-specific eligibility must preserve typing/IME protections and existing world-switch guards. Viewer-originated requests retain authenticated worker/session/input checks; version the protocol if its command contract changes and require matching binaries. System menu and reveal control must work without shortcuts or a responsive viewer. Never install a global keyboard hook.
5. On display/DPI/work-area changes, re-fit open host/panel bounds and ensure an accessible title bar/reveal strip. Support negative monitor coordinates, minimized/maximized restoration and hot-unplug. A manual Recover windows command restores visible panels near the current host monitor, clears their topmost state for the current session and reports that reset. Do not silently save this as a new layout.
6. If detached character controls close while chrome is collapsed, reveal Condensed controls immediately. Closing Chat hides it and keeps its drafts; the always-reachable Chat button reopens it. Closing a monitor stops only its preview/chat presentation. Destroying a panel removes it from dialog/focus routing safely.
7. A saved collapsed/off-screen state must not recreate an inaccessible startup. Validate placement, ensure reveal strip/menus are available, and fall back to Condensed if recovery creation fails. Recoverability takes priority over exact old placement. Handle unavailable tray services without startup failure.

Focused checks: extract and use a small presentation/recovery state helper; cover collapsed → pop-out → close, hidden/minimized panels, unavailable monitor, and invalid saved positions. Source-check actual WM_COMMAND/WM_SYSCOMMAND, DPI/display messages and fitted-viewport wiring. Native: hosting on/off, collapse/expand repeatedly, Alt-Tab, closing last controls panel, host maximize/minimize, monitor removal, saved collapsed startup, Windows system menu and Explorer tray restart. Mark untestable paths pending.

## Checkpoint 2 — Commands and navigation that stay understandable

Create a shared command registry with stable identifiers, labels/tooltips, source requirements, enabled/checked state and one dispatch path. Use it for menus, cards, quick actions and keyboard bindings. Move presentation code out of the growing main.cpp where helpful, with small Win32 adapters; avoid a broad controller rewrite.

### Primary strip and menu map

Default Condensed strip: **character selector | + Character | Chat | Attention/count + Next | Characters | View | Settings**. Labels remain visible by default. The selector also exposes selected-character actions. When width is tight, move secondary actions to a clearly labelled More menu; keep character switching, Chat, attention status and recovery accessible. Menus open by mouse or keyboard with visible focus, appropriate access keys and preserved selection while status changes.

| Location | Commands and organization |
| --- | --- |
| Character selector/card action menu | Selected character's Monitor, Workspaces, Warm/Economy, appearance/alias/color/alerts, Restart/Cancel restart, Close. Explicit target name and status. Failed/disconnected/pending-restart slots remain selectable for management. |
| + Character | Next free login, individual closed slots, Copy main settings and open login. Preserve the current confirmed, bounded import policy and private profile lease. |
| Characters | Switch to ready character; selected-character actions; Session sets; Manage profiles/Open profile folder; separate lifecycle section for Stop managing and Close host. |
| Chat | Show/hide, Dock/Pop out, account/conversation selection, existing Pins; source-specific Mark read and Review in viewer remain close to the conversation. |
| Attention | Next/Previous, inbox, filters, Dismiss; Review in viewer is a separate explicit action. Unread/notification status remains visible without an expanded inbox. |
| View | Expanded/Condensed/Minimal; cards/dropdown; shared Chat/controls docking; Monitors submenu with live targets and Stop all; diagnostics; window recovery. |
| Settings | Appearance and density; Customize quick actions; Audio/alerts; Switching/shortcuts/transitions; Save host choices. Group into a small settings dialog with sections and Apply/Cancel, not a cascade of tiny dialogs. |
| Each window's header/menu | Dock/Pop out when applicable, Always on top, relevant local controls, Close/hide, Restore host controls. Monitor size/rate/Chat options belong here. |

Workspaces still open the owning active viewer's existing controls; explain when activation is needed and offer explicit Switch then open. Never silently change the world merely by selecting a management target. Keep **Show active viewer in separate window** distinct from **Stop managing all characters**, and make their effects explicit. Host close should use descriptive buttons for Leave characters running, Log out characters, and Cancel, replacing the ambiguous Yes/No meaning if supported by the existing Win32 facilities.

Activation and management gestures are explicit: a primary card click or committed ready-character choice in the switch dropdown requests a world switch. Arrow keys move focus/highlight; Enter or a mouse selection commits; Escape cancels without switching. A card action-menu button, Shift+F10/context menu, or the labelled Selected character actions control chooses the management target and opens its commands without switching the world or chat sender. Clicking/committing a disconnected, closed or restarting character opens its management/status actions instead of attempting promotion. Keep the active badge distinct from the selected-management outline. Do not reuse the existing dropdown CBN_SELCHANGE handler, which immediately calls switchTo(), for management-only selection. Only successful handoff synchronizes the active indicator; an unsuccessful switch keeps the prior active world and the chosen management target available for recovery.

Restart/Cancel restart must be immediately visible on a disconnected/restarting card or selected status area. Do not bury it in Settings. Keep Escape/Skip transition available while busy; do not disable the whole shell for a handoff. Mutating actions retain current source revalidation after menus/dialogs run.

Customization: optional direct actions for Monitor selected, Restart, Workspaces, background mode, voice-follow, mute, transition skip and diagnostics; reorder via move-left/right controls and optionally drag; icon+text default with text-only/icon-only alternatives and tooltips/accessibility names. Up to 12 quick actions, known command IDs only, no script/path/macro execution. Mandatory recovery/identity stays available; reset toolbar and reset all appearance are separate operations. Destructive/lifecycle buttons retain native confirmations regardless of where pinned.

Focused checks: command mapping/eligibility, management vs active vs sender identity, stale menu targets, overflow without clipping, lifecycle recovery availability. Native keyboard/Tab/IME and mouse on each presentation, including detached and busy states.

## Checkpoint 3 — Folderstorm themes, identity and quiet status

### Shared appearance

Use semantic theme tokens across main host, shared Chat, character panel, monitors, attention inbox and settings dialogs. Reuse the existing Folderstorm theme's colors as the design source, not worker XML as a runtime requirement. Host theme is independent of each viewer's skin; no native skin switching or profile mutation.

| Token | Folderstorm Midnight (default) | Folderstorm Daylight |
| --- | --- | --- |
| Background | #111016 | #F7F5FB |
| Panel | #1A1921 | #FFFFFF |
| Border | #302D3B | #D6D0E0 |
| Primary text | #F5F3FA | #211A2B |
| Secondary text | #ADA9BA | #61566F |
| Accent | #BFA4FC | #6C3FA8 |
| Warm brand accent | #F0CC53 | #735A00 |

Dark values are derived from `skins/firestorm/themes/folderstorm/colors.xml`; light values are proposed tokens to validate for contrast. Use gold sparingly for identity/attention accents and lavender for selection/focus. Body text uses the readable foreground, not arbitrary character color. Support Midnight, Daylight and Follow Windows, with Midnight default for new configuration. Windows High Contrast overrides decorative colors. Existing users retain density/docking/visibility; do not turn a saved large layout into Condensed without their choice.

Use native caption/system controls and supported Win32/DWM theming where available; do not use undocumented ordinal dark-mode APIs or a web-rendering dependency. Keep native edit controls for selection, clipboard, IME and accessibility. Owner drawing is limited to themed surfaces/buttons/cards where necessary; supply accessible names, state, focus, contrast and high-contrast fallback. If native menus cannot be safely themed on an OS version, a readable native menu is acceptable and documented. No full custom title bar required.

Default density is Compact, approximately 28–32 logical pixels per main row; Comfortable uses approximately 36–40. Scale all layout/font/hit targets by per-window DPI and text scale, using font metrics rather than hardcoded character widths. Compact shell should fit around 560 logical pixels with overflow; the detached switcher should also support a narrow vertical card list. Detached Chat should become usable around 360 logical pixels by stacking selectors/actions. These are targets, not reasons to clip text or controls at large accessibility sizes.

No blinking, pulsing, continuously moving decorations or large saturated header fills by default. Respect reduced-motion settings for shell effects and transitions. Show clear focus/hover/selected/disabled states and reserve stable space for changing badges to prevent layout jitter. Render/invalidate only on meaningful changes.

### Character switching and chat identity

Expanded view offers compact character cards with name/alias, fixed color accent, state (Active/Warm/Economy/Disconnected/Restarting), unread badge and action menu. Condensed uses the dropdown; both stay selectable options in every density. Cards can be reordered as presentation order without changing slot/profile identities. Do not introduce avatar/profile-image fetching as a dependency; initials or the existing mascot suffice.

Show three separate concepts clearly: the current active world, selected management target and **Send as**. Aliases never hide the actual account identity completely; retain actual account name and grid in the sender details/tooltip, disambiguating duplicate aliases visibly. Use account/grid color in selectors, a restrained composer border and monitor header; contrast-adjust the display accent without rewriting the stored RGB. Keep the sender label always visible even in narrow or icon-only modes.

World switching must leave shared Chat sender/draft unchanged. Selecting a different conversation/account preserves the old draft and restores the correct canonical draft before enabling Send. Monitor exchange rebinds the monitor composer only after successful handoff, under existing guards. Theme/dock/resize changes preserve caret, selection, scroll position, IME composition and drafts; send acknowledgments cannot erase subsequent edits. Restriction/session changes clear forbidden history/attention/preview immediately in all themed views.

### Quiet state and diagnostics

Default status says useful things: Active, 2 unread, Restart waiting for window to close, Disconnected, or Monitor paused. Detailed MiB/draws/loops/frame age/rate caps/worker diagnostics move into an expandable Details panel, with optional quick action. Important errors have a short explanation and available action; do not replace the whole window title with every polling update. Show stale timestamps/unknown measurements honestly; do not imply Warm/Economy savings were measured.

Errors requiring recovery remain visible in compact/minimal presentations as a stable badge/indicator. Details may offer Copy diagnostics with account names/paths excluded by default and explicit review, never chat text or secrets. No periodic notifications just because telemetry changed.

Host appearance/settings use an editable draft with Apply/Cancel; preview changes revert on Cancel. Apply updates runtime, and clearly labelled Save host choices persists, matching the existing contract. Immediate view/window toggles remain session-only until Save. Do not save account preferences as a side effect of a theme preview or apply host changes via native viewer Preferences.

Focused checks: token contrast (4.5:1 normal text, 3:1 control/focus boundaries where applicable), migration and defaults, stable identity under reorder, geometry using nontrivial DPI/long labels, draft/state preservation paths. Native visuals for dark/light/High Contrast, 100/150/200% scale, mixed DPI, keyboard/IME, several viewer skins. A mockup is design evidence only.

## Checkpoint 4 — Monitor polish and independent Always on top

Monitor header includes character accent/name, state, Exchange with active, Chat toggle and a compact menu. Keep image read-only and aspect-correct. Existing explicit exchange gesture remains; the new labelled button improves discoverability. A subtle stale/paused/restricted/disconnected label distinguishes a held frame from a live capture. No extra full-width debug footer by default. Preserve four-monitor/aggregate-10-FPS bounds, actual low-resolution rendering, privacy clearing, event-driven buffering and stopping capture on minimize/close.

Offer **Always on top** independently for detached Chat, detached character controls, every monitor, and Attention. Default Off. Provide a small pin control with tooltip and checked state plus a checked item in each window's system/Window menu. Topmost is a window property, not a character privilege: it stays with the monitor window when sources exchange. Re-docking suspends it; undocking restores its runtime choice. Keep persistence by stable presentation-window key through Save host choices; never inherit a character's preference through reused slot/account identity.

Use `SetWindowPos(HWND_TOPMOST/HWND_NOTOPMOST, ... SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE)` on that panel only. Do not mark the main host or worker topmost as an implementation shortcut. Audit Win32 owned-window propagation, popup/modal ordering and `fitSurface()` together: pinning one panel must not pin all viewers, cover its own menus, hide the world, steal focus, or leave native confirmation dialogs inaccessible. Register any new auxiliary HWND in existing foreground/input routing; all panels retain dialog navigation. If ownership must change, explicitly preserve owner-close/minimize behavior and cleanup rather than using cross-process reparenting.

Pins remain respected while another application is foreground but never force activation. Closing/hiding removes the visible topmost surface; restore on reopen only according to that panel's choice. Main host minimize follows existing panel policy, not a newly invented always-visible behavior. Recovery clears pins for the current runtime so misplaced pinned panels can always be reclaimed. No persistence of active frame/chat contents.

Focused checks: per-panel state migration, dock/undock/rebind and reset behavior; native z-order requires Windows testing. Test each panel independently over the viewer and another app, menus/dialogs, host minimize, monitor swapping, pinned panel close, mixed DPI, recovery and restriction changes. Do not claim a model test proves Win32 stacking correct.

## Checkpoint 5 — Named session sets

Add Characters → Session sets: Save current as, Open, Update, Rename, Duplicate, Delete and Manage. A set remembers a chosen subset/order of up to five existing private profiles, their expected account/grid references where known, Warm/Economy choices, presentation modes, panel geometry/docking and optional monitor assignments. Default capture includes selected profiles/order/background policies and shell layout; monitor reopening is an explicit saved option shown in the Open summary. Account colors, chat pins and shared host theme remain global appearance preferences, not duplicated divergent copies. No implicit account-workspace restoration added here.

Save/Update use a small dialog showing capture choices and name; Update confirms replacement and preserves those choices. Open shows the profile list, already-running/missing/conflicting states, intended layout changes and **Open logins**. Explicit opening may launch missing profiles through ordinary native login; never submit credentials, click Login, silently restart a running worker, terminate other characters or exceed five concurrent profiles. Cancel stops remaining launches, leaves already opened login windows untouched and reports them. Launch sequentially through the existing registry/lease path and bound pending intents; timeout/failure does not retry forever.

Sets reference internal private-profile IDs, never arbitrary executable or filesystem paths. Separate profile identity (may exist before login) from account/grid identity (known only after successful login). Opening a set reuses a running profile only when its binding matches; on mismatch, mark that entry needing attention and do not apply account-specific follow-ups. Do not assume the requested account will log in. Allow exactly one active Open operation with a unique operation token and captured profile/account expectations. Revalidate the token before every launch, layout application or delayed monitor follow-up. Invalidate it synchronously on Cancel, Stop managing, host close/logout, controller loss, worker replacement, timeout or a newer explicit Open. A new Open first cancels the earlier operation and reports already-opened login windows; it never replays its remaining launches. Lifecycle cancellation must clear the queued work before detachment begins, so a delayed launch cannot call beginManagement() and undo Stop managing. Already-opened native windows remain subject to normal lifecycle rules; cancelling a set does not forcibly terminate them.

Nonmembers keep running with their policies. Existing same-profile sessions are reused, not duplicated. Existing unrelated sessions consume capacity; show entries that cannot fit with explicit skip/cancel choices rather than evicting them. Apply session layout without changing shared Chat sender or discarding drafts. Fit geometry before showing windows. No forced world switch; initial activation is the existing normal lifecycle behavior, and already-active characters remain active.

Saved monitor assignments open only when explicitly included in the user's current Open action, and only after the expected account/grid is ready and current restrictions permit capture. No startup capture because a set file exists. Revalidate at application time, respect all preview budgets and expose skipped assignments. Default topmost is preserved from current per-window preferences rather than implicitly enabling pins from a set.

Bounded independent versioned store: at most 32 sets, 64 KiB, 64 UTF-8 bytes per name, five distinct profile references and four monitors per set; validate IDs, enum ranges, UTF-8, geometry and uniqueness. Atomic writes and transactional parse; no partial mutation on malformed/truncated/oversized data. Duplicate names get an explicit Replace/Save as choice, not silent overwrite. Profile refs remain inside the existing private profile root. Deleting a set never deletes profiles or logged-in accounts. Handle missing profiles with a clear skipped-entry report.

Focused checks: set roundtrip, malformed/oversized/duplicate data, five-slot capacity with unrelated running sessions, mismatched account login, cancelled/partial launch, Stop managing or close during a queued launch, overlapping Open operations, late replies after cancellation, unavailable/locked profiles, no credential persistence, delayed guarded monitor opening. Native: save a set, close/reopen selected profiles normally, preserve unrelated logins and drafts, cancellation, missing profile, panel layout on changed monitors.

## Checkpoint 6 — Attention navigation and finishing pass

Build Next/Previous attention atop the existing inbox, event cursor and dismissal watermarks. Default includes unread IMs and supported native attention (teleport, permission and Inventory offers); Nearby is available as a filter, Off by default for navigation. Filters cover categories and accounts; local navigation preferences do not change native unread counts, notification acceptance or existing alert sounds. Keep dismissed reminders hidden until qualifying new activity, including repeated dismissals.

Use stable source keys and an explicit, deterministic navigation order: urgent native-action categories first, then unread conversations, oldest pending within a category with stable tie-breaks. Keep the chosen row stable as new events arrive; Next advances from its source key and wraps once. Coalesce conversation unread into one target, deduplicate events and avoid loops on stale/removed items. Show an empty state without switching characters when nothing eligible remains. Label ordering and allow oldest-first across all categories as an alternative in Settings.

Next selects an eligible conversation in shared Chat or the corresponding inbox entry; it does not automatically activate that character's world, mark read, dismiss or accept an offer. Expose explicit **Review in viewer** or **Switch to character** beside the selected attention. Save the prior chat draft before changing conversation. User-configurable typing-safe navigation shortcuts have no new imposed defaults. Add explicit Next/Previous attention commands to the extended command/binding model from checkpoint 1 rather than overloading the existing seven switch indices. Apply source/restriction eligibility per command and retain conflict handling; never relax world-switch guards to enable shell navigation. New restrictions/end-of-session remove items immediately, preserving the queued-restriction fixes in PR27.

Complete command discoverability and empty states: logged out, all five slots full, no monitors, stale monitor, no attention, malformed settings and pending restart. Ensure every action from the old Session popup is retained in the new map or deliberately replaced by an equivalent visible flow; produce an old→new command coverage table in the PR. Verify that the condensed/minimal UI has no unrecoverable path and no identity ambiguity.

Focused checks: deterministic traversal/wrap/removal, identical conversation IDs on different accounts, repeated dismissal/new message, category/account filters, restricted items and stale generation, chat-draft preservation. Run only helpers/controllers affected by the changes. Finish with a read-only Astra High cumulative review, then address confirmed findings and rerun affected checks. Record pending native tests, not fictitious visual acceptance.

## Persistence and migration design across checkpoints

Keep existing `FSH1` HostOptions readable with its 512-byte limit; do not append arbitrary strings to its one-digit parser. Keep existing `FSP1` presentation readable. Add a versioned bounded host-experience store for theme, density, card/dropdown preference, quick-action order, attention filters and stable per-panel pin/recovery choices; bound to 64 KiB and reject invalid data transactionally. Session sets use the separate store described above. Stable window IDs must include Attention without weakening existing rectangle bounds or confusing monitor ids 3–6.

Expose which settings are runtime-only versus saved. Reuse one settings draft/controller; cancel restores any visual preview, and a failed write reports failure without destroying the last good file. Do not automatically overwrite old files simply by reading or starting a new version; keep existing defaults/migration graceful. Make partial multi-file save outcomes visible or stage related updates before commit. Configuration cannot smuggle arbitrary commands/paths or authorize a live account action.

## Completion checklist and handoff

- [ ] Recoverable collapsed UI, persistent reveal, tray/system-menu backup and display-change recovery.
- [ ] New command groups, direct critical actions, descriptive lifecycle choices and customizable quick actions.
- [ ] Folderstorm Midnight, Daylight and Follow Windows, High Contrast fallback, Compact/Comfortable density.
- [ ] Cards and dropdown, distinct active/management/chat sender identity and preserved drafts/IME.
- [ ] Quiet status, discoverable diagnostics and actionable failures.
- [ ] Polished read-only monitors and per-breakout Always on top, including Attention.
- [ ] Named session sets with explicit normal login and guarded optional monitor reopening.
- [ ] Next/Previous attention, filters, stable source binding and explicit native review.
- [ ] Backward-compatible bounded persistence, focused checks, command coverage and recovery matrix.
- [ ] Astra High review findings addressed, separate testing PRs attached, Windows/voice gaps stated honestly.

Questions are nonblocking: native reproduction details of the lost UI, preferred final light-theme tint and compact target widths can be collected after initial testing. Implementation uses the decisions above meanwhile. No new live voice testing is assumed available. A screenshot or build installation from the user is not a prerequisite for making progress.


## Planning review — Astra High

A read-only `gpt-6-astra` reviewer at high reasoning inspected this plan against PR27 source. Three findings were incorporated before finalizing: cancel all session-set launch/follow-up work on lifecycle exit or a replacement Open using an operation token; explicitly extend the seven-command shortcut model with command-specific eligibility and compatibility; and define switch versus management gestures, including keyboard and unavailable-character behavior. The review confirmed that independent-window pinning, reserved recovery geometry and fitSurface/ownership risks are covered, while the native cause of lost controls remains unproven. This review covers the plan; checkpoint 6 still requires a review of the actual implementation.
