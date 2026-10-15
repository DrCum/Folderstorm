# MCP session controller and monitor tools

E9, depends on E0 and extraction of typed controller commands from tools/session-host/main.cpp. This is a separate authority surface from each character's viewer MCP connection. No credentials, automatic login completion, navigation or building.

## Controller-owned transport

Worker APIs cannot safely control logout/restart or unlaunched profiles: the connection disappears, and account credentials must not become global host authority. Add a current-user-only, authenticated controller endpoint with its own discovery namespace, random controller-instance epoch and independently managed opt-in permissions. Reuse proven discovery/ACL patterns, bounded local framing/HTTP and loopback-only scope; do not expose worker pipe secrets, arbitrary executable/profile paths, process handles or window messages.

Bind every command to controller_instance and stable profile ID/revision. Running-character changes additionally require expected worker identity/generation and exact viewer_session. Slot order, names, active selection and PIDs are display hints only. Stopped profiles have no login reference and use their profile identity/revision; do not fabricate one. Controller access cannot silently enable worker MCP or unpause denied account permissions.

Authorize in the controller and marshal typed commands onto its UI thread. Host responses never leak bearer tokens, password data or account-private Chat through session metadata. No generic host UI automation. The host accepts only its configured matching viewer binary and existing profile lease/state machine.

## Tools and choices

- `sessions_list/status`: configured profiles permitted by controller access, actual logged-in identity when permitted, active/background/disconnected/restart state, existing resource diagnostics. Opt-in metadata permission. Diagnostic freshness and unavailable fields are explicit.
- `session_activate`: explicit targeted handoff, reusing native serialization, input/microphone ownership and optional visual transition policy. Separate Ask-capable activation permission; never run as a side effect of reads, sends or monitor operations.
- `session_background_mode`: explicitly set Warm/Economy on an eligible inactive worker. No hidden promotion or ownership change; return Busy/RequiresInactive where appropriate. Ask-capable mode permission.
- `monitor_open/close/list`, `monitors_open_all/arrange`, `monitor_cycle_configure/pause/next`: reuse existing read-only low-resolution monitors, shared streams and render budget. Changes affect previews only, never avatar selection or active character. Preserve source-bound monitor Chat and pause-before-chat rules, stale-image clearing and DPI minimum-size validation. Ask-capable monitor-layout permission. No raising preview quality/FPS beyond configured bounds.
- `session_restart`, `session_logout`, `session_detach`: independent explicit lifecycle operations with separate Ask-capable permissions and action summaries. Restart is native close → release profile lease → open login window. Native logout cancellation, dialogs, delays and uncertain acknowledgments remain authoritative; no force termination, uncertain retry or automatic replay of messages. Detach is not logout and cancels pending relaunches. A disconnected profile may relaunch only through its current controller-owned recovery state.
- If launch is included, `session_open_login` opens the configured profile's native login window. It does not accept passwords, fill credentials, approve login, bypass duplicate-account reservation or enable Local Assistant. Native host approval must work even when no worker is connected.

Controller permissions and approvals are host-owned, not self-authorized by a worker/account. A worker's existing grant does not authorize control of other characters. New controller capabilities must be explicitly enabled. Store host choices with existing stable autosave/profile naming rules and native Apply/Cancel; do not write private viewer settings files.

## Transactions and safety

Lifecycle operations return controller-owned receipts with source profile/worker generation, phases and known/uncertain outcomes. Keep the receipt after the old viewer exits. If the host instance changes, reject the old receipt/capability; do not turn a retry into a new restart. One lifecycle owner per profile and one active-character handoff owner.

Reuse current handoff/modal/closing guards, restart cancellation, profile leases and mismatched-binary refusal. Pause-all revokes pending account work before any new side effect, but resuming an account remains an explicit native action outside MCP unless separately reviewed. Losing the controller releases ownership through existing recovery; no orphan input/microphone owner. Do not automatically restart on a disconnect event.

## Delivery and checks

Root owns typed command extraction, endpoint authorization and lifecycle integration sequentially. A read-only Sol 6.1 High agent can audit Win32/native paths; no parallel main.cpp writers, especially with UI #50. Final independent Sol 6.1 Extra High review is mandatory; Max is appropriate for complex cross-controller authority/lifecycle review. Never Ultra.

Test wrong controller epoch, invalid/expired token, profile reorder/rename, slot reuse, worker replacement, opt-out profiles, absent worker login, queued revoke, conflicting handoffs, modal/native close cancellation, stuck process/lease timeout, detached worker, stop-managing with pending restart and no uncertain replay/force kill. Monitor tests cover budget sharing, cycling Chat and layout refusal before moving any windows.

Windows: two/five characters, Warm/Economy, host restart/controller loss, background focus/IME, native logout cancellation, profile recovery and monitor layouts at mixed DPI. Verify lifecycle operations never grant themselves Chat access or alter voice/input ownership outside the native handoff rules.
