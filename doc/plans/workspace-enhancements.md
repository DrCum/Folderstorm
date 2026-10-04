# Folderstorm workspace feature roadmap

PR #3 supplies the Workspaces toolbar button, World menu entry and switcher, including favorites and saving a new workspace. A persistent favorites strip is not included.

## Delivery and usage checkpoints

Implement in this order: original idea 4, then 1, 2, 3, 5 and 6. Finish one feature, report the files changed and what is ready, then pause for the user's next instruction. Keep additions that complete the same feature together. Snapping and alignment may use separate checkpoints because their combined UI work is larger.

Start implementation on a new local branch based on the latest PR #3 code, keeping the current PR unchanged. Retain feature boundaries as separate local commits. Defer the next PR and the combined manual testing pass until the user requests them; determine the correct PR base from the merge state then. Do not run viewer builds, packaging, GitHub CI, broad test suites or add subagents for this sequence. During implementation, use only small checks needed to catch a direct editing problem, such as parsing changed XML and inspecting the diff. Report native behavior that remains unverified.

## 1. Favorite workspace and layout strip — original idea 4

Status: implemented as the first local checkpoint on `feat/workspace-enhancements`, based on PR #3 commit `7ed3e18737`. Shared favorites/dispatch and a single strip component attach to the existing navigation stack across bundled skins. Strip visibility is optional and also appears in searchable Workspaces preferences. XML/control/string checks and diff inspection pass; builds and native testing are deferred. The short native checklist is in `doc/testing/manual-workspace-enhancements.md`.

Goal: one-click access to starred workspaces/layouts without opening the switcher.

Suggested arrangement, as a separate row near the existing landmark favorites bar:

    Landmarks:   [Coworking Space] [Cherry's Fruit Bar] ...
    Workspaces:  [Driving] [Inventory sorting] | Layouts: [Dual monitor] [More…]

These are example user-created names, not reinstated Starting Arrangements.

- Reuse the account-local FSWorkspaceQuickSwitchFavorites setting from PR #3. Favoriting/unfavoriting in the switcher immediately updates the strip.
- Give workspace and layout buttons distinct groups and labels. Full names appear in tooltips; long names are shortened visually. Overflow entries go into More, with access to the existing switcher for management/saving.
- Add a Show workspace favorites strip option in the switcher, discoverable through Settings search. Keep visibility as a viewer preference independent of loading a workspace, so the switch controls do not disappear because of the layout being loaded. The strip starts hidden until enabled.
- Route button clicks through FSWorkspaceController::quickSwitchWorkspace/quickSwitchLayout, retaining the Preferences-open, account/session and visibility restriction checks. Explain why switching is disabled while Preferences has pending changes.
- Refresh after profile rename/delete, favorites changes, account changes and available-width changes. Ignore stale favorite references without altering saved profiles. Clear account-specific labels at logout.
- Use normal skin colors/fonts/button resources. Account for the shared navigation area and its chrome placement, visibility, UI scale and Mouselook behavior.
- Integration points: fsfloaterworkspaces.*, fsworkspacecontroller.*, llnavigationbar.*, fschromelayoutcontroller.*, settings XML and navigation XUI. A small shared favorites resolver should serve both switcher and strip, avoiding duplicate parsing.
- The navigation bar and main view have bundled overrides in MetaHarper, Starlight and Starlight CUI, including translations. Inspect the layered XUI result and use a shared strip component with minimal additions to required overrides. Do not assume the earlier Preferences-panel skin check covers this new row.

Checkpoint: the optional row displays the same favorites as the switcher, switches entries, handles overflow, and honors disabled states. Do not add folder restoration or shortcuts in this chunk.

## 2. Remember Inventory folders — original idea 1

Status: implemented as the second local checkpoint on `feat/workspace-enhancements`. Both save UIs offer an opt-in checkbox. Bounded, validated account-local folder context covers the primary, owned extra and ordinary additional Inventory slots. A scoped Inventory adapter preserves search filters; live-only folder/navigation/selection snapshots support Preview/Cancel. Missing or filtered folders skip with a status/notification. Inventory must be usable; an early switch may need repeating after it loads. XML/binding and diff checks only; no builds or native run. Keyboard shortcuts are the next checkpoint.

Goal: an Inventory sorting workspace reopens the folders being worked on, along with its windows.

- Add a per-save Remember Inventory folders option, available in Preferences and the quick-save switcher. Older workspaces retain their geometry-only behavior.
- For single-folder Inventory windows, capture the folder root and presentation mode. For normal Inventory windows, capture a selected folder where one exists. Do not serialize item selections, search filters, expanded trees or Inventory contents.
- Store optional, validated folder context against the primary Inventory slot, owned extra and ordered ordinary additional Inventory slots. IDs remain in account-local workspace settings; never accept arbitrary floater registry names/keys from persisted data.
- Restore after Inventory and the target windows are ready, using existing folder-view APIs and account/session generation checks. Missing/deleted/inaccessible folders keep a usable window and produce a brief status rather than blocking the whole workspace.
- Reuse current window instances and preserve Preview/Cancel behavior: capture the original folder presentation before applying saved context and restore it on Cancel. Loading older saves leaves current folder context alone.
- Integration points: fsworkspacelayout.*, fsworkspaceserialization.cpp, fsworkspacecontroller.*, llpanelmaininventory.*, llsidepanelinventory.* and both save UIs.

Checkpoint: a named workspace restores different folder roots into multiple Inventory windows; existing workspaces still load, and Cancel restores the prior folder views.

## 3. Workspace keyboard shortcuts — original idea 2

Goal: load a chosen workspace/layout or cycle favorite entries from the keyboard.

- Add workspace actions to the existing keybinding system rather than installing a separate global key handler.
- Support a binding to a selected saved entry and next/previous favorite actions. Store named-target assignments per account, with clear handling for renamed/deleted entries.
- Use the same controller entry points and restrictions as strip/switcher clicks. Respect text entry, chat focus, Mouselook and existing binding conflict handling. Ship without taking over existing shortcuts.
- Offer the binding controls in the existing Controls workflow, with a link from the switcher if useful.
- Integration points: keybinding/Controls UI, key_bindings.xml, workspace favorites resolver and controller dispatch.

Checkpoint: direct and favorite-cycle actions work consistently with buttons and do not activate while typing in fields where the existing binding system suppresses viewer actions.

## 4. Return to previous arrangement — original idea 3

Goal: undo a workspace/layout switch even when the previous setup was never saved.

- Capture the current live arrangement immediately before an accepted switch, including the Inventory folder context introduced in step 2.
- Retain one session-only previous arrangement and its active profile identifiers. Return swaps current and previous, enabling toggling between the two. No history of messages, permissions or Inventory operations is involved.
- Add Previous arrangement to the switcher and strip overflow, plus a keybinding action using step 3's dispatch. Disable it until a previous arrangement exists.
- Centralize capture/apply so toolbar, strip and shortcut switching behave identically. Settle pending placement before capture; returning must not recursively overwrite the snapshot it is applying.
- Honor account/session boundaries and Preferences transactions. Clear previous state on logout. Continue the existing Conversations geometry-only limitation.
- Integration points: fsworkspacecontroller.*, switcher/strip and keybinding actions.

Checkpoint: switch from an unsaved arrangement, return to it, and toggle back without changing saved workspace definitions or unrelated settings.

## 5. Window snapping and alignment — original idea 5

Goal: arrange supported utility windows cleanly around the viewport and monitor gutters.

- Start with optional snapping during ordinary drag/resize for standalone Inventory, Chat and Map windows. Use UI logical coordinates and existing viewport/gutter geometry.
- Snap to useful frame/viewport edges and neighboring supported windows within a small tolerance. Respect actual window minimum sizes, title/close reachability and dependent/hosted-window rules. Provide a temporary bypass while dragging.
- Then add an Arrange action for choosing supported windows and aligning edges or distributing spacing. Reuse the snapping geometry helpers; do not move arbitrary editors or dialogs.
- Treat snapping controls and the align/distribute UI as two checkpoints if the second becomes a larger update.
- Integration points: floater drag/resize placement, FSChromeLayoutController geometry, supported-window enumeration and a small Arrange UI.

Checkpoint A: opt-in dragging snaps correctly and can be bypassed. Checkpoint B: selected utility windows align/distribute within their valid bounds.

## 6. Optional restore at login — original idea 6

Goal: arrive in a chosen workspace, or resume the last arrangement.

- Add an account-local startup choice: Off, a named workspace, or Last arrangement. Default Off.
- For a named workspace, restore once after login when chrome, Inventory and the required UI are ready. Missing profile names keep the viewer usable and expose a brief status.
- For Last arrangement, save a dedicated bounded account-local snapshot at orderly logout/quit, independent of named definitions. Include the supported window/folder context from the previous steps.
- Do not overwrite a valid last arrangement while the UI is tearing down, from the login screen or from another account. Use the existing session guards and protect startup callbacks from applying after logout or after the user has already switched manually.
- Retain supported-window limits, visibility restrictions, Preferences/switcher exclusions and Conversations geometry-only behavior.
- Integration points: startup/logout lifecycle, fsworkspacecontroller.*, account settings and Workspaces preferences/search.

Checkpoint: the selected startup behavior restores once, Off preserves manual startup behavior, and stale/missing data cannot interfere with a later login/account.

## Later combined native check

When the user chooses to test and create the next PR, use a short Windows pass: show/hide the strip and overflow; restore two different folder workspaces; invoke a shortcut; return to an unsaved arrangement; snap/align supported windows; restart with each startup option. Include Preferences Preview/Cancel, a narrow/high-DPI view and one alternate skin. No build or broad test pass is part of the implementation checkpoints.
