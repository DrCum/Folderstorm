# Host friends, New IM and monitor tools

Implemented above PR #40 on `feat/host-friends-monitor-tools`.
Private protocol **15** requires matching host and viewer binaries.

## Completed

- [x] Ctrl+A selects all in shared, detached and monitor Chat composers and transcripts. Read-only history remains read-only; selecting history pauses automatic following.
- [x] New IM in shared and monitor Chat opens a character-bound friends/resident chooser. The sending account is shown explicitly. Open IM/double-click creates or reuses a native private conversation; sending remains a separate action.
- [x] Display names and usernames appear together. Friends sort online first, then alphabetically; offline friends remain eligible for IM.
- [x] Collapsible right friends sidebars in shared and monitor Chat, initially hidden, with filtering, status and an IM action.
- [x] Independent friends windows for each logged-in character, available through New IM or the sidebar pop-out button. Windows have the host theme, character accent, Always on top, keyboard navigation and window recovery.
- [x] Contacts/search results stay in memory. Worker, account, grid, generation and query tokens guard results and selections; teardown/restrictions clear stale information. A different panel's search invalidates the previous panel's results rather than silently changing its chosen resident.
- [x] Native start/send/receive and support-contact restrictions are rechecked when opening IM. Failed searches and refusals leave messages unsent. Older native Close events cannot reverse an explicit reopen.
- [x] Open all inactive monitors; Tile as grid and Arrange as row on the invoked screen's work area. Existing individual image size/rate and character exchange remain available.
- [x] Four fixed monitors plus one separate cycling window. Play/Pause, Previous/Next and an interval control; default 10 seconds, bounded to 2–3,600. Eligible inactive characters cycle in configured card order, without changing the active world.
- [x] Cycling clears/presents the previous image before changing the source label. It skips unavailable/restricted/active characters, suspends navigation during handoffs, Preferences and open selectors, and preserves source-owned drafts.
- [x] Duplicate views share one worker stream. The existing aggregate **10 FPS** preview cap, supported resolutions and conservative in-flight rate reservations remain. Hiding/minimizing windows releases unused capture policies.
- [x] Default “Pause to chat.” locks cycling Chat interaction. Advanced → Require pause to chat can relax this; entering the composer then pauses before typing, with explicit Play to resume. Cycling does not automatically mark its changing Chat read.
- [x] Interval/safety/pins/placements use existing transactional Preferences and autosave. FSX1–FSX3 choices migrate to FSX4 with default safety enabled. Named session sets continue capturing only the four fixed assignments; contacts, searches and drafts are never stored in them.

## Implementation limits

Contacts are bounded to 2,048 friends and 100 search results per character; fixed UTF-8 IPC pages remain under 3,072 bytes. Only visible contacts surfaces refresh friends, at five-second intervals. Direct IM visibility must be enabled to open an IM in host Chat; a visibility change while opening never redirects the destination to Nearby. Resident-name search uses the native `AvatarPickerSearch` capability with a 20-second timeout and one outstanding HTTP search per worker. Regions without that capability report unavailable search; friends still work. Name-cache misses show “Name loading” with a unique resident ID until the next refresh. A new query attempted while HTTP is pending reports that it is still running and can be retried.

No credentials, conversations, Inventory contents, filters, selections or search results are added to persisted profiles. The interval, safety choice, window placements and pins are ordinary host preferences. Opening IM does not promote a background character, leave a group, terminate a call or replay an uncertain send.

## Actual focused validation

Compiled and executed these twelve production-helper checks with GCC C++17, `-Wall -Wextra -Werror -pedantic -UNDEBUG`:

`contacts`, `monitor_tools`, `chat_lifecycle`, `monitor_chat`, `monitors`, `protocol`, `experience`, `preferences`, `presentation`, `preview`, `sets`, `chat_behavior`.

Checks cover online-first ordering/offline eligibility, bounded UTF-8 pages, stale worker/account/grid/generation/query rejection, pending/error searches, explicit reopen/older Close ordering, cycling order/timing/typing pause and retained drafts, negative-coordinate screen tiling, duplicate-source budgets, fixed-monitor swapping, settings migration and transaction rejection. `git diff --check` passed. Reviewed native name-cache/search/IM APIs, window/source lifetimes, message routing, painting and rate admission.

No native Windows compiler/runtime was available. No viewer/host build, installer/packaging, GitHub build, unrelated Go tests, broad suite or subagents were run. Native compile/link and graphical behavior remain pending acceptance.

## Windows acceptance

1. Build/install the matching protocol-15 host and viewer. Start two, then five characters. Confirm older/mixed binaries refuse the private protocol cleanly.
2. In shared Chat, select the sending account, choose New IM, filter friends, search a resident name and open a result. Check display-name/username disambiguation, online-first ordering, an offline friend, existing/closed conversations, and explicit Send. Verify the target native account receives the outgoing message and another account never sends it.
3. Repeat from detached Chat and every monitor. Toggle the right sidebar; pop it out for multiple characters. Check theme/character colors, Pin, Tab/Shift+Tab, filtering, resizing, 100/150/200% DPI, and recovery onto reachable work areas.
4. Search from two panels for the same character. Start a new search, disconnect/restart/log out, and change restrictions while requests are pending. Old results/selections must clear or report a refusal; no stale account sends. Check a failed/unavailable search and normal recovery through another query.
5. With five logged-in characters, Open all inactive monitors. Add the cycling monitor while four fixed views are open. Tile on each screen (including negative desktop coordinates), then arrange as row. Size/rate/pin choices and explicit exchanges should remain intact.
6. Cycle at 2, 10 and 3,600 seconds; reorder character cards; pause, go backward/forward and resume. Disconnect or promote its selected character. Confirm the active world never changes automatically, and an old image is cleared before the new character's label appears.
7. Leave default safety on: cycling Chat says “Pause to chat.” and cannot send/change/mark read until paused. Save drafts under two accounts/conversations, navigate and return; each draft stays with its owner. With safety off, focus the composer and type immediately: rotation must pause first. Play must resume explicitly.
8. Show the same character in a fixed and cycling monitor. Check one native preview stream and total acknowledged/pending preview rates at or below 10 FPS; minimize/hide/close views and check release. Watch for flicker, stale images and source-label mismatch.
9. Ctrl+A, copy/paste, Enter to send, Shift+Enter and IME input in each Chat surface; history selection must preserve reading position. Reopen the host and check interval, safety, pins and placements. Verify Preferences Cancel discards edits and Apply autosaves them.
