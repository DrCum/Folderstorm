# Session host experience — implemented features and Windows handoff

Source implementation completed 2026-10-10. The recovery checkpoint is [PR #28](https://github.com/DrCum/Folderstorm/pull/28), based on unmerged PR #27. The design/tools checkpoint is [PR #29](https://github.com/DrCum/Folderstorm/pull/29), `feat/session-host-design`, based on `feat/session-host-recovery`. Both PRs are draft and attached to this chat. These are dependent cumulative builds; no PR was merged or earlier published history rewritten. This document records source completion, not native Windows acceptance.

## Completed features

- **Recoverable minimal UI.** A focusable Show controls strip sits outside the fitted viewer rectangle. Minimal mode, closing popped-out controls, saved placement, tray and system-menu recovery reuse the same restoration path. Manual position recovery brings panels onto a current work area, restores minimized panels and clears their pins for the session. Explorer restarts re-register the tray backup. New optional host/native recovery shortcuts default unassigned.
- **Organized commands.** Characters, View, Settings, Open login and Attention replace the long Session popup. Narrow windows use More. Restart/Cancel restart stays visible for the selected disconnected/restarting profile. Cards have separate switch and management actions; committed dropdown choices switch, while arrows/Escape do not. Workspaces for an inactive character require an explicit switch confirmation. Host close uses descriptive command links with a native-editor fallback.
- **Customizable quick actions.** Up to twelve ordered, allowlisted actions; icon/text, text-only or icon-only labels; tooltips; separate reset. No extra quick row by default. Actions use the same guarded dispatch as menus. Characters/View/Settings menus and session sets can also be pinned.
- **Folderstorm themes.** Midnight, Daylight and Follow Windows, High Contrast fallback, Compact/Comfortable density, native captions/edit controls and visible selection/focus/hover/disabled states. Per-window DPI and Windows text scale affect fonts and controls. Oversized settings dialogs scroll and reveal keyboard focus. Owner-drawn selector rows are measured again after font/DPI changes and docking.
- **Cards and clearer identity.** Optional reordered character cards in Expanded view; compact dropdown in Condensed. Active world, management target and Send as remain distinct. Actual account/grid details are available on the sender tooltip; alias labels retain the real name. Display accents are adjusted for contrast without overwriting chosen colors. World switching does not change the shared composer account or move drafts.
- **Quiet status.** Default surfaces show useful state and unread counts. Resource counters move behind Show diagnostics. Monitor rate/age diagnostics are optional. Small work areas fall back to condensed controls; the Chat composer reserves space above the notice footer. Minimal reveal text can expose actionable notices.
- **Monitor polish and window pinning.** Exchange, Chat, Pin and Options buttons; explicit waiting/paused/stale/restricted/disconnected states; readable aspect-correct image. Each Chat, controls, Attention and monitor window has independent Always on top, default Off. Docking suspends a pin; undocking restores its preference. A monitor keeps its pin when exchanging characters. Monitor Options can save the current size/rate as defaults. Four previews and the shared 10-FPS bound are preserved.
- **Named session sets.** Save current as, Open, Update, Rename, Duplicate, Delete and Cancel opening. Capture choices include selected private profiles, Warm/Economy policies, shell arrangement and optional monitor assignments. Update confirms replacement and preserves capture choices. Open reviews the intended result and launches ordinary login windows sequentially; it does not submit credentials or evict other sessions. Missing, locked, disconnected, conflicting or unavailable profiles/monitors are reported. At most one opening operation survives; cancellation/replacement/lifecycle changes revoke later actions, including skipped-profile monitors.
- **Attention navigation.** Next/Previous, category and profile filters, default Nearby Off, native attention before unread conversations or optional oldest-first order. Source-qualified deduplication, stable selection, wrap and removal. Navigation selects shared Chat or an inbox entry; Review in viewer remains explicit. It does not accept offers, switch worlds or mark messages read. Prior drafts are saved before changing the composer target.
- **Guarded persistence and bindings.** Existing FSH1/FSP1 files remain readable. Independent bounded, versioned host-experience and session-set stores reject malformed/truncated data transactionally and save by atomic replacement. Session sets are limited to 32 sets/64 KiB, five profiles and four monitors. Runtime Apply/Cancel remains separate from Save host choices; partial multi-file saves are reported. Three native Controls actions and optional host chords extend recovery/attention without weakening the existing seven switch-command guards.

Host session sets contain only profile/account references and presentation configuration. They exclude credentials, conversation text, drafts, Inventory contents, filters/selections and recorded simulator actions. Native account/session restrictions, microphone/input ownership, private profile leases, Preferences transactions and uncertain-send handling remain in force.

## Command coverage: old Session popup → current location

| Earlier action | Current location |
| --- | --- |
| Manage workspaces; close; restart; cancel restart | Characters/selected card actions; selected-profile Restart/Cancel shortcut; workspaces offer explicit switch when needed |
| Character alias, color and alerts | Characters → Name, color and alerts |
| Open profile folder; copy main settings and open login | Characters/selected actions; source confirmation and profile isolation retained |
| Stop managing all; close host | Characters lifecycle section; separate confirmations |
| Voice follows active; mute background sound; alert volume | Settings → Host settings → Audio; Voice/Mute optional quick actions |
| Switch to each character; next/previous ready character | Characters → Switch character; optional existing character shortcuts |
| Configure character shortcuts | Settings → Keyboard shortcuts; includes recovery/attention and viewer Controls opt-in |
| Attention inbox | Attention menu → Open inbox; optional quick action |
| Transition style/duration/height; skip animation | Settings → Host settings → Switching; View → Skip current transition; Escape; optional Skip quick action |
| Open each inactive monitor; stop all monitors | View → Monitors; selected character Monitor action |
| Monitor size/rate and saved defaults | Each monitor → Options → size/rate; Use this size and frame rate as monitor defaults, then Save host choices |
| Normal/Condensed/Collapsed | View → Expanded/Condensed/Minimal with Show controls; system menu |
| MiB/draws/loops descriptions | View → Show diagnostics; optional Details quick action |
| Last status/notice | Notice footer; minimal Show controls strip |
| Pop out/dock Chat and controls; show/hide Chat | View; Chat checkbox; optional docking quick actions |
| Save host choices | Settings; optional Save choices quick action |

## Actual checks and review

Seven affected portable checks passed directly with C++17, `-Wall -Wextra -Werror -Wshadow -pedantic -DNDEBUG -UNDEBUG`: recovery, experience, sets, shortcuts, protocol, controller and monitor_chat. Assertions stayed enabled. The new tests cover recovery geometry, transactional stores and limits, command eligibility/key conflicts, accent contrast, attention wrap/dedup/removal, opening replacement/cancellation, skipped-source independence and skipped monitor revocation. Existing affected protocol/controller/Chat tests cover the shared boundaries reused by the implementation. Final experience/sets checks were repeated after review revisions.

Source checks cover XML/native action registration, protocol 11 compatibility, CMake registration/link dependencies, menu coverage, ownership/restriction checks, font/geometry changes and diff hygiene. No broad test suite, Go tests, Windows viewer/host builds, installer packaging or GitHub builds were run. No Windows/Wine toolchain was available; native pictures and behavior were not verified here. CMake/CTest was not executed.

A read-only **gpt-6.1-sol, max** subagent reviewed the cumulative implementation at the user's request. Confirmed fixes include the monitor callback return, cancellation before replacement-set confirmation, monitor defaults discoverability, High Contrast selection, first-open-only Attention placement, unavailable-profile skips, revocation of their delayed monitors, detached context-menu routing, scrolled-tab focus reveal, mixed-DPI docking font measurement and complete quick-action registration/dispatch. The review does not certify Win32 rendering, focus, z-order or live voice.

## Focused Windows acceptance

Build the matching host and viewer together: **private protocol 11 rejects mixed binaries**. Keep the recovery-only PR28 build separately if tracing the lost-controls report.

1. **Recovery first:** hosting on/off; Minimal → pop out controls → close controls; Show controls, Alt+Space, tray recovery; minimize/maximize; saved minimal startup; Explorer restart; monitor unplug/negative coordinates; pinned misplaced panels. Recover must preserve accounts, drafts and active ownership.
2. **Commands and focus:** five characters; disconnected/restarting target management; Tab/Shift+Tab; arrows/Enter/Escape; right-click/Shift+F10 docked and detached; IME; menus during handoff; each optional quick action. Close → Cancel must leave everything running. Leave logged in versus native logout must follow the labelled choice.
3. **Theme/accessibility:** Midnight/Daylight/Follow Windows/High Contrast; Compact/Comfortable; 100/150/200% DPI and Windows text scale, long aliases; mixed-monitor dock/undock; scroll to the bottom of settings then Tab back to the page tabs. Check selection, text, clipping, focus and retained drafts/caret.
4. **Pinning/monitors:** each panel independently over a viewer/other app; native dialogs and menus; host minimize; source exchange; pin close/reopen/docking/recovery. Confirm stopped/minimized/restricted previews clear/pause appropriately and size/rate defaults persist only after Save.
5. **Session sets:** Save/Update capture options, rename/duplicate/delete, explicit login, unrelated running profiles, mismatched account/grid, locked/missing/detached/disconnected entries, partial completion report. Start replacement Open while a prior set waits: prior work must stop before confirmation, even on Cancel. Cancel/Stop managing/host close/session replacement must prevent further launches and monitors. No auto-login or reopened skipped monitor.
6. **Attention and Chat:** inactive unread IM/native offers; optional Nearby; filters; repeated dismissal/new messages; wrap/removal/restriction changes; same conversation IDs on different accounts. Next must preserve drafts, avoid world promotion/read/acceptance and require explicit Review. Recovery/attention native Controls actions must not fire while typing or composing IME.

Voice follows active remains untested because live voice testing is unavailable to the user. Native topmost/owned-dialog ordering, actual keyboard/IME behavior and visual acceptance remain pending. No claim of measured performance changes is made.

## Implementation choices

The twelve optional quick actions are reordered through numbered choices; card order uses earlier/later actions. Drag reordering was optional and is not required. Readable OS menus remain native. Diagnostics remain local and Copy diagnostics was optional, so no clipboard-export feature was added. Daylight is independent of viewer skin. Appearance changes apply on accepting the settings draft, so Cancel has no live-preview changes to undo. None of these choices require new user setup.
