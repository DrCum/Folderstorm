# Workspace enhancement checkpoints

These are the original PR #4 checks. The separate strip/labels are superseded by PR #5’s shared landmark row; expanded branches, ordering, restore reports and Quiet UI are covered in [the PR #6 native pass](workspace-usability-validation.md).

## Favorite workspace/layout strip

This checkpoint has XML/binding and diff checks only. No viewer build, native UI run or broad tests were performed.

When ready for the combined native pass:

1. Star a workspace and a layout in the existing switcher, then enable Show workspace favorites strip. The row appears separately below navigation/landmark favorites with distinct labels. Switch from its buttons and resize the viewer until entries overflow into More. Long names have full tooltips. More opens the existing switcher for saving/managing entries and can hide the row.
2. Repeat with navigation and landmark favorites individually hidden, UI visibility off/on, Mouselook, an alternate skin and a higher UI scale. The row should follow the configured navigation span and hide in Mouselook/hidden UI.
3. Open Preferences. Strip switching and the switcher's strip visibility control are disabled, with a tooltip explaining OK/Cancel. Toggle strip visibility from Workspaces preferences and Cancel: its original visibility returns. A saved workspace cannot change the strip visibility preference.
4. Unfavorite, rename and delete entries; stale references disappear from the row and overflow. Log out and switch accounts; labels and old menu actions must not carry over. Existing favorites remain stored per account. A failed/corrupt saved entry reports the failure.

## Optional Inventory folders

This checkpoint has XML/binding, source inspection and diff checks only. Builds and native behavior checks are deferred.

1. In two single-folder Inventory windows, open different folders and choose different list/gallery/combination modes. Save A with **Remember Inventory folders** enabled; change folders/modes and save B with it enabled. Switch both ways from the toolbar and favorites strip. Close extras and reload A: newly created windows should restore their respective folders. Repeat with one selected folder in the primary window's **All Items** tab.
2. Load an old workspace or one saved with the option off: current folder roots and selections stay alone. Resave with the option enabled and confirm it now remembers folders. Selecting a saved workspace in Preferences shows its saved checkbox choice; overwrite honors the checkbox at confirmation time. Search **remember folders** to reach that control.
3. From normal and single-folder views, preview a different folder workspace then **Cancel**. Original roots, presentation, normal tab, navigation history and selections should return, including after folder children load asynchronously. Search text and filter settings stay unchanged. Repeat with extra windows, minimized windows and a second preview before Cancel.
4. Delete a saved folder or filter out a saved normal-view folder, then load its workspace. Geometry still loads and a brief notice explains the skip; no folder or Inventory operation occurs. Try a switch immediately after login and again once Inventory loads. Respect current RLVa Inventory visibility restrictions and windows with open Filters. Check the new checkbox fits at minimum switcher size and in an alternate skin/high UI scale.

## PR preflight review

The review before publication parsed all 11 changed XML files against the current `main`, checked literal control/string bindings for the switcher, Preferences and strip, verified the new sources/settings registrations, checked vertical bounds at the switcher's minimum size and Preferences content height, and confirmed the navigation stack exists in every bundled English skin override. Both committed and working diffs passed whitespace checks.

Source review covered menu ownership, skin fallback, account/session guards, legacy workspace parsing, optional capture and Preview/Cancel restoration. It tightened three paths: overflow Hide rechecks Preferences at click time, explicit selection cancels deferred Inventory selections, and role placement rechecks visibility restrictions on the idle pass. The current main was merged locally without conflicts.

No viewer compilation, packaging, native UI run, broad suite, or GitHub build was performed. The new strict LLSD test cases are present but have not been run in this checkpoint. The native checks above remain required to confirm behavior on Windows and alternate skins.
