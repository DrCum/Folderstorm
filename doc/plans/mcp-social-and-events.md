# MCP Chat, contacts and events

Depends on E0 in [the roadmap](mcp-expansion-roadmap.md). Deliver as E1 social reads, E2 explicit writes, E3 event polling. No navigation or building APIs.

## Native service and source ownership

Extract viewer-owned Chat/contact/notification services from `fssessionworker.cpp`, retaining its host IPC adapter. Existing code uses LLIMModel, FSNearbyChat, LLAvatarTracker, LLAvatarNameCache and AvatarPickerSearch; profile lookup uses LLAvatarPropertiesProcessor. Ordinary viewers and managed workers use the same service. MCP must not borrow host selected-account state, host transcript, FSC1 contact text pages, unread presentation or the host's event cursor.

Every operation binds exact account/grid/viewer login and bridge epoch. Resident selections use resident UUID plus grid; conversations use native identity/topic and participant metadata. Display name and username are both shown when available. Restriction/name visibility checks run before delivery, including for retained data. Searches/profile fetches have guarded callbacks, bounded deadlines and no retained raw Worker pointer across yields.

## Surface and permissions

Planned names are contract proposals to finalize in E0:

- `chat_conversations`, `chat_history`, `chat_search`: loaded, session-memory history only. Pagination reports retained bounds, truncation and gaps. Searching never silently scans disk logs. LLIMSession.loadHistory can load disk transcripts into mMsgs when LogShowHistory is enabled, including when opening an IM. Maintain explicit current-login live-message provenance and exclude cached/prior-login history; do not assume in-memory means live-only or change native history preferences. Separate `chat_read`, initially opt-in.
- `friends_list`, `resident_search`, `resident_profile`: online friends first, then case-insensitive display/username order with UUID tie-break. Offline friends remain messageable. Preserve search limits: 2–127 UTF-8 query bytes, at most 100 results and one actual outstanding request per viewer (including discarded requests); reuse the native 20-second HTTP timeout. Snapshots are paginated and use the native 2,048-friend ceiling. Profiles expose a bounded public/native-visible field allowlist, not private notes. Separate opt-in `friends_read`, `resident_search` and `profile_read`.
- `chat_open_im`: resolves an exact resident selection and opens/creates the native conversation without sending. Does not infer a target from an ambiguous name. `chat_state` permission, no implicit promotion/focus.
- `chat_draft_set/get`, `chat_send`, `chat_mark_read`, `chat_close`: draft storage is in-memory and source/conversation bound; host/monitor shared drafts require expected revisions to avoid overwriting typing. Send is explicit even after selecting a resident. Separate Ask-capable `chat_send_im`, `chat_send_group`, `chat_send_conference`, `chat_send_nearby`; `chat_state` governs read/close changes. Draft get requires chat_read even when chat_state permits mutation. Read/history calls do not implicitly mark read. Close hides the conversation in assistant/shared-host surfaces without leaving a group, ending a call or deleting native history; ordinary-viewer MCP keeps an equivalent assistant visibility state. Nearby cannot close. Genuine new incoming messages reopen; old catalog/queued/outgoing events do not. Reuse monotonic close fences.
- `notifications_list/get`, `notification_review`: supported native-visible notices/offers with identities, descriptions and current lifecycle. Opt-in `notifications_read`; explicit UI review is separately approved and may require active ownership. Review opens native UI; it never accepts inventory, permissions, teleport offers or other response buttons.

A send request freezes exact sender, grid, conversation/topic, participants and text before native Ask. Recheck session readiness, RLV sender/group/conference restrictions and FSData support-contact restrictions immediately before transport. Text is data: do not evaluate viewer chat-bar commands; support emotes only through an explicitly audited normal-chat path. Respect native mute/DND policy where applicable without making native sound debug preferences an MCP prerequisite. Apply native message length/channel limits, conservative burst limits and no automated bulk broadcast.

Return request/operation ID, submitted versus refused/uncertain state and native message identity where available. Native acceptance does not prove recipient delivery. Lost acknowledgment or disconnect never triggers automatic resend. Draft remains available after refusal; no synthesized outgoing history line claims delivery.

## Events

Use independent per-viewer readers for Chat, contacts, supported notifications, operation results and permitted session-status changes. Other new domains may add typed event topics later. Authorize each topic using its underlying read permission; event subscription is not a permission bypass.

Start with `events_poll`, request/response with optional bounded wait, rather than unsolicited push. No outbound service or background notification sending is implied. A first call explicitly chooses now or the retained window; snapshots are separate. One client's poll must not consume another's data, alter unread counts or change host UI filters.

Define opaque cursors bound to viewer_session, stream, filter and authorization epoch. Bound reader count/TTL, event retention, payload bytes, page size and wait time; advertise actual limits. Separate `next_cursor` (only scanned/delivered entries) from `latest_cursor`. Reject cross-login/future/unknown cursors. Existing EventBuffer.after returns one event while payload.cursor is the latest watermark: never use that latest value as the returned continuation or buffered messages will be skipped.

Overflow and privacy flush return explicit gaps and snapshot reconciliation instructions. Keep event IDs monotonic within a login even when privacy changes; do not silently restart at zero. Never expose restricted content or counts through another topic's sequence. After a notification gap, reconcile active IDs/removals so expired notices do not remain unread. Permission revocation clears private retained payloads and invalidates reader cursors; re-enabling does not revive old authority.

## Checks and native acceptance

Test exact source/schema dispatch and independent clients; duplicate names and offline friends; stale/overlapping searches/profile fetches; support contact and group receive/send restrictions; retained data becoming restricted; changed Ask text/destination; queue pause/revoke; unknown send outcome and dedup expiration. Exercise draft conflicts and close → older native close → queued message ordering.

Cursor tests cover full pagination, repeated polls, retained edge, overflow, privacy flush, filtered events, changed permissions, future/cross-login cursor, independent readers, notification removal and no read-state mutation.

Windows: IM/Group/Conference/Nearby under two/five characters, background modes, visible/hidden host Chat, native and MCP drafts, native restrictions, native close/removal, unsupported profile/search capabilities and disconnect during sends. Verify ordinary viewer operation and existing host Chat/sounds still work.

## Implemented checkpoint

E1–E3 are delivered together because live message provenance, private event cursors and close fences share native callbacks. Source reviewed independently with Sol 6.1 Max through `8c8d963baa`; all confirmed findings were corrected. Six affected portable C++ checks, ten native-boundary source checks and registered Go Social dispatch checks pass. Native Windows compile/runtime acceptance remains pending.

History covers live callbacks observed by this service, never disk transcripts. Polling is immediate (`wait_ms=0`); four topic rings retain at most 512 events/512 KiB each. Cursor pages are bounded to 64 KiB; 128 opaque cursors expire after five minutes. Friend/search snapshots are source-bound and short-lived. Sends are limited to 1,023 UTF-8 bytes and report native submission, never delivery. Assistant drafts and visibility are independent of native composers. Only supported offers can be reviewed in native UI; no response is accepted automatically.

The checkpoint also preserves native-readable v1 notecards, checks COPY/MODIFY and native viewing restrictions before and after content downloads, and keeps bounded returned prefixes valid UTF-8. Lossless source editing is a separate checkpoint.

Profile-fetch follow-up: HTTP pending requests and load generations now include their captured assistant authority. Opening a native profile during an MCP lookup starts an independent native fetch; revoking MCP cancels only its fetch/delivery. Captured-key cleanup cannot consume the native request. Native acceptance must cover both request/completion orders, revocation while loading, duplicate callbacks, and a timed-out fetch replaced by a newer generation. Account/login checks remain enforced before transport and delivery; legacy UDP behavior is retained.
