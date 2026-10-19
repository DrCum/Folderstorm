# Velopack shortcuts and Folderstorm support links

Follow-up above PR #71 on `fix/velopack-install-support`. Published history and
other worktrees are preserved. No release, installer or application was deployed.

## Behavior

- Viewer Help/Crash reporting, host Support and the login-page report action open
  the public Mantis tracker at https://folderstorm.com/bugs. No account/build data
  is added to the URL and no report or attachment is submitted automatically.
  GitHub Issues remains available for development tracking.
- Configured Sentry builds offer a one-time crash-reporting invitation from the
  first interactive viewer/host launch. Enable and Not now both persist; prior
  consent is preserved and managed workers do not prompt. See
  `sentry-validation.md` for the consent/privacy and acceptance cases.
- The Phoenix login splash's Firestorm latest-version panel becomes Folderstorm
  updates, with a user-click-only native update check and the published release
  list. Its Firestorm JIRA button opens the same Folderstorm report action.
  Rewrites are limited to the actual upstream host/path; repeat DOM updates are
  idempotent. Grid-supplied and custom login pages are preserved.
- The NSIS coexistence notice names the separate installation without claiming
  that it is out of date or requiring removal. Startup no longer deletes or
  retargets that installation's shortcuts. It is independent of the release feed.
- A Windows Velopack installation offers a native shortcut chooser on the first
  interactive viewer or session-host launch. Four independent choices cover
  Desktop/Start menu × viewer/session host. This is intentionally outside SDK
  install hooks and managed worker startup, so updates cannot block on that UI.
  Help → Desktop and Start menu shortcuts and host Preferences → Support → Crash
  reports → Application shortcuts reopen it later.
- Choices are versioned, bounded and stored per package variant in
  HKCU/Software/Folderstorm/Install/<app ID>. A named mutex prevents competing
  viewer/host prompts. First-run Not now remembers no shortcuts; later Cancel
  preserves existing choices. Updates reapply selected choices on the first
  interactive launch of the new source commit, without asking again.
- Shortcut names distinguish flavor/channel/CPU. Targets use the stable root
  launcher when available (otherwise current/viewer) and current/session-host.
  Icons and working directories come from the matching current binaries. Only
  recognized shortcuts to that installation with empty arguments may be replaced
  or removed; other installations and customized account/profile links are kept.
- Uninstall removes only those owned shortcuts and invalidates the applied-commit
  marker while retaining choices, so same-build reinstall recreates selected
  entries. Older `VlpkFolderstorm...` uninstall records are removed only when
  their complete uninstall command matches this installation’s Update.exe.
  Early SDK hook executable lookup
  uses GetModuleFileName, not the not-yet-initialized viewer directory service.
  The uninstall registry identity uses the SDK's full variant app ID, and support
  metadata points to Folderstorm. SDK hooks no longer rewrite Firestorm settings.

## Focused checks

- Strict C++17 install-policy checks: all four-choice round trips,
  corrupt versions/masks, link ownership/conflict decisions, legacy registration
  name/ownership guards and safe executable filenames.
- Extracted actual splash JavaScript syntax and behavior checks: latest-version
  replacement, absolute/relative JIRA link rewriting, lookalike-host exclusion
  and repeated DOM updates without further mutations.
- XML/native wiring checks for new menu/notice names, report URLs, managed worker
  suppression, hooks and removal of legacy shortcut/settings modifications.
- A configure-only CMake check confirms viewer plain/host keyword linking
  signatures and the Windows shortcut libraries for both targets.
- The affected fsupdate.cpp runtime compiles and links into a Win64 test binary
  using MinGW with C++17, -Wall -Wextra -Werror -pedantic and the viewer's legacy
  Windows API visibility defines. Changed early-hook function bodies also pass
  a syntax check against real Windows headers. These binaries were not run.
- No broad viewer/SDK, packaging, GitHub build or unrelated tests were run.

Commands from the source root:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -Iindra/newview \
  indra/newview/tests/test_fsinstallpolicy.cpp -o /tmp/test-install-policy
/tmp/test-install-policy
FS_MINGW_CXX=/path/to/x86_64-w64-mingw32-g++ \
  python3 -m unittest discover -s indra/newview/tests -p test_fsinstallsource.py -v
git diff --check
```

Without Node/CMake/MinGW the corresponding optional source tests skip; the portable
policy test remains registered in the standalone CMake regression project.

## Public tracker and Windows acceptance

The maintainer selected self-hosted Mantis for public/anonymous reports after the
initial GitHub setup. All primary report actions now use the supplied tracker URL.
GitHub Issues is retained as a development option; reporting no longer depends
on the GitHub issue-template/default-branch setup. Sentry opt-in crash reporting
and release/download links remain separate.

Native Windows acceptance remains pending:

1. Fresh Velopack install: chooser appears once from either viewer or host;
   Desktop/Start menu × both apps honor all selected/cleared choices. Close/Not
   now and later Help/Support editing work, including keyboard traversal and DPI.
2. Click each new shortcut, confirm the matching package variant runs, relaunch
   and upgrade without another prompt. Check viewer and host concurrently, a
   custom profile shortcut and a separate NSIS/other-channel installation.
3. Uninstall/reinstall the same build; saved selected shortcuts must be recreated
   without another prompt, and only matching owned shortcuts/legacy uninstall
   entries may be removed. Keep native
   settings and persistent host choices. Verify Windows Apps identity/version.
4. Each native and login report action opens https://folderstorm.com/bugs without
   query metadata. Verify the anonymous reporting flow in a signed-out browser.
   Opening it must not submit or attach private data.
5. Login splash: Folderstorm update check uses the native configured feed;
   release metadata and current version remain distinct, including with no
   published releases, custom grids, resizing and upstream DOM refreshes.
6. Verify the NSIS notice is informative, does not open an SL uninstall guide,
   and leaves its shortcuts/files/settings intact. Recheck crash consent and a
   controlled Sentry crash independently of bug-report issue creation.
7. With no earlier consent, launch the configured viewer or host: Enable opts in;
   Not now, Escape and Close remember Off. Restart/upgrade must not repeat the
   invitation. Preserve earlier explicit On/Off choices, suppress worker prompts
   and duplicate simultaneous prompts, and allow changing the choice later.
