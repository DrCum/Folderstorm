# Folderstorm workspace feature roadmap

PR #3 supplies the Workspaces toolbar button, World menu entry, favorites and quick saving. [PR #4](https://github.com/DrCum/Folderstorm/pull/4) adds the optional favorites strip and Inventory folder restoration, and carries PR #3 into `main`. Those two additions are implemented; native Windows/skin testing remains outstanding. Verify PR #4’s merge state when the next coding session starts.

## Follow-up implementation status — October 4, 2026

PR #4 was verified merged into `main` at `33e371fb3f`. The isolated follow-up branch `feat/workspace-tools` implements every remaining section below, with separately reviewable commits and focused checks. The user subsequently requested that workspace favorites share the landmark row: the follow-up uses the existing skin's landmark button style, a `|` divider, separate overflow menus and no labels or additional opaque row.

Completed: assignable direct/favorite-cycle keyboard actions; session-only Previous; optional snapping and selected-window alignment/distribution; optional named/Last startup restore; active/modified indicators; reviewed Update current; selective groups; optional toolbar button sets; bounded portable export/import with collision review; and a read-only fitted diagram. Native Windows and skin acceptance remains outstanding. See [follow-up verification and completed checklist](../testing/workspace-tools-validation.md).

The user also scheduled a one-time conditional continuation for **5:30 a.m. October 4, America/New_York**: resume only if implementation/publication is unfinished; otherwise do nothing. The completion PR and attached chat artifact are the completion record.

## Delivery and continuous implementation

The latest user instruction replaces the earlier requirement to pause after each feature. The user authorized and scheduled a one-time start at **12:15 a.m. Eastern on October 4, 2026** (`America/New_York`, 04:15 UTC) after their usage reset. The automation was created successfully to resume this chat’s workspace work and create a PR after focused checks. The user explicitly resumed work after that kickoff fired early. Continue through the entire remaining roadmap without waiting for confirmation between features, including when the user is away/asleep. Keep meaningful progress updates and separately reviewable commits. Combine closely related work when useful; snapping and alignment can remain two smaller commits without a pause between them. If a genuine blocker needs user input, ask asynchronously and continue independent authorized work where possible.

Remaining delivery order:

1. Keyboard shortcuts (section 3): direct switching and cycling favorites.
2. Previous arrangement (section 4): restore the unsaved setup before a switch.
3. Snapping, then alignment (section 5).
4. Startup restore (section 6): optional named workspace or last arrangement.
5. Active-workspace/modified indicator (section 7).
6. Update current workspace from the switcher (section 8). This can share one update with the indicator.
7. Selective restoration (section 9): choose which parts a workspace restores.
8. Per-workspace toolbar button sets (section 10).
9. Export/import (section 11).
10. Visual preview before switching (section 12).

At the next start, inspect the current repository and PR #4 merge state. Use a new branch/worktree based on current `main` if PR #4 has merged; otherwise retain its changes as an explicit dependency on a separate follow-up branch. Carry this latest roadmap into the follow-up branch if it is newer than the merged copy; the matching planning copy is `/workspace/plans/folderstorm-workspace-roadmap.md`. Preserve user edits and avoid rewriting published history. Do not automatically merge PRs. The user now authorizes checking the completed work and creating/publishing a follow-up PR when done, without another approval request. Use the correct base for PR #4’s merge state, describe the final scope and validation limits, and attach the created PR to this chat. Do not merge it automatically.

Check changed code and paths it could have affected; avoid testing unrelated areas. In particular, do not run Go tests when no Go code or relevant integration changed. Keep verification focused and economical: relevant targeted tests when appropriate, XML parsing, bindings/registration checks and source/diff inspection. The existing prohibition on big testing, viewer/build tests, packaging and GitHub builds remains. Document native behavior that still needs the user's local build/testing. Do not add subagents for this sequence. If delegation is later explicitly authorized, the user's restriction remains: only 6.1 Sol, never Fast mode.

## 1. Favorite workspace and layout strip — original idea 4

Status: implemented as the first local checkpoint on `feat/workspace-enhancements`, based on PR #3 commit `7ed3e18737`. Shared favorites/dispatch and a single strip component attach to the existing navigation stack across bundled skins. Strip visibility is optional and also appears in searchable Workspaces preferences. XML/control/string checks and diff inspection pass; builds and native testing are deferred. The short native checklist is in `doc/testing/manual-workspace-enhancements.md`.

Goal: one-click access to starred workspaces/layouts without opening the switcher.

Original arrangement (superseded by the user's shared-row request in this follow-up):

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

Status: implemented in the follow-up branch; focused checks and native verification limits are recorded in `doc/testing/workspace-tools-validation.md`.

Goal: load a chosen workspace/layout or cycle favorite entries from the keyboard.

- Add workspace actions to the existing keybinding system rather than installing a separate global key handler.
- Support a binding to a selected saved entry and next/previous favorite actions. Store named-target assignments per account, with clear handling for renamed/deleted entries.
- Use the same controller entry points and restrictions as strip/switcher clicks. Respect text entry, chat focus, Mouselook and existing binding conflict handling. Ship without taking over existing shortcuts.
- Offer the binding controls in the existing Controls workflow, with a link from the switcher if useful.
- Integration points: keybinding/Controls UI, key_bindings.xml, workspace favorites resolver and controller dispatch.

Checkpoint: direct and favorite-cycle actions work consistently with buttons and do not activate while typing in fields where the existing binding system suppresses viewer actions.

## 4. Return to previous arrangement — original idea 3

Status: implemented in the follow-up branch; focused checks and native verification limits are recorded in `doc/testing/workspace-tools-validation.md`.

Goal: undo a workspace/layout switch even when the previous setup was never saved.

- Capture the current live arrangement immediately before an accepted switch, including the Inventory folder context introduced in step 2.
- Retain one session-only previous arrangement and its active profile identifiers. Return swaps current and previous, enabling toggling between the two. No history of messages, permissions or Inventory operations is involved.
- Add Previous arrangement to the switcher and strip overflow, plus a keybinding action using step 3's dispatch. Disable it until a previous arrangement exists.
- Centralize capture/apply so toolbar, strip and shortcut switching behave identically. Settle pending placement before capture; returning must not recursively overwrite the snapshot it is applying.
- Honor account/session boundaries and Preferences transactions. Clear previous state on logout. Continue the existing Conversations geometry-only limitation.
- Integration points: fsworkspacecontroller.*, switcher/strip and keybinding actions.

Checkpoint: switch from an unsaved arrangement, return to it, and toggle back without changing saved workspace definitions or unrelated settings.

## 5. Window snapping and alignment — original idea 5

Status: implemented in the follow-up branch; focused checks and native verification limits are recorded in `doc/testing/workspace-tools-validation.md`.

Goal: arrange supported utility windows cleanly around the viewport and monitor gutters.

- Start with optional snapping during ordinary drag/resize for standalone Inventory, Chat and Map windows. Use UI logical coordinates and existing viewport/gutter geometry.
- Snap to useful frame/viewport edges and neighboring supported windows within a small tolerance. Respect actual window minimum sizes, title/close reachability and dependent/hosted-window rules. Provide a temporary bypass while dragging.
- Then add an Arrange action for choosing supported windows and aligning edges or distributing spacing. Reuse the snapping geometry helpers; do not move arbitrary editors or dialogs.
- Treat snapping controls and the align/distribute UI as two checkpoints if the second becomes a larger update.
- Integration points: floater drag/resize placement, FSChromeLayoutController geometry, supported-window enumeration and a small Arrange UI.

Checkpoint A: opt-in dragging snaps correctly and can be bypassed. Checkpoint B: selected utility windows align/distribute within their valid bounds.

## 6. Optional restore at login — original idea 6

Status: implemented in the follow-up branch; focused checks and native verification limits are recorded in `doc/testing/workspace-tools-validation.md`.

Goal: arrive in a chosen workspace, or resume the last arrangement.

- Add an account-local startup choice: Off, a named workspace, or Last arrangement. Default Off.
- For a named workspace, restore once after login when chrome, Inventory and the required UI are ready. Missing profile names keep the viewer usable and expose a brief status.
- For Last arrangement, save a dedicated bounded account-local snapshot at orderly logout/quit, independent of named definitions. Include the supported window/folder context from the previous steps.
- Do not overwrite a valid last arrangement while the UI is tearing down, from the login screen or from another account. Use the existing session guards and protect startup callbacks from applying after logout or after the user has already switched manually.
- Retain supported-window limits, visibility restrictions, Preferences/switcher exclusions and Conversations geometry-only behavior.
- Integration points: startup/logout lifecycle, fsworkspacecontroller.*, account settings and Workspaces preferences/search.

Checkpoint: the selected startup behavior restores once, Off preserves manual startup behavior, and stale/missing data cannot interfere with a later login/account.

## 7. Active workspace and modified indicator

Status: implemented in the follow-up branch; focused checks and native verification limits are recorded in `doc/testing/workspace-tools-validation.md`.

Goal: make the current arrangement identifiable without opening Preferences.

- Highlight the active workspace's favorite button and identify the active workspace in the switcher. Use text/icon/tooltip cues as well as skin colors; an unfavorited active workspace still has a visible identity in the switcher.
- Show a modest modified indicator when supported live positions, geometry, folder context or chrome differ from the active saved definition. Reuse a shared comparison outside the Preferences transaction rather than opening a transaction just to compare.
- Compare only fields the workspace actually restores. Ignore management windows, legacy omitted fields, transient placement and unsupported conversation state. Reconcile expected fitting to a smaller display so a completed switch does not immediately report a misleading modification.
- Refresh on accepted switches, rename/delete, updates, account/session changes and meaningful live layout changes. Bound/debounce comparison work rather than serializing every window on every draw.
- Integration points: fsworkspacecontroller.*, shared quick-access model, switcher and favorites strip.

Acceptance: current/modified cues reflect supported changes, clear after updating, and cannot retain another account's identity. Later selective restoration and toolbar sets extend the same comparison.

## 8. Update current workspace from the switcher

Status: implemented in the follow-up branch; focused checks and native verification limits are recorded in `doc/testing/workspace-tools-validation.md`.

Goal: save adjustments to the active workspace without reopening Preferences or inventing a new name.

- Add Update current alongside the existing Save as new action. Enable only for an existing user workspace in the current account/session and when Preferences is closed.
- Show a replacement confirmation naming the destination. Capture the reviewed live arrangement and validate its profile/account/revision before acceptance; do not silently save a different arrangement changed while the confirmation was open.
- Preserve that workspace's folder-restoration choice and, once added, its component/toolbar choices. Save as new remains a separate action and existing unsupported definitions remain protected.
- Settle pending placement before capture. Reuse strict serialization and controller persistence; update favorites/active state without changing unrelated preferences.
- Integration points: fsworkspacecontroller.*, fsfloaterworkspaces.*, switcher XUI and confirmation notifications.

Acceptance: Update replaces exactly the named workspace with the reviewed setup, Cancel leaves it intact, and stale confirmations cannot write another session's settings.

## 9. Choose what a workspace restores

Status: implemented in the follow-up branch; focused checks and native verification limits are recorded in `doc/testing/workspace-tools-validation.md`.

Goal: change Inventory or maps without necessarily changing viewport, bars or chat.

- Add per-workspace capture/restoration choices for viewport/bars, Inventory, maps and compatible chat geometry. Keep Inventory folder restoration a subordinate opt-in choice. Explain these choices in both save/management workflows without crowding the quick switcher.
- Extend the bounded schema with a validated component mask. Missing masks retain legacy behavior. Only selected groups are captured/applied; excluded groups must not open, close or move windows, alter folders/Inbox, or reset active layout identifiers.
- Apply the same mask to modified comparisons, Update current, previews and named startup restoration. Previous arrangement and Last arrangement retain complete supported live snapshots so returning can restore unsaved state regardless of a named workspace's mask.
- Preserve transactional OK/Cancel semantics and all existing supported-window/account/session/restriction rules. Mouselook viewport settings belong to the viewport group.
- Integration points: fsworkspacelayout.*, serialization/controller, both save UIs, Settings search and startup/previous-arrangement adapters.

Acceptance: an Inventory-only workspace leaves viewport, bars, maps and chat alone; old definitions still behave as before; invalid masks fail without changing saved data.

## 10. Save toolbar button sets

Status: implemented in the follow-up branch; focused checks and native verification limits are recorded in `doc/testing/workspace-tools-validation.md`.

Goal: let Driving and Inventory sorting workspaces expose different useful toolbar tools.

- Add an optional per-workspace toolbar-content choice, distinct from toolbar placement in the viewport/bars group. Default off; old saves never replace button sets.
- Capture bounded ordered command IDs per supported toolbar and the relevant display mode. Restore using the viewer's toolbar configuration APIs; validate IDs against registered commands and skip unavailable feature-gated tools rather than accepting arbitrary actions.
- Settle pending UI placement and preserve supported toolbar restrictions. Ensure the workspace switcher remains reachable through the World menu even if its toolbar button is absent from the loaded set.
- Extend live rollback, Previous arrangement, Last arrangement, modified comparison and Update current to include opted-in toolbar content. Avoid changing keyboard bindings.
- Integration points: toolbar configuration/LLToolBarView, command registry, workspace schema/controller and save UI.

Acceptance: switching changes ordered button sets only when requested, Cancel/Previous restore prior sets, unknown commands are handled gracefully, and skin/icon-only modes remain usable.

## 11. Export and import workspaces

Status: implemented in the follow-up branch; focused checks and native verification limits are recorded in `doc/testing/workspace-tools-validation.md`.

Goal: move arrangements between computers without manually copying settings files.

- Use the native file picker and a versioned, bounded export format containing selected workspace definitions. Export portable layout data by default; offer an explicit option to include account-specific Inventory folder references.
- Exclude credentials, unrelated settings, runtime rollback/history and conversation data. Include validated component choices and optional registered toolbar IDs from sections 9–10.
- Validate file size, schema, names, counts, numbers, UUID fields and command IDs before changing any settings. Never instantiate arbitrary floater names/keys or execute operations from an imported file.
- Show a review of accepted/skipped entries and name collisions. Default to keeping existing definitions or choosing new names; replacing an entry must be explicit. Persist a validated batch transactionally, respecting profile limits and account/session changes.
- Folder references remain optional and may be unavailable on the receiving account/grid. Retain the existing nonblocking skip behavior and geometry fitting on different displays.
- Integration points: native file picker/LLSD serializer, strict workspace model, controller and a compact import/export management UI.

Acceptance: export/import round trips portable arrangements, collisions do not silently overwrite, and malformed or oversized files cannot partially change settings.

## 12. Visual preview before switching

Status: implemented in the follow-up branch; focused checks and native verification limits are recorded in `doc/testing/workspace-tools-validation.md`.

Goal: understand a saved arrangement before applying it.

- Show a small diagram of the saved viewport, bars and supported window rectangles when an entry is selected. Reuse geometry fitting rather than launching windows or changing the live arrangement to draw the preview.
- Use labeled shapes, not a captured world/chat screenshot. Distinguish workspace window coverage from a chrome-only layout and label omitted component groups.
- Indicate the adjustments needed for the current display/minimum window sizes. Identify unavailable folders or unsupported details when the existing read-only model can determine them; do not claim monitor information the saved schema does not contain.
- Keep double-click/explicit Switch behavior, keyboard access, overflow management and skin/high-DPI sizing. Invalid definitions produce a readable preview state while preserving their stored data.
- Integration points: fsworkspacelayout fitting, existing viewport diagram patterns, switcher UI and shared read-only profile resolver.

Acceptance: selecting an entry never applies it, the diagram agrees with the controller's fitted placement, and narrow/alternate-skin layouts remain readable.

## Later combined native check

The user performs native testing from their local Windows build. Keep a short checklist covering favorites/overflow, multiple folder workspaces, direct/cycling shortcuts, Previous with unsaved positions, snapping/bypass and alignment, all startup choices, active/modified indicators and Update confirmation, selective restoration, toolbar sets, portable imports/collisions and visual preview. Include Preferences Preview/Cancel, a narrow/high-DPI view and an alternate skin. Verification and the completion PR are authorized, but full viewer/build tests, GitHub builds and unrelated/broad test suites remain excluded.
