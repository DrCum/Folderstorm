# Gesture board implementation and acceptance

Branch: `feat/gesture-board`, separate worktree `/workspace/Folderstorm-gesture-board`, stacked on open/unmerged PR #8 (`feat/workspace-context`, `34ab510d3d`) plus plan-only commit `aa828a9447`. Main was `33e371fb3f` at the baseline check. Existing worktrees and edits were preserved.

## Completed features

- [x] Standard **Gesture board** toolbar command and **World > Gesture board…** menu entry. Uses the existing Gestures icon and inherited skin resources. A real launcher in Preferences > Move & View > Workspaces is searchable by gesture board, gesture launcher, soundboard and gesture tiles.
- [x] First open creates an empty **My gestures** board for an account with default settings. The Inventory picker filters to gestures, supports multiple selection and requires Add/select to accept. Inventory drag/drop adds references without requiring pasted UUIDs or chat commands. Existing gesture/link references focus their tile instead of adding a duplicate.
- [x] Direct native playback, activation/loading of inactive gestures, separate Loading/Playing/Disabled/Unavailable tile states and useful failures. Native gestures-enabled/RLVa checks remain authoritative. Repeated loading clicks never queue extra plays; clicking a playing gesture stops it. Native play can contain sound, animation, wait and chat steps.
- [x] Bounded delayed playback (30 seconds) checks account/session, generation, current board/tile/reference, resolved item/asset and current restrictions. Opening editors/pickers, accepted changes, switching boards, closing the tool, Stop, logout or shutdown invalidate pending playback. Existing gesture-loaded callback slots are untouched. Gesture/Inventory observers update state; fallback checking runs only while a load is pending.
- [x] **Stop board gestures** cancels pending requests and stops still-playing native gesture IDs started by the board tool in this session. Completed IDs are discarded; unrelated gesture IDs are left alone. Native playback of one gesture ID is shared between viewer controls, so independent ownership of that same gesture is not promised.
- [x] Tile context menu: edit, replace through Inventory selection, move earlier/later, remove. Dragging tiles changes order without triggering Play; dropping Inventory gestures adds them at the end rather than silently replacing a tile.
- [x] Custom plain display text and RGB tile colors through the existing color picker, with gesture-name and theme-color fallbacks. Automatic light/dark foreground choice, standard button states/focus styling and a separate text state indicator. Long tile text wraps with a full tooltip containing the Inventory name/path. Original gestures are unchanged.
- [x] Editor Save/Cancel stages label/color/replacement choices, detaches the color picker on closure/reuse and guards replacement with an editor token and login/definition checks. Stale editors, menus and deletion confirmations cannot overwrite a changed definition or another account's collection.
- [x] Create, rename, duplicate, delete and switch saved boards. Ordered tiles and the selected board are account-local. Board/tile IDs remain stable on rename and are regenerated on duplication. Delete confirms only deletion of board configuration. Limits: 16 boards, 64 tiles each, 64 Unicode characters per name/label, finite RGB channels in [0,1]. Malformed stored data is preserved without a silent overwrite.
- [x] Only item references, ordered presentation fields and selected board identity are serialized. No gesture asset contents, chat commands, Inventory contents/filters/selections or conversation history. Management floaters remain outside workspace supported roles and do not commit pending Preferences settings. No new keybindings, MCP endpoint or import/export format was added.

## Actual focused checks

- A small standalone LLSD/model test linked to the existing llcommon library passed. It covers ordered label/color/default roundtrip, Unicode, reference-only records, corrupt/unknown versions and UUIDs, duplicate records, malformed/nonnumeric/nonfinite colors, maximum board/tile counts, rejected writes preserving originals, reorder bounds and source immutability, foreground selection and cancellation on account/login/generation/target/restriction/deadline changes.
- Changed settings/commands/menu/notification/floater/Preferences XML parses. New control names, singleton-floater behavior, source/header registrations and command label/tooltip references were checked. Fixed controls and panel bounds were checked at minimum widths; the three new resources inherit from default across skins.
- Native API signatures and numeric conversions were inspected, including one-argument `findCategoryUUIDForType`, loaded-versus-active gesture state, the manager's single callback slot, callback lifetime, standard button defaults, text mouse routing and drag release avoiding Play.
- `git diff --check` passed. No viewer/native build, packaging, GitHub build, unrelated Go test or subagent was used. These checks do not establish observed native playback or visual skin correctness.

Standalone check (run from repository root; library paths refer to the existing managed workspace prebuilts):

```sh
g++ -std=c++20 -DLL_LINUX=1 -DLL_RELEASE=1 \
  -Iindra/llcommon \
  -I/workspace/Folderstorm-theme-build/build-linux-x86_64/packages/include \
  -I/workspace/Folderstorm-theme-build/build-linux-x86_64/packages/include/apr-1 \
  indra/newview/fsgestureboardmodel.cpp indra/newview/tests/test_fsgestureboard.cpp \
  /workspace/Folderstorm-theme-build/build-linux-x86_64/_deps/openjpeg_avx2-build/bin/libllcommon.a \
  -L/workspace/Folderstorm-theme-build/build-linux-x86_64/packages/lib/release \
  -lboost_fiber -lboost_context -lboost_filesystem -lapr-1 -laprutil-1 \
  -lexpat -luuid -lz -lpthread -o /tmp/fs-gesture-board-test
LD_LIBRARY_PATH=/workspace/Folderstorm-theme-build/build-linux-x86_64/packages/lib/release \
  /tmp/fs-gesture-board-test
```

## Remaining Windows and skin acceptance

1. Compile/package with the user's normal Windows build. Add the Gesture board toolbar button and use the World-menu/Preferences launcher. At minimum size and larger UI scale, inspect Folderstorm/default and Anastorm, responsive columns, wrapped labels, color contrast, focus/hover/pressed states and scrolling.
2. Add ordinary, linked and no-copy gestures with the picker and Inventory drag/drop. Add a duplicate, choose several gestures, hit the 64-tile limit, replace through the editor and try broken/deleted/trashed references. Confirm no gesture plays merely from selecting or editing tiles.
3. Play a known sound/chat/animation gesture, an inactive gesture and an already loading one. Check activation, loaded state and repeated clicks, click-to-stop and Stop board gestures without stopping unrelated IDs. Disable gestures/change restrictions, cancel during loading, switch boards, replace/remove, close and log out before completion; verify no stale play. Check missing/load failure and one timed-out asset.
4. Edit Unicode/long text and colors. Save and Cancel, including a color picker left open while cancelling/reusing the editor. Reset to gesture-name/theme defaults, rename the Inventory item, switch skins and confirm labels/fallbacks follow those changes. Reorder by drag/context menu without playing.
5. Create/rename/duplicate/delete boards and relogin to the same/different account. Confirm independent order/styles and selected board, stable references, Preferences Cancel after board edits and workspace saves excluding the board/editor/picker.

## Limits and deferred scope

Playing an inactive gesture activates it in the normal viewer/server list; cancelling a later pending play does not undo a native activation already requested. The board does not auto-deactivate gestures or deactivate similar triggers. Stop cannot recall emitted sound/chat, inject audio into voice, or provide separate playback instances of a gesture shared with another viewer control. It is a gesture launcher, not a local audio-file player.

Workspace associations remain the optional follow-up in the plan. They are not included in this first delivery. Keyboard shortcuts are deliberately excluded. Native Windows/playback/skin acceptance above remains unverified until exercised in the viewer.
