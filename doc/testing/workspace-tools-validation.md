# Workspace tools follow-up

Base: merged PR #4, `main` / `33e371fb3f`. Branch: `feat/workspace-tools`, isolated worktree `/workspace/Folderstorm-workspace-tools`. Earlier plan-only commits `3de6e39288` and `bec84b4822` are carried forward through the latest roadmap and its authorization/verification instructions. Existing published branches and user edits were preserved.

## Completed checklist

- [x] Existing Controls bindings: Switch assigned workspace/layout, Next/Previous favorite and Return to previous arrangement. No default keys are taken; text entry and the viewer's existing mode/focus/conflict routing apply. Use for shortcut selects the account-local direct target. Accepted renames/deletions update targets and favorites.
- [x] Previous arrangement: one bounded session-only live snapshot, including unsaved positions and optional folder/toolbar context. Return swaps the two arrangements, preserves deliberate overlap, restores active identifiers and clears across account/session boundaries. Named definitions remain unchanged.
- [x] Optional utility snapping: frame, viewport and neighboring supported-window edges in logical UI units; Shift bypass. Other floaters retain their existing behavior.
- [x] Arrange selected utility windows: align left/right/top/bottom or distribute horizontally/vertically. Rechecks supported/visible/standalone/restriction eligibility, respects minima and supports Previous.
- [x] Account-local startup restore: Off by default, named workspace or Last arrangement. Waits for UI/Inventory readiness, times out with a notice and cancels after a manual switch/session change. Last is captured before orderly quit closes windows; pending Preferences prevents overwriting the prior valid snapshot.
- [x] Current/modified indicators in the switcher and favorite buttons. Comparison is debounced, ignores omitted/excluded fields and reconciles accepted fitted geometry. Returning to an unsaved Previous still compares against the named definition.
- [x] Update current from the switcher: confirms replacement, saves the arrangement reviewed when confirmation opened, guards account/session/revision/definitions, and preserves folder/component/toolbar options. Unsupported saved extensions are protected.
- [x] Selective workspace groups: viewport/bar placement, Inventory/Received Items, maps and compatible chat. Folder references are subordinate to Inventory. Legacy saves retain all groups. Excluded groups are neither opened/closed nor moved; excluding chrome retains its layout identifier.
- [x] Optional toolbar content: ordered registered command names per left/right/bottom bar plus display mode. Legacy/default saves leave sets alone. Preview/Cancel, Previous, Last, Update and modified comparison include opted-in sets. World menu access remains available; keyboard bindings are unchanged.
- [x] Native-picker export/import: selected named workspace; portable output omits folder references by default. Import reviews valid/skipped names and collisions, defaults to keeping existing names, requires explicit replacement and confirmation, and commits one validated batch. Maximum 1 MiB, depth 24, 20,000 XML tokens, 32 profiles, 16 additional Inventory windows and 64 commands per toolbar. DTD/entity declarations are refused; unknown commands are omitted with a review count. Imported files cannot select arbitrary floater registry names or invoke operations.
- [x] Read-only diagram preview: selecting a saved entry updates an open diagram without applying it. Uses the same fitted/staggered window geometry as loading and skin-layered/live minima; labels restored/omitted groups, fitting/skipped/missing-folder counts. Explicit Switch and double-click behavior remain. Toolbars use current tool metrics; restrictions/filters may change before loading.
- [x] Requested favorites styling: workspace/layout favorites share the existing landmark row after a `|` divider, use `favorites_bar_button.xml` and the landmark font from the active skin, and have no group labels or additional opaque full-width row. Both groups retain independent overflow. Workspace-only visibility works when landmarks are disabled.

## Focused checks actually performed

- Standalone geometry/name test, including shared neighboring-title fitting, deliberate overlap without staggering, tiny-frame title reachability and component roles: passed with `g++ -std=c++17 -Wall -Wextra -Werror` (two small source files only).
- Standalone import resource-fence test: passed with the same warnings enabled; covers empty/oversized files, escaped LLSD, DTD/entity declarations, excessive depth/nodes and unbalanced closing tags.
- All 9 changed XML files parse. Literal control/string names resolve in all five affected UI components; changed setting bindings resolve and settings have no duplicate names.
- Source/CMake, new floater registration and all four Controls action registrations checked. Shared landmark-row hosts exist in default, MetaHarper, Starlight and Starlight CUI English navigation overrides.
- Switcher controls fit inside its fixed scrollable body, with the action footer outside; preferences/tools remain scrollable. Source review covered transaction rollback, immutable confirmations, account/session/generation guards, startup teardown ordering, role restrictions and data exclusions.
- Both working and complete branch diffs pass whitespace checks. Current remote main was fetched and remains the merged PR #4 base.

No viewer compilation, packaging, graphical/native run, broad suite, unrelated Go tests or GitHub builds were performed. New real-LLSD schema cases for masks, toolbar counts/modes/duplicates and unsupported replacements are written but not executed because they require the viewer's llcommon linkage. Native Windows/MSVC `/WX` compilation and behavior are still unverified.

## Short native pass

1. Assign direct/cycle/Previous keys in Controls. Switch using buttons and keys; type in chat/search/editors; repeat in sitting/Mouselook modes. Rename/delete a target in Preferences, test OK and Cancel.
2. Move/resize utilities without saving, switch and return twice. Include minimized/closed utilities, multiple Inventory roots, toolbar sets and deliberate overlap. Repeat Preview/Cancel with attached Filters, tabbed chat and changed RLVa visibility restrictions.
3. Enable snapping, drag/resize near frame/viewport/peer edges, hold Shift, then align/distribute a selected subset. Check small viewer sizes and oversized minima.
4. Test startup Off/named/Last, a missing name, slow Inventory, a manual switch before readiness and a later account login. Quit with Preferences pending and confirm the prior valid Last remains.
5. Change a restored field and confirm `*`; Update/Cancel and stale confirmations. Save Inventory-only/maps-only/toolbar-only workspaces and ensure excluded groups stay unchanged. Old saves must keep their legacy behavior.
6. Export with folder references off/on; review imports with new/colliding/unsupported names, Keep/Replace, profile limits and malformed/oversized XML. Nothing changes before acceptance; a rejected batch changes nothing.
7. Open Diagram preview, select different workspaces/layouts and confirm no live arrangement changes. Switch and compare geometry; include smaller displays and missing folders.
8. Confirm landmark/workspace buttons share one row and skin style, with the divider, long labels, independent overflow, landmark/workspace visibility combinations and active/modified cues. Repeat at minimum/short UI sizes, higher UI scale and an alternate skin. Save options must remain accessible by scrolling.
