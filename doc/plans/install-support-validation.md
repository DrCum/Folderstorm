# Velopack shortcuts and Folderstorm support links

Follow-up above PR #71 on `fix/velopack-install-support`. Published history and
other worktrees are preserved. No release, installer or application was deployed.

## Behavior

- Viewer Help/Crash reporting and host Support use one prefilled GitHub issue URL
  with build-only environment information. It does not require an issue template
  to exist on the default branch and never submits a report or attaches files.
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

- Strict C++17 install-policy/report URL checks: all four-choice round trips,
  corrupt versions/masks, link ownership/conflict decisions, legacy registration
  name/ownership guards, safe executable
  filenames and escaped prefilled report URLs with no template dependency.
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

## External setting and Windows acceptance

GitHub returned `has_issues: false`. The connected integration's attempt to enable
it was rejected with HTTP 403, Resource not accessible by integration. A repository
owner must enable Settings → General → Features → Issues. Until then GitHub will
reject report links regardless of which viewer build is used. No alternate
credentials, permission escalation or issue creation was attempted.

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
4. With Issues enabled, each native and login report action opens a reviewable
   GitHub draft. Report actions must not submit or attach private data.
5. Login splash: Folderstorm update check uses the native configured feed;
   release metadata and current version remain distinct, including with no
   published releases, custom grids, resizing and upstream DOM refreshes.
6. Verify the NSIS notice is informative, does not open an SL uninstall guide,
   and leaves its shortcuts/files/settings intact. Recheck crash consent and a
   controlled Sentry crash independently of bug-report issue creation.
