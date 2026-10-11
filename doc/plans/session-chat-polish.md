# Session chat, read handling and character sounds

Status: implemented in source on `feat/session-chat-polish`, isolated worktree `/workspace/Folderstorm-session-chat-polish`, based on unmerged PR #30 (`feat/session-profile-copy`, `9b7aa69403`). Native Windows acceptance is pending. The plan-only branch and earlier published runtime branches remain preserved. The implementation uses private protocol **12**; build and install matching host/viewer binaries.

## Completed-feature checklist

- [x] Managed typing guard: physical-key quarantine, selective stale logical-key cleanup, all forward aliases for per-frame scans, per-origin Unicode suppression, ownership-epoch rejection of posted key/text input and bounded content-free diagnostics. This addresses the leading source cause; the user's exact intermittent Windows trigger is not reproduced here.
- [x] Latest-message history on main, detached and monitor Chat; incremental appends, message-ID anchors for older reading/selection, hidden/show/DPI refitting and contextual Latest buttons.
- [x] Enter sends; Shift+Enter inserts a newline; repeat/IME commit guards and shared native composer adapter. Pending sends keep editor focus with temporary read-only backpressure; drafts and uncertain-send behavior remain intact.
- [x] Displayed-chat automatic reading, default On, only for shown, non-minimized, painted tail views; source-bound coalescing and unseen-arrival protection.
- [x] Optional active-character automatic reading, default Off, 60-second initial delay, range 5–3600 seconds; new-message grace and foreground/visibility/ownership/session resets.
- [x] Independent per-character Direct IM, Group, Conference and supported-notification alert categories. Existing combined choices are inherited until explicitly customized.
- [x] Custom character WAV sounds with picker, Preview and Reset; validated atomic local import, mute/volume behavior, owned async playback and bounded cache.
- [x] Additive bounded `host-chat.dat` settings, prior-file backup and explicit Save host choices; existing FSP1/FSX1 files retain their own formats and choices.

Actual checks: eight affected portable checks (`input_guard`, `chat_behavior`, `read_policy`, `sound`, `alerts`, `protocol`, `chat`, `monitor_chat`) passed under C++17 with `-Wall -Wextra -Werror -Wshadow -pedantic -DNDEBUG -UNDEBUG`. Source-reviewed native event/caret/dialog/read/audio paths, source/epoch guards, Send/draft backpressure, detached Pin/Latest geometry, account changes, parser/file limits and CMake registration. `git diff --check` passed. No Windows/MSVC compiler is available here; no native host/viewer build, installer, packaging, GitHub builds, broad suite, unrelated Go tests or subagents were run.

Implementation choices: automatic read defers quietly through a `ReadDeferred` reply if the worker has newer queued events than the host consumed. Active native unread counts are refreshed when draining queued IM events. Background custom sounds remain an additional filter on native eligibility; active characters use their native alert producer. Audio assets are at most 4 MiB/10 seconds, with a 20 MiB cache; Preview/alert playback holds its own backing buffer. Read and sound dialogs use Apply/Cancel, with persistence through Save host choices.

Native acceptance: extended four/five-character hosted/separate switching and typing, lock/modifier keys and IME/paste; initial/tail/scrolled-back Chat, Enter/Shift+Enter and pending sends on all surfaces; hidden/minimized/collapsed/older-history read behavior and arrivals during reads; active delay/reset/new-message grace; direct-versus-group alerts, two custom character sounds, Preview/Reset/Cancel, missing/corrupt/non-ASCII sources, saving/restarting and dark/light/DPI layouts. See the detailed sections below for exact scope and constraints.

## Evidence from testing and source inspection

- The user can type in host Chat and move with WASD in the viewer, but cannot type in native viewer Chat, the address bar or Settings search. Enter can focus toolbar Nearby Chat despite another editor having focus. Stopping host management while leaving characters logged in immediately restores typing. A regular viewer launched without the host also accepts typing. The failure is intermittent: four characters worked normally for a while before typing failed. Prioritize the managed input path; there is no evidence here requiring a computer reboot or attributing this to upstream Firestorm.
- `FSSessionWorker::textAllowed()` denies all Unicode input while **any** entry in the handoff `heldKeys` array remains set. `keyAllowed()` denies only the matching key. This difference explains how text can fail while unrelated movement keys work. The polling fallback requires both Windows physical release and a cleared native keyboard level. Native Caps Lock level represents toggle state; native key-up callbacks are conditional on the keyboard level. These are plausible ways for the release bookkeeping to become stale. The exact triggering key has not been reproduced on Windows.
- Both `Host::refreshChat()` and `refreshMonitorChat()` replace history using `SetWindowTextW`, then use `EM_SETSEL(-1, -1)` and `EM_SCROLLCARET`. A negative selection start removes selection rather than explicitly placing the caret at the text end. Replacing the buffer resets scroll state. The existing code therefore does not reliably follow the newest line or preserve reading an older line.
- Main Chat uses a multiline EDIT; monitor Chat uses a single-line EDIT. Neither has a shared Enter-to-send handler. Dialog routing and shortcut interception run before normal message dispatch.
- Read requests currently come from explicit Read/Mark read buttons. Host `ChatView::markRead()` clears the selected conversation after acknowledgement; the worker uses native `LLIMModel::sendNoUnreadMessages` for IM sessions. Automatic read handling needs a rendered-event watermark so a late acknowledgement cannot hide a newer unseen message.
- Character alerts have three combined choices: Off, IM, IM plus supported attention. `AlertGate` does not distinguish Private, Group and Conference within the IM choice. The worker already distinguishes these topics and checks native friend/nonfriend/group/conference sound preferences, Do Not Disturb, mute and restrictions. This foundation supports separate host notification categories without changing native viewer preferences.

## Delivery order

| Checkpoint | Scope | Native acceptance target |
|---|---|---|
| 1 | Managed viewer typing recovery | Switching, held keys, editor focus, Unicode/IME and detachment |
| 2 | Latest-message history and Enter-to-send | Main, detached and monitor Chat; scrolling and drafts |
| 3 | Visible-chat read handling and optional delayed active-character read | Correct account/conversation badges and arrival races |
| 4 | Separate notification categories and custom character sounds | Independent group mute, per-character audio and persistence |

Keep separate reviewable commits and publish useful Windows testing checkpoints when implementation is authorized. Do not merge or alter the earlier published PRs. Resolve source-level failures and continue independent work; native acceptance remains explicitly pending until tested.

## 1. Repair managed viewer text input first

Preserve the single managed input owner and all session/transition guards. Do not solve this by allowing text into a standby worker, synthesizing input, stealing Windows focus or disabling the held-key protection.

- Extract handoff key quarantine bookkeeping into a small testable helper. Capture only valid physical key mappings during promotion. Ignore unmapped native key zero and treat lock-key toggle state separately from physical held state.
- Release quarantine when the physical key is released, even if a key-up was delivered elsewhere or the viewer's logical keyboard state is stale. Quietly clear just its stale native logical state when every physical alias is released, preserving fresh keys; do not synthesize release callbacks. Per-frame scans match all forward native aliases, independent of the latest native message and the lossy inverse map. Key-up, ownership revocation, session change and detachment all have explicit cleanup paths. Preserve normal keyboard/mouse release and PTT reset.
- Replace the indefinite all-text veto with suppression tied to the originating held key and promotion boundary. Repeated characters from a genuinely held switching key must still be discarded; a fresh unrelated character must not be blocked by another quarantined key. Inspect the native WM_CHAR/IME event path before choosing the event-origin plumbing. Do not use a timeout that eventually admits repeats from a key still physically held.
- Keep queued pre-handoff characters bound to their originating input ownership. Handle modifier keys, keypad mappings, Unicode surrogate pairs, dead keys, paste and IME composition without turning shell shortcuts into typing shortcuts.
- Continue forwarding native focused-editor handling for Enter; after recovery, Enter must reach the editor that owns focus rather than redirecting it to toolbar Chat.
- Add bounded diagnostic logging on quarantine capture/release and a denied text event, including ownership/generation and key state but **never** the character, draft or chat contents. Avoid per-frame log spam.

Focused checks: helper cases for physical release without native key-up, stale logical state, lock keys, invalid mappings, a key held through promotion, fresh text while another key remains held, and ownership/session reset. Source-review the Windows callbacks and native keyboard API. Windows acceptance: a longer four/five-character session with repeated hosted/separate switches and focus changes; clicking main Chat, Settings search and address bar; Ctrl/Alt/Shift and Caps Lock around switching; held WASD and switching chords; IME/dead keys/paste; host shutdown with Keep logged in. Standby workers must continue refusing simulator input.

## 2. Make every Chat surface behave consistently

### Latest history and intentional scrolling

- Use a shared transcript-update adapter for main/detached Chat and every monitor. Opening a conversation defaults to its newest line. The initial scroll must happen after the EDIT has its actual visible size, including initial opening, docking, DPI changes and showing hidden monitor Chat.
- Use an explicit valid end position, not `EM_SETSEL(-1, -1)`, when following the tail. Prefer bounded incremental appends when the transcript prefix is unchanged; rebuild only on source/conversation changes, privacy clearing or history eviction.
- Track follow-tail versus reading older history per surface and live conversation. New messages, polling, another surface editing the same draft and layout changes must not pull an older-history reader to the top or bottom. Preserve selected text and a stable visible-line anchor during a rebuild.
- Provide a small Jump to latest affordance only while the reader is away from the tail. Sending from that surface resumes follow-tail for its selected conversation. A source/generation change discards old scroll state.
- History restrictions must clear the control immediately regardless of scroll state. No transcript or scroll/draft state is written into named workspaces or presentation files.

### Enter behavior

- Plain Enter sends only when the focused control is that surface's Chat composer. Shift+Enter adds a newline; make monitor composition compatible with this behavior without increasing its default height. No shell shortcut or other EDIT field should send Chat.
- Share a composer subclass/adapter across docked, detached and monitor controls. Coordinate `WM_GETDLGCODE`, Unicode/IME processing and dialog routing so Enter is processed after preceding typed characters, does not trigger a default unrelated button and does not produce a second send through WM_CHAR/key repeat.
- Enter that commits an IME composition must not send it. Preserve Tab/Shift+Tab navigation and ordinary Enter behavior in menus and dialogs.
- Reuse `queueChat` and existing monitor binding validation. Preserve the selected send-as character, conversation, restrictions, draft and uncertain-send behavior. Reject disabled, empty, stale or busy sends; never queue an automatic retry.

Focused checks: follow-tail/open/show, append versus rebuild, manual scrolling/selection, eviction, own-send follow, changing conversation and privacy reset; Enter/Shift+Enter routing, repeats, pending sends and IME. Run only affected portable chat/monitor/controller helpers. Windows acceptance covers long histories, initial login, detached panels, several monitors, narrow/DPI layouts and a send after switching world character without changing send-as.

## 3. Read the chats the user is actually viewing

### Visible Chat

New setting: **Mark displayed chats as read**, default On. Apply to the displayed conversation in main Chat, detached Chat and each monitor Chat. Reading an inactive character's visible Chat is valid; it must not switch world/input ownership.

- Count a surface as displayed only while its transcript is shown, its containing window is visible and not minimized, and it has painted through the current visible message watermark. A collapsed, hidden or closed panel must not mark anything read. Windows foreground focus is not required for an open visible panel; a user may monitor several characters at once.
- Automatic read applies at the tail. Scrolling back pauses it until Jump to latest. Merely opening an account picker, keeping Chat enabled but collapsed, or selecting a management target does not clear unread conversations.
- Coalesce duplicate read requests across surfaces. Bind every request to worker, generation, account, grid, conversation/topic and the last incoming event actually rendered. A successful read acknowledgement clears only the acknowledged watermark; newer incoming events retain their unread state.
- Add a guarded automatic-read request mode or acknowledgement metadata if needed: at the worker, refuse/defer a native read if that conversation has newer unrendered queued events. Preserve manual Mark read's explicit behavior. Ensure buffered native absolute unread updates predating a read cannot resurrect or erase badges incorrectly.
- Use the native read API for compatible IM/group/conference sessions and shell-local counts for Nearby. Never treat Mark read as accepting an offer, hiding a native permission dialog or dismissing unrelated attention.
- Stop/reset pending automatic requests on disconnect, restrictions, source changes, monitor exchange, detachment and controller loss. A failed request remains unread; retry only while its current read eligibility still holds.

### Delayed read for the active character

New opt-in setting: **Automatically mark the active character's chats read**, default Off, with **After 60 seconds** as the initial delay. Allow 5–3600 seconds. This is a deliberate automatic dismissal policy, separate from visible Chat reading.

- Count uninterrupted time with that character Ready and the confirmed active input owner. Reset on a switch, login generation change, disconnect, handoff or stop management. Suspend/reset while the host/active viewer is minimized or the session application loses foreground activity, so leaving the computer does not clear badges by itself.
- After the delay, clear eligible unread chat conversations for that account through the same source-bound read controller. Do not clear other accounts or supported notification/offer attention. A new incoming event gets its own full delay rather than being immediately cleared by an already-expired active timer.
- Use latest accepted incoming-event timing and a bounded per-conversation schedule. Do not issue a read every polling tick. Prioritize real sends, handoffs and incoming event delivery over bulk background read work.
- Expose both settings together under Settings → Chat and read behavior. Edit a draft; Cancel changes nothing, Apply affects the session, and Save host choices persists the choices. Existing viewer Preferences OK/Cancel and native settings remain intact.

Focused checks: visible/hidden/minimized/collapsed panels, reading history, inactive monitor Chat, overlapping surfaces, restrictions, duplicate requests, new events before/after acknowledgement, native catalog updates, timer switch/reset, delay boundaries and new-message grace. Update only affected portable chat/monitor/read models and protocol checks if the wire changes.

## 4. Give each character useful sound choices

### Independent categories

- Provide separate per-character checkboxes for **Direct IMs**, **Group chats**, **Conferences**, and **Supported notifications**. Turning off Group chats affects its host alerts, not its history, unread badges, conversation subscriptions or other accounts. Nearby remains silent as it is today.
- Preserve existing settings on migration: legacy Off stays Off; legacy IM enables the three IM categories; legacy IM plus supported attention enables those three and supported notifications. New profiles retain silent alert defaults; adding controls must not unexpectedly turn on sound.
- Treat host category settings as an additional filter on native eligibility. Continue honoring native group/conference/friend sound settings, DND, mute/volume, blocked senders, restrictions, self echoes, source generation, deduplication, freshness and burst suppression. Active characters retain their native alert producer; do not double-play native and host sounds.
- Offer these under each character's Sound and notifications page, accessible from Characters and Settings, with the account/grid identity clear. Category controls remain separate from the Attention list's display filters.

### Custom sounds

- Per character, offer Built-in tone, Choose sound…, Preview and Reset. Start with native-supported **WAV** files; report unsupported formats clearly rather than silently playing a different sound.
- Import a validated sound into a host-owned user-data sound folder and store a bounded relative identifier, independent of installation path. Validate RIFF chunk boundaries, supported PCM format, duration and size before playback; proposed limits are 4 MiB, 10 seconds, mono/stereo PCM 8/16-bit at 8–48 kHz. Never execute a file or fetch a sound from the network.
- Use the existing host volume setting for built-in and imported audio, without changing system-wide volume. Keep async playback buffers alive until playback ends; stop owned playback before replacing/freeing them. Bound the decoded cache and invalidate it on selection changes; avoid loading/decoding audio on the render/status polling path.
- Preview is an explicit action and uses the selected character's sound and host volume. Cancel does not change its assignment. An unavailable/corrupt saved sound reports a quiet actionable status and falls back to the built-in tone when otherwise eligible; it does not override mute or category Off.
- Keep alias/color, sound/category and read-policy storage versioned and transactionally parsed. Read old FSP1/FSX1 files; preserve every existing unrelated host choice, account identity, pin and rectangle. Back up before migration and write atomically. Audio bytes belong in the sound asset folder, never in a workspace or session-set record.

Focused checks: each topic alone, Group Off with Direct IM On, multiple accounts/grids, old-policy migration, replay/throttle/mute/restrictions, malformed/truncated/oversized WAV and unavailable assets, volume and selection/reset cancellation. Run affected alert/presentation/persistence/audio helpers only. Windows acceptance: distinctive sounds from two inactive characters, direct versus group messages, preview/reset, non-ASCII paths, restart/persistence and both dark/light themes.

## Shared boundaries and completion criteria

- Reuse the shared source-bound Chat model/controller for all surfaces. Never infer send/read ownership from an alias, slot label, active-world selection or management selection alone.
- Keep five-character handling, detached navigation, restriction redaction, uncertain sends, native UI/voice ownership, fitted hosting, profile isolation and Preferences OK/Cancel unchanged in behavior unless explicitly addressed above.
- Do not persist chat messages/drafts, Inventory contents, selections, filters or credentials. Diagnostic output contains state and identifiers only, never text contents.
- If rendered read watermarks require a protocol change, advance the private protocol once and document the matching host/viewer requirement. Do not promise compatibility with mixed binaries.
- Add focused tests for the changed helpers and actual reported failure cases. No broad viewer/host builds, packaging, GitHub builds, unrelated Go tests or subagents are needed for planning or default source validation. Native Windows focus, EDIT scrolling, IME, audio and native unread integration still require the user's matching build.
- Before publishing implementation, verify all six requested chat/notification improvements and the managed typing regression individually. Include actual checks and pending Windows acceptance in each relevant PR. Planning completion is not implementation completion.
