# Selective main-profile copying

Implemented 2026-10-11 on `feat/session-profile-copy`, based on unmerged PR #29 (`feat/session-host-design`, `a588a6adee`). Existing branches/build archives and published history remain untouched. This is source implementation completion; Windows acceptance is pending.

## Completed

- Once-per-private-profile first-open prompt: Copy from main profile, Keep existing settings/defaults, Cancel. The versioned marker is saved atomically only after successful process launch. Cancel/failure does not consume it. Existing profiles get one offer after upgrading, without resetting them.
- Later access under both Characters and Settings, naming the selected management target. Running or leased profiles remain protected; accepted copying opens normal login.
- Independent graphics, interface/skin, colors, keyboard bindings, quick preferences and toolbar checkboxes. Main installation is the default source, with a browse option for another installation or native backup. Toolbar-only/file-only copying does not rewrite global controls.
- Explicit source account selection for saved toolbars, bounded directory discovery, UTF-16 filesystem paths, absolute inherited sources and validated selection masks. Selective requests use a new source variable, so an older worker cannot turn a toolbar-only choice into its legacy all-preferences copy. Source choices are not stored in session sets or named workspaces.
- Source toolbar snapshot held in memory before login; applied once to the first successful account immediately before normal toolbar loading. Native parameter validation/canonical output, registered commands only, duplicates/bounds checks, alignment/display/layout preservation and first-run reset handling. Pending data is discarded on detachment.
- Global preference/UI and per-account toolbar backups before atomic replacement. Security/automatic-action policies, warning choices, credentials, account data, conversation/Inventory contents, workspaces/boards, filters/selections and caches retain their existing isolation.
- Setup-modal reentrancy guard, continuing worker servicing, management epoch and session-set cancellation/expiry guards. Active host-panel ownership supports popped-out/pinned controls. Preferences OK/Cancel remains unchanged because copying requires a closed destination viewer and runs at startup.

## Actual focused checks

- `profile_seed` passed directly under C++17 and C++20 with `-Wall -Wextra -Werror -Wshadow -pedantic -DNDEBUG -UNDEBUG`. Tests cover every valid selection mask, independent graphics/interface/file/toolbar groups, native security-policy exclusions, unknown files, malformed/overflowed mask parsing without partial output changes, and rejecting DTD/entity/non-UTF-8 XML text. Failed reads clear any pending bytes before native parsing.
- `controller` and `sets` passed under the same strict C++17 flags, covering the related lifecycle/cancellation helpers. These portable tests do not execute the Win32 setup dialogs or viewer startup/import path.
- Source-reviewed the native toolbar parameter declarations, command registry, successful login/account-directory initialization, toolbar loading/reset ordering, backups, environment consumption, modal/timer guards and non-Windows stub. `git diff --check` passed.
- No native compiler is installed here. No viewer/host build, packaging/installer build, GitHub build, unrelated Go tests, broad suite or subagents were run. Private protocol remains 11; test matching host/viewer binaries from this checkpoint.

## Targeted Windows acceptance

1. First-open Copy, Keep and Cancel for a new and existing closed profile. Verify Cancel/picker/confirmation cancellation leaves setup available; Keep preserves existing choices; successful launch suppresses the next prompt. Later Characters/Settings copying remains available after closing the destination.
2. Copy toolbar-only from a different source account; verify order/placement, icon/text mode, alignment/layout, first-run reset handling and persistence after restart. Failed authentication followed by a successful login must copy only to the actual successful account; detachment before login must discard it. A named startup workspace may subsequently apply its own saved toolbars as usual.
3. Independently copy graphics, interface, each UI file, and all groups. Confirm unchecked groups and native browser/Inventory acceptance/debit/permission/ignored-dialog choices stay unchanged. Check backups in global user_settings and the destination account folder; source files stay unchanged.
4. Non-ASCII paths, another-installation/native-backup selection, multiple/no source accounts, missing/corrupt/oversized files and unavailable toolbar commands. Refused toolbar files must preserve the destination file. Verify notices and native log summaries.
5. Leave setup open while other characters/chat run; confirm they stay serviced, nested launches/switches are blocked, Cancel/stop management/session-set cancellation cannot produce a late launch. Test invocation from popped-out Always-on-top controls, keyboard traversal, high contrast and larger Windows text/DPI.
