# Host and custom-viewport refinements

Status: all eight requested features are implemented in source on `feat/session-refinements`, isolated worktree `/workspace/Folderstorm-session-refinements`. The starting point is PR31 head `b6d2bdeabba758829d31b38b608c724981456286`; main includes PR30 at `9b7aa69403`. Earlier published history and the original checkout are preserved. Private protocol **13** requires a matching host and viewer. Native Windows acceptance is pending.

## Completed checklist

- [x] Custom-viewport hover access and popup fitting: menu-container blank space no longer intercepts mouse-enter discovery. Camera, Graphics, Volume and Media pulldowns use each icon's real screen coordinates, fit to the full UI window, convert back into their actual parent coordinates and refresh when chrome geometry changes. Rollover preferences and native click actions are retained.
- [x] Independent background sounds: native friend/non-friend, group and conference sound switches no longer gate host alerts. Host notification ownership is acknowledged separately from world/media muting. Native immediate/deferred UI sound producers and UI gain are suppressed only while the host owns background alerts; active and unmanaged viewers retain native audio. Explicit master/UI mute, UI volume, DND, sender mute, restrictions, freshness, duplicate and burst checks remain. Eligibility is rechecked during queue drain.
- [x] Stable mouse and keyboard dropdown choices: a shared transaction retains stable IDs through native CLOSEUP ordering, commits on acceptance, restores on Escape, protects lists from polling and invalidates on source/removal/privacy changes. Main, detached and monitor selectors use it.
- [x] Settings layout and complete control painting: separate clipped field area, fixed tabs/footer, overflow-only horizontal/vertical scrolling, page-specific ranges, focus reveal, resize/DPI font updates and named checkboxes without raw 1/0 labels. Midnight/Daylight painting covers tabs, dropdown items/arrows, checkboxes, scrollbar controls and popup/transcript nonclient scrollbars. High Contrast uses native controls/system colors.
- [x] Separate category visibility and Attention choices: Nearby, Direct IM, Group, Conference, Viewer Notifications, teleport offers, permissions and inventory offers. Settings > Chats and notifications separates Show in Chat, Attention, Read behavior and Characters. Chat/View/monitor menus provide quick visibility controls. Filters do not mark read or change sound choices; Attention/account badge counts and pinned rows respect their applicable filters.
- [x] Viewer notification descriptions/lifetimes: native notification IDs and bounded formatted descriptions replace the single sticky generic reminder. Removal/expiration clears the corresponding host row/history/badge. Known offers retain explicit native review and response ownership. Native or host Read/Dismiss never accepts an offer.
- [x] Host conversation closing and native session close detection: Chat > Close conversation in host and monitor × hide a conversation across host surfaces while retaining its unsent draft. Nearby cannot be closed. A worker-acknowledged event fence prevents catalog refreshes, old buffered messages, system messages or outgoing echoes from reopening it; new incoming messages reopen it when its category is visible. A native session observer tracks real removals, not minimization/tab hiding. Host Close does not leave groups, terminate calls or retry messages.
- [x] Offline private-profile names: Profile name can be edited before login and stays visible while stopped, starting or disconnected. Labels seed from known account names and follow the unchanged CharacterN profile identity/card ordering. Ready account appearance retains source guards and actual sending-account labels. Save host choices persists names and filters.

## Storage and boundaries

`host-chat.dat` advances to bounded FSC3 and reads FSC2. The prior combined Attention setting migrates into separate categories without unexpectedly hiding existing conversations. FSP1/FSX1 stores remain readable and unchanged; their previous file backup/save behavior remains. Labels are bounded to 64 UTF-8 bytes per five private profiles. Saved data contains choices, labels and sound asset references, not transcripts, drafts, native notification contents, Inventory contents, selections or credentials.

Existing worker/generation/account/grid guards, five-character handling, single input/microphone ownership, receive/send restrictions, Preferences transactions, monitor image limits, source-bound drafts and uncertain-send rules remain. Close is host metadata only. Conversation/notification state is bounded by the existing session-local model/queue limits; a retained-history gap still directs users to native history/notifications.

## Actual focused checks

Twelve affected portable checks passed with C++17 `-Wall -Wextra -Werror -Wshadow -pedantic`; session tests used `-DNDEBUG -UNDEBUG` to retain Release assertions:

- chrome_layout
- choice_state
- dialog_layout
- chat_lifecycle
- protocol
- chat
- monitor_chat
- read_policy
- alerts
- usability
- experience
- controller

Final protocol cases explicitly round-trip CloseChat, ConversationClosed and NoticeRemoved and reject an unknown event type. Lifecycle cases cover migration/invalid input, independent categories, saved profile labels, close fences, retained drafts, native removal, system/outgoing messages, stale generations and queued notice privacy. Final lifecycle/protocol/usability checks were repeated when related review changes justified them. Source review covered Win32 selection/message routing, fixed/scrolling parent hierarchies, owner-draw forwarding/font sizing, theme callback lifetimes, observer teardown, sound ownership/drain checks, profile identity and menus. Cumulative diff hygiene passed.

No native Windows/MSVC or MinGW compiler is available here. No viewer/host build, installer/packaging/GitHub build, broad suite, unrelated Go test or subagent was used. Portable checks and source inspection do not establish Windows visual/runtime acceptance.

## Windows acceptance

1. Reproduce the user's Standard-versus-custom viewport test. Check Volume/Media/Graphics/Camera hover and rollover-Off click behavior in window/viewport bar placements, resize, display scaling and supported skins. Leave a popup open while changing geometry.
2. With native IM sound switches Off, select two custom WAVs and test Direct/Group/Conference eligibility with each account inactive, monitor Chat shown/hidden, background world mute On/Off and native/host explicit mute/DND. Switch back and check no deferred duplicate. Keep active native notifications unchanged.
3. Mouse-select and arrow-select conversations/accounts in shared/detached/monitor Chat during arrivals. Escape restores the previous choice. Disconnect/removal/restriction changes invalidate safely; drafts and send/read identities stay correct.
4. Open all settings dialogs in Midnight/Daylight/High Contrast at ordinary and larger text/display scales. Check no unnecessary bars; overflow scrolls only fields, tabs/buttons stay visible, selected dropdown rows/arrows/scrollbars match, and Tab/Shift+Tab/Space/Enter/Cancel work.
5. Hide groups and Viewer Notifications independently in Chat and Attention, then reopen/save/restart. Confirm filtered badges/pins, unchanged native unread/subscriptions and independent sound categories. Test no visible chat categories and inactive monitor Chat.
6. Resolve or expire native generic notifications and supported offers; verify correct host removals and that Read/Dismiss/Close never accepts anything. Confirm descriptions clear under privacy restrictions and sender restrictions changing during queued delivery.
7. Close a private/group/conference host conversation with an unsent draft; old queued/system/outgoing events and catalog refreshes keep it closed. A new real message reopens it with the correct draft/account. Close in native UI; minimize/switch tabs without closing; test multiple surfaces and late acknowledgements.
8. Name an unused private profile; rename a logged-in account; stop/restart/reorder cards and save/reopen the host. Verify labels remain attached to their numeric private folders and actual sending account/grid remain clear. Cancel must preserve earlier choices.

Live voice acceptance remains unavailable; voice ownership paths were not changed.
