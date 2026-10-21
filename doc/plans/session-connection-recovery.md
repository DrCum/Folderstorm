# Recover temporary session-control stalls

Follow-up above PR #72; earlier build heads remain unchanged. The user reports
occasional healthy logged-in viewers being returned to standalone windows, with
only Restart available. The exact incident has not been reproduced on Windows.

## Confirmed source paths

The old host closed the pipe after a 12-second Ready reply timeout (60 seconds
during login). The worker also detached and closed it after 15 seconds of command
silence. A temporary blocked loop could therefore permanently end management of
a healthy account. The original inherited capability could not be reopened.

Additionally, promotion failure closed a healthy pipe even after an authenticated
refusal, including native modal/Preferences checks. An ordinary refused switch
could become an unnecessary full detachment.

## Implemented plan

1. Treat elapsed reply time as a stalled link, retain its authenticated pipe and
   outstanding request, and display **recovering control**. Log the request kind,
   sequence, generation, PID, slot and elapsed time; never log its payload.
2. On command silence, the worker revokes held input and microphone ownership,
   cancels its camera/transition leg, enters Warm and refuses actions. It stays
   managed; saved preferences, private profile, session and chat listeners remain.
   A pending mode reply is completed with an explicit paused error. No old
   promotion can subsequently grant input.
3. Drain the original response once without applying delayed host actions. Drop
   unsent queued sends, typing, actions, contacts, reviews, restart/launch intent
   and session-set automation. Keep canonical drafts. A timed-out send remains
   uncertain and is never resent or silently removed from its draft.
4. Send a fresh account/grid/generation-bound Recover request over that same pipe.
   This is an idempotent revoke/reset to Warm, never a grant. The host requires
   its exact sequence and identity, a non-error acknowledgement and no remaining
   managed input/voice/promotion/embedding before clearing quarantine.
5. Block promotions while another recovering link could still grant stale input.
   Cancel related handoffs and quarantine their outstanding legs. A formerly
   active same-login character can return only through the ordinary acknowledged
   first-frame handoff. Newer character choices and native restarts take priority;
   changed logins never inherit the old activation intent.
6. Recover refused promotions without discarding their pipe. Restore the prior
   character only if its captured generation is still current. Bringing a
   character forward can expose its open Preferences draft; hiding it while
   Preferences is open still requires the user to finish that draft. OK/Cancel
   is never applied automatically and native focus-lock/modal guards remain.
7. Preserve explicit lifecycle actions. Stop managing/Keep logged in can abandon
   a stalled capability immediately, so closing the host does not wait forever
   for an unavailable loop. Quit all still requires native logout. Already
   accepted native Quit/Detach may complete, but uncertain closes/restarts are
   not replayed. Disconnected recovery UI remains visible and usable.

The private worker protocol is **17**. Rebuild matching host and viewer. Mixed
versions are rejected. No host preference migration or avatar movement is added.

## Diagnostics and limits

Windows writes UTC transition records to
`%LOCALAPPDATA%\FolderstormSessions\connection-diagnostics.log`, rotating at 1 MiB
to `connection-diagnostics.previous.log`. Viewer logs also record pause/reset or
actual pipe closure under `SessionConnection`. The host records no account names,
grid, chat, credentials, worker capability IDs or request text in this log.

This recovers temporary stalls and refused switches while the original capability
exists. A genuinely broken pipe, invalid peer/sequence, terminated viewer or
already detached viewer from an older build still needs the explicit existing
restart/standalone path. It does not adopt arbitrary windows or recover across a
host restart. A permanently hung worker cannot grant a replacement input owner;
the host does not kill it or guess that an outstanding action failed.

## Focused checks performed

- Six strict C++17 portable tests passed: connection, protocol, controller,
  monitor_chat, registry and transition. They use `-Wall -Wextra -Werror -Wshadow
  -pedantic`, with assertions enabled for Release.
- The new connection target configured, compiled and passed through Release
  CMake/CTest. It checks deadlines/clock underflow, late replies, reset identity,
  source changes, denied grants, private-draft retention, protocol round trips,
  Preferences policy and payload-free diagnostics.
- Two source-extracted C++ harnesses execute the actual affected worker command/
  pause/reset and host quarantine/failure/stop-management methods. They cover
  refused promotion, both pending switch legs, receipt uncertainty, cancelled
  restarts, stale login, stopped management, rejected stale sends, failed reset,
  disconnected-window recovery and invalid peer guards. Minimal UI stubs do not
  establish native Windows behavior.
- The complete affected host translation unit passes MinGW Win64 C++17 syntax
  checking with real Windows headers and generated build configuration. This
  does not link or execute the host, viewer, SDK or installer.
- `git diff --check` passes. No broad viewer, packaging, GitHub build or unrelated
  Go test was run. No native Windows runtime acceptance is claimed.

## Windows acceptance

1. With matching binaries and two test characters, pause one viewer's loop in a
   debugger for more than 16 seconds, then resume. Confirm the host stays usable,
   shows recovering rather than Restart, keeps the login and resumes hosting.
   Repeat with the host's loop paused and with Warm/Economy background accounts.
2. Pause during revoke and during first-frame promotion; choose a different
   healthy character while recovering. Verify input and microphone never belong
   to two managed characters and recovery does not override the newer selection.
3. Keep a private IM draft in shared and monitor Chat during the stall. Verify
   editing/sending pauses without changing sender or destination, history/draft
   survives, and resumed input cannot become another account's Nearby chat.
4. Stall with an in-flight send or restart. Check native delivery before any
   deliberate resend; confirm no duplicate send, automatic relaunch or delayed
   action is produced by recovery. Test an account/session change before reset.
5. Exercise refused switching, open native Preferences, OS dialogs and a cancelled
   native logout. Verify the account remains manageable and drafts are preserved.
6. Close the host using Keep logged in while stalled; it must exit and the viewer
   must return to ordinary operation when its loop resumes. Quit all must retain
   native approval/cancellation and never force termination.
7. Test actual process/pipe loss separately. It must report unavailable control
   and use the honest standalone/restart path, with no arbitrary process adoption.
8. Inspect rotated diagnostics for the correct PID/slot/sequence/timing and lack
   of chat/account payload. Verify ordinary switching, chat, monitors and camera
   restoration remain correct after recovery.
