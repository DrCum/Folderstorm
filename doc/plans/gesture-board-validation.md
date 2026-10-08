# Gesture board implementation and acceptance

Branch: `feat/gesture-board`, separate worktree `/workspace/Folderstorm-gesture-board`, stacked on open/unmerged PR #8 (`feat/workspace-context`, `34ab510d3d`) plus plan-only commit `aa828a9447`. Main was `33e371fb3f` at the baseline check. Existing worktrees and edits were preserved.

## Completed features

- [x] Standard **Gesture board** toolbar command and **World > Gesture board…** menu entry. Uses the existing Gestures icon and inherited skin resources. A real launcher in Preferences > Move & View > Workspaces is searchable by gesture board, gesture launcher, soundboard and gesture tiles.
- [x] First open creates an empty **My gestures** board for an account with default settings. The Inventory picker filters to gestures, supports multiple selection and requires Add/select to accept. Inventory drag/drop adds references without requiring pasted UUIDs or chat commands. Existing gesture/link references focus their tile instead of adding a duplicate.
- [x] Direct native playback, activation/loading of inactive gestures, separate Loading/Playing/Disabled/Unavailable tile states and useful failures. Native gestures-enabled/RLVa checks remain authoritative. Repeated loading clicks never queue extra plays; clicking a playing gesture stops it. Native play can contain sound, animation, wait and chat steps.
- [x] Bounded delayed playback (30 seconds) checks account/session, generation, current board/tile/reference, resolved item/asset and current restrictions. Opening editors/pickers, accepted changes, switching boards, closing the tool, Stop, logout or shutdown invalidate pending playback. Existing gesture-loaded callback slots are untouched. Gesture/Inventory observers update state; fallback checking runs only while a load is pending.
- [x] **Stop board gestures** cancels pending requests and stops still-playing native gesture IDs started by the board tool in this session. Completed IDs are discarded; unrelated gesture IDs are left alone. Native playback of one gesture ID is shared between viewer controls, so independent ownership of that same gesture is not promised.
- [x] Tile context menu: edit, replace through Inventory selection, move earlier/later, remove. Dragging tiles changes order without triggering Play; dropping Inventory gestures adds them at the end rather than silently replacing a tile.
- [x] Custom plain display text and RGB tile colors through the existing color picker, with gesture-name and theme-color fallbacks. Automatic light/dark foreground choice, standard button states/focus styling and a pressed playing state and non-ready state details on hover. Long tile text truncates with a full tooltip containing the Inventory name/path. Original gestures are unchanged.
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

1. Compile/package with the user's normal Windows build. Add the Gesture board toolbar button and use the World-menu/Preferences launcher. At minimum size and larger UI scale, inspect Folderstorm/default and Anastorm, mixed-size tiles, truncated labels, color contrast, focus/hover/pressed states, compact strip and page navigation.
2. Add ordinary, linked and no-copy gestures with the picker and Inventory drag/drop. Add a duplicate, choose several gestures, hit the 64-tile limit, replace through the editor and try broken/deleted/trashed references. Confirm no gesture plays merely from selecting or editing tiles.
3. Play a known sound/chat/animation gesture, an inactive gesture and an already loading one. Check activation, loaded state and repeated clicks, click-to-stop and Stop board gestures without stopping unrelated IDs. Disable gestures/change restrictions, cancel during loading, switch boards, replace/remove, close and log out before completion; verify no stale play. Check missing/load failure and one timed-out asset.
4. Edit Unicode/long text and colors. Save and Cancel, including a color picker left open while cancelling/reusing the editor. Reset to gesture-name/theme defaults, rename the Inventory item, switch skins and confirm labels/fallbacks follow those changes. Reorder by drag/context menu without playing.
5. Create/rename/duplicate/delete boards and relogin to the same/different account. Confirm independent order/styles and selected board, stable references, Preferences Cancel after board edits and workspace saves excluding the board/editor/picker.

## Limits and deferred scope

Playing an inactive gesture activates it in the normal viewer/server list; cancelling a later pending play does not undo a native activation already requested. The board does not auto-deactivate gestures or deactivate similar triggers. Stop cannot recall emitted sound/chat, inject audio into voice, or provide separate playback instances of a gesture shared with another viewer control. It is a gesture launcher, not a local audio-file player.

Workspace associations remain the optional follow-up in the plan. They are not included in this first delivery. Keyboard shortcuts are deliberately excluded. Native Windows/playback/skin acceptance above remains unverified until exercised in the viewer.

## Compact refinement: actual focused checks

- Inspected the supplied screenshot and extracted recording frames showing separated/overlapping controls and large reserved help/status/scroll areas.
- The standalone gesture-board model/cancellation check above passed after adding version-one migration, density/custom-size roundtrip, paired integer size bounds, unknown metadata rejection, and paged geometry checks. Geometry covers mixed sizes at 204 × 30, 264 × 126, 600 × 320 and 1 × 1: stable order, every tile exactly once, no overlap, positive bounds, configured sizes where space permits, unchanged dimensions after a longer label, and one empty page for an empty board.
- A focused XUI check parsed both changed floaters, verified static source control/string references and standard icon names, and checked the control strip at widths 220, 280, 530 and 800. Controls stay within bounds without overlap; resize anchors are present. The board has no scroll container or help paragraph. The editor spinner limits match the model.
- Inspected the native drag routing: `LLScrollContainer::handleDragAndDrop` always returns true, and `LLView::childrenHandleDragAndDrop` stops on mouse-opaque children. The board now validates and accepts supported Inventory cargo before either path consumes it.
- Inspected native button ellipsis/default parameters and disabled automatic resize, native spin-control precision/value conversions, menu callbacks/handles and stale-definition/session guards. `git diff --check` passed.
- No viewer/build/packaging/GitHub build, unrelated Go tests or subagent was run. This is not native Windows visual or drag/playback validation.

Additional Windows acceptance: resize an existing large saved board down to its minimum; check the single strip in Folderstorm and Anastorm at normal/larger UI scale. Toggle compact/regular and edit one tile's width/height, text and color; Save/Cancel, Duplicate and relog. Verify long names ellipsize, every page is reachable, navigation hides with one page, deleting the last tile on a page clamps navigation, and Add/drop/duplicate selects the right page. Drag ordinary and linked Inventory gestures onto both a tile and empty grid space without playing; reject folders/non-gestures/world/notecard cargo. Reorder by drag on a page and Move earlier/later across pages. Play/loading/stop and restricted/missing errors should remain recognizable through native pressed state, tile tooltip and warning tooltip. A tile larger than the available board area may render smaller, but enlarging the board restores its saved dimensions.

## Native feedback: tiles cover strip and Next appears outside the window

The earlier static XML check missed `LLFloater::initFloaterXML`'s legacy header stretch: after creating the controls it increases the floater height with `setRect`, deliberately leaving the controls in place. The runtime grid used the final height, so it started above the XML-positioned controls and covered most of their click area. `right="0"` also means an absolute left-edge coordinate in XUI, not an offset from the right edge, moving the Next button outside its footer.

`layoutFrame` now computes the board selector, warning, Add/Stop/Options, tile grid, empty state, footer and both page buttons from the final floater width/height and actual `getHeaderHeight()`. The floater applies all those rectangles through `setShape` so child/text bounds update together. Controls begin four units below the real header; the grid begins four units below the strip. Footer children use footer-local dimensions. The XML fallback for Next is `right="-1"`. Rebuilds after saved-rectangle restoration, resizes and tile changes use the same frame. No stored presentation fields, playback or Inventory-drop logic changes.

Actual focused checks passed: the standalone model suite now exercises final sizes 220 × 120, 220 × 215, 280 × 190 and 530 × 565 with 20/25/32-unit headers and one/multiple pages. It checks complete control rectangles inside the window, the four-unit header gap, no control/control or control/grid overlap, footer containment and local right-edge Next placement, and every populated tile's translated rectangle outside the strip/footer. The prior model/page/cancellation cases also passed. Changed XUI parses; a wiring check confirms all board surfaces use the shared frame, footer-local placement and grid-local tile conversion; `git diff --check` passed. No native viewer/build/packaging/GitHub build or unrelated tests run.

Remaining native acceptance: in Folderstorm and Anastorm, resize between minimum and larger sizes, close/reopen with the saved rectangle, add enough gestures for several pages, and click the selector, Add/Stop/Options and both page buttons across their full height. Check that the strip sits directly under the title bar, never disappears behind tiles, and Next stays at the footer's right edge. Repeat at larger UI scale; Vintage's 20-unit header is another useful skin check. Native visual/click correctness remains unverified by these geometry checks.

## Native feedback: board-name field appears above the editor

The reusable board/tile editor shrinks its window for New/Rename/Duplicate. Its content panels lacked explicit follows flags, leaving the name field at the initial tall-window position outside the compact editor. Both panels and their children now declare top-left layout and explicit top/left (plus right for stretching) anchors. On every open, the editor sets visibility before changing height, then positions panels from the final actual skin header and positions status/Save/Cancel within the footer. The authoritative gesture name truncates in the editor and retains its full tooltip. Save/Cancel/session guards and stored definitions are unchanged.

Focused checks passed: changed XML parses; all editor panel/child anchors and local control bounds are valid; alternating board/tile opens with 20/25/32-unit headers keep every active field within the window below its header, with separation from the footer; source wiring applies the final rectangles on each open; `git diff --check` passed. No model test was repeated because persistence/playback/page geometry did not change. No viewer/build/packaging/GitHub build or unrelated tests run. Remaining Windows acceptance: New, Rename and Duplicate followed by Edit tile, then repeated board/tile reuse; check that fields remain inside the window and reachable, and Save/Cancel, color picker and replacement picker behave normally in Folderstorm/Anastorm at normal/larger UI scale.
