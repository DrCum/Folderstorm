# Responsive first-launch setup

Follow-up above PR #73, preserving #72 and #73's published history. The reported
#72 installer automatically launched a white viewer with the shortcut dialog
visible but unable to process clicks, Escape or Alt+F4. Hovering its system close
button still drew the Windows frame effect; closing the viewer eventually caused
Windows to offer force termination.

## Cause and implementation

The new `LLAppViewer::init()` path called `DialogBoxIndirectParamW` synchronously
before returning to the viewer's frame loop. That nested native loop does not run
`LLWindowWin32::gatherInput()`. The viewer's separate window thread forwards input
and timer callbacks through bounded, blocking queues (1024 entries by default).
Blocking initialization leaves those queues undrained and can block the window
thread too. A Windows title-bar hover effect is not evidence that its application
message loop is making progress. The crash invitation used the same blocking
startup path. A live Windows debugger/backtrace was not available here.

Plan implemented:

1. Remove both modal calls from viewer initialization. Keep Sentry initialization
   and existing consent synchronization; reporting still defaults to Off.
2. Offer setup from `idle()` only at the interactive login screen or after a
   completed automatic login. A viewer-native floater handles both questions,
   one at a time. Rendering, input queues and networking continue normally.
3. Route Help's manual shortcut chooser through that same nonblocking floater.
   Apply saves its four explicit choices; Not now/Escape/Close preserve previous
   choices, or record no shortcuts on first use. App shutdown saves no answer.
4. Preserve the shared, bounded registry/file formats and per-install shortcut
   ownership checks. Deferred prompt transactions retain the nonblocking named
   mutex lease until response or teardown. Host UI uses the same preparation and
   commit logic with its existing native dialog, on the host's UI thread.
5. Retain explicit Enable/Not now crash decisions, default Not now, saved-choice
   suppression, worker suppression and newer-consent precedence. Skin creation
   failures and native floater restrictions release leases without recording a
   decision. Temporarily hiding an open question is not a dismissal.

No protocol, installer format, consent format or shortcut identity changed in
this follow-up. Its #73 base already requires matching protocol-17 binaries.

## Focused validation

- Execute actual viewer setup methods with small LLUI/platform stubs: pre-login,
  auto-login progress, worker/quitting suppression; 5000 continued polling frames
  with a question open; checkbox Apply; Help preserving an open draft; first-use
  and later cancellation; consent Enable/default decline; hidden UI; blocked
  native floater opening; teardown without recording a decision; save errors.
- Execute the actual deferred consent and shortcut transaction bodies with small
  Windows/storage stubs: concurrent chooser suppression, lease release on every
  exit, already-saved decisions, stale consent response, cancelled questions,
  upgrade reapplication, invalid choice masks and failed shortcut writes.
- Strict C++17 portable install/crash-policy tests, XML/wiring checks, existing
  affected Windows fsupdate compile/link and early-hook checks, and fscrash
  enabled compile/disabled link against real Windows/pinned Sentry headers.

These tests do not execute Win32 UI, a viewer, the Sentry SDK or an installer.
Native MSVC compilation and graphical acceptance remain pending. No broad viewer,
SDK, packaging or GitHub builds were run.

```sh
python3 -m unittest discover -s indra/newview/tests -p test_fsstartupprompts.py -v
python3 -m unittest discover -s indra/newview/tests -p test_fsinstallsource.py -v
python3 -m unittest scripts.tests.test_folderstorm_sentry_build -v
```

## Windows acceptance

1. Install/launch with no saved shortcut choices. The login screen must render
   before the themed chooser appears. Leave it open for at least two minutes:
   its checkboxes/buttons, Tab, Space, Enter and Escape must remain responsive.
2. Apply a mix of Desktop/Start menu choices; verify both viewer and host links.
   Reopen from viewer Help, change choices and repeat. Cancel must preserve
   previously saved choices. Start another matching viewer/host while a question
   is open: there must be no competing chooser and neither app may hang.
3. On a Windows user with no crash decision and a configured Sentry build, finish
   shortcuts then test Enable and, in a separate clean test profile, Not now/
   Escape/Close. Off must be the default. Reopening/upgrading must not repeat a
   saved decision. Existing On/Off choices and managed workers must not prompt.
4. Close the viewer while setup is open; it must quit normally. Restart must be
   possible without a stuck prompt/lease. Test automatic login, UI hiding,
   larger text/DPI and the default/alternate skins. Restart/upgrade with saved
   shortcut choices should reapply links without asking again.

Use a disposable Windows test user/profile for pristine prompt cases; do not
remove the user's existing preferences or consent to force a test.
