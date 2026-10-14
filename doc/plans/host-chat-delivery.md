# Host chat capacity, unread ownership and alert diagnostics

Follow-up above PR39 head `21d58240ab`, preserving its Lucide, placement, tooltip and scrollbar changes. Protocol 14 and the existing saved settings formats remain unchanged.

## Problem and resulting behavior

Viewer notifications shared the 64-conversation quota with Nearby and native IM/group/conference sessions. Notifications occupied quota even when hidden. The reported combination—native IM received, host only showing Nearby, a history-gap label, and many entries when Viewer Notifications was enabled—matches a production-model reproduction with 63 notices rejecting the next IM. A rejected chat also never reached the host alert gate.

Native IM floaters also retained their internal keyboard focus while their managed viewer was inactive. Incoming-message refresh used that logical focus to clear native unread counts before the worker sampled them. Turning off host automatic reading could not prevent that native path.

- Native conversations retain their 64-slot/512-line limits. Viewer notices have independent 64-slot/128-line limits, with oldest-notice eviction and removal of its notice lines. Notification pressure cannot evict native chat, drafts or close/restriction fences. Genuine event-queue/native-history gaps are still reported. Category visibility continues to affect display only; it does not mark notices read or accept offers.
- Managed native IM refresh/focus clears unread only for a ready, visible foreground viewer that owns active input and is not switching. Ordinary standalone viewers retain their native behavior. Explicit host Mark read and configured automatic reading retain their existing account/session fences.
- Host Preferences → Advanced → Diagnostics → Chat and alerts → Show report shows live counts, the most recently observed queue backlog, applied viewer alert policy, and the latest result per alert category. Results include worker-side DND/mute/system/active/restriction decisions, host-side category/freshness/burst/capacity decisions, and whether Windows accepted the playback request. No message text, sender UUIDs, transcripts or sound paths are included or written to disk. Results clear on account/login-generation change.
- Worker alert explanations use the existing bounded response detail field. Host sound choices remain independent of native friend/non-friend/group/conference sound switches; native master/UI mute, DND, restrictions, background ownership and burst limits remain. A playback request does not establish that a sound was audible. Maddy's separate silence still needs native acceptance; the report provides evidence instead of treating the capacity fix as proof that every audio failure is resolved.

## Focused checks

Seven affected portable checks passed using C++17 `-Wall -Wextra -Werror -Wshadow -pedantic -DNDEBUG -UNDEBUG`: chat_capacity, chat, chat_lifecycle, alerts, choice_state, read_policy and monitor_chat. The new test initially needed its explicit chat-model include; it was corrected and rerun successfully.

Coverage includes the exact 63-notice reproduction; hundreds of notices; independent line limits; fresh IM acceptance/alert eligibility with native unread zero; draft and close/restriction preservation; eviction/removal ordering; native-capacity limits and login reset. Existing receive restrictions, close fences, notice-gap cleanup, monitor/source ownership and read acknowledgements still pass. Added checks cover native foreground/read eligibility and alert decision reasons, including diagnostic detail round trips in the existing wire format. `git diff --check` passed.

Reviewed native IM focus/refresh callers, the worker's eligibility recheck, response detail bounds, playback buffer ownership, report constness and session reset. No viewer/host build, packaging, GitHub build, unrelated tests or subagents were run. MSVC compilation and Windows behavior remain pending.

## Windows acceptance

1. Rebuild and restart the matching host and viewer. Send fresh Sodie↔Maddy IMs with the recipient inactive and host automatic reading disabled. Verify both host conversation entries, live text and unread counts, including with the previously selected native IM window internally focused. Repeat Warm/Economy and shared/monitor Chat.
2. Hide Viewer Notifications and generate/retain many notices. Verify IMs still appear and alert. Temporarily show notices to check bounded retention/removal; host Read/Close must not accept native offers.
3. Enable per-character Direct IM alerts and use the selected WAV, host volume above zero, native master/UI unmuted and DND off. Leave native friend/non-friend IM sound switches off. Send messages five seconds apart while their recipient is inactive; check Preview and custom playback with monitor Chat shown/hidden.
4. If an alert is silent, open Advanced → Diagnostics → Chat and alerts → Show report immediately and capture the relevant Direct IM result. Check category Off, volume zero, DND/native mute, switching/active recipient and burst suppression produce useful explanations without message content. Switching account/login clears old results. Test native active/standalone chat reading, explicit host read, and the two optional auto-read settings separately.

Earlier live messages dropped by the old host are not replayed or copied from native history. Use new messages for acceptance; previous/native history remains in the owning viewer.
