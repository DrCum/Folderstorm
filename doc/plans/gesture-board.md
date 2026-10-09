# Gesture board implementation plan

Status: implemented on `feat/gesture-board`, stacked on open PR #8 (`feat/workspace-context`, head `34ab510d3d`). The user authorized implementation and publication. Inventory selection, native playback, saved boards, tile reordering, editable tile text/colors, compact paged presentation and individual tile sizes are included. Keyboard shortcuts are excluded. Actual focused checks and remaining native acceptance are recorded in [gesture-board-validation.md](gesture-board-validation.md).

Planning source: `feat/workspace-context` at `34ab510d3d`, the current PR #8 worktree. PR #8 was verified open/unmerged and current main remained `33e371fb3f`; implementation uses a separate worktree at `/workspace/Folderstorm-gesture-board` from the PR #8 head plus the plan-only commit `aa828a9447`. Preserve user edits and published history. Do not mix this feature into a workspace/snapshot fix without an explicit delivery decision.

## User experience

Add a standard **Gesture board** toolbar button and a World-menu entry. Both open a resizable floater with a board selector, a paged grid of fixed-size gesture tiles and one compact control strip.

1. Choose **Add gestures…** and select one or more gestures in an Inventory picker, or drag gestures from Inventory onto the board. No chat commands, UUID entry, trigger phrases or gesture editing are required.
2. Click a tile to play its entire gesture through the normal viewer gesture engine. Loading and playing states are visible; an unavailable tile offers a useful explanation rather than silently doing nothing.
3. Use a tile's context menu to **Edit tile…**, **Replace gesture…**, **Move earlier/later**, or **Remove tile**. Dragging a tile reorders the board; dragging an Inventory gesture adds/replaces only at an explicit drop target. Removing a tile never deletes its Inventory item.
4. Edit the tile's displayed text and choose its color through the existing color picker or a small theme-aware palette. Provide **Use gesture name** and **Use theme color** resets. Tile changes do not rename, recolor or modify the original Inventory gesture.
5. Create, rename, duplicate and delete named boards, such as Driving or Parties. Remember the selected board for that account. Board deletion confirms the loss of its tile configuration, not deletion of the referenced gestures.

Original illustrative layout (superseded by the compact refinement below):

```text
Gesture board                         [Driving       v]
[Add gestures…]  [Edit board…]  [Stop board gestures]

┌──────────────────┐ ┌──────────────────┐
│      Horn        │ │     Thank you    │
│  custom orange   │ │    theme color   │
└──────────────────┘ └──────────────────┘
┌──────────────────┐ ┌──────────────────┐
│      Wave        │ │   Engine start   │
│   custom blue    │ │   custom green   │
└──────────────────┘ └──────────────────┘
```

Color notes above describe the mockup; actual tiles show the selected label, with Inventory name/path available in a tooltip. The UI does not show UUIDs or programming details. Long labels truncate with ellipses and retain a full tooltip; they never grow tiles. Ready tiles do not display Play. Playing tiles use the standard pressed state; loading, restrictions and unavailable details remain in tooltips. A small warning indicator offers full error details on hover.

## Source-backed behavior and limits

- `LLGestureMgr::playGesture(const LLUUID&)` resolves Inventory links and plays an already loaded active gesture. It does not accept an arbitrary unloaded item and load it automatically.
- `LLFloaterGesture::onClickPlay` and `LLGestureBridge::performAction("play")` already activate inactive gestures with `activateGestureWithAsset(..., true, false)` and play after loading. Reuse the normal activation behavior: keep other gestures with similar triggers active, and show that the played gesture becomes active.
- `isGestureActive` can be true while its loaded pointer is still null. Treat **active**, **loading** and **ready** as different states.
- `setGestureLoadedCallback` stores one callback per item; registering a board callback there could overwrite another UI's callback. Prefer observing manager readiness and a bounded pending request, or introduce a shared additive completion mechanism only if needed. Do not take over that callback slot.
- The engine already checks the viewer's gestures-enabled setting and `RlvActions::canPlayGestures`. Retain those checks and all normal restrictions on sound, animation and chat steps.
- A tile plays all sound, animation, wait and chat steps in its gesture. Sound is normal in-world audio, not microphone/voice injection or an arbitrary local audio-file player.
- Native Stop prevents subsequent steps and stops the gesture's animations. It cannot retract chat already sent or promise to silence a one-shot sound already emitted. Do not label this as muting all audio.

## Shared model and persistence

Introduce a small board model and shared controller, separate from the floater. Proposed touchpoints are `fsgestureboardmodel.*`, `fsgestureboardcontroller.*` and `fsfloatergestureboard.*`; reuse existing viewer controls and registrations rather than adding a parallel gesture engine.

Account-local, versioned data:

- Board: stable generated ID, bounded name, ordered tiles and compact/regular density. Renaming does not change the ID.
- Tile: stable generated tile ID, gesture Inventory item reference, optional custom label, optional custom RGB color and optional paired width/height in pixels. An absent label/color means current Inventory name/current skin styling.
- Board settings: collection and selected board ID in `settings_per_account.xml`; definitions contain references and presentation choices only.
- Initial limits: 16 boards, 64 tiles per board; board names and custom labels up to 64 Unicode characters with a bounded UTF-8 representation. Reject malformed IDs, nonfinite/out-of-range color channels, unknown versions and excessive collections. Keep malformed stored data for diagnosis rather than silently replacing it with an empty save.
- Resolve gesture links at playback time, revalidate the authoritative item type/asset, and handle a renamed, moved, deleted or broken link. Resolve aliases to one underlying item for duplicate-play protection. Adding an existing gesture on the same board selects its tile instead of creating an accidental duplicate; the same gesture can appear on different boards.
- Do not serialize gesture asset contents, sounds, chat commands, conversation content, Inventory contents, Inventory filters or item selections. No export/import or MCP endpoint is part of this first implementation.

Saving is explicit for the tile/board editor: **Save** commits the local configuration, **Cancel** leaves it unchanged. Picker selection is staged until **Add**. Accepted drag/drop or reorder actions commit immediately. Opening, switching or editing a board never activates or plays its gestures.

## Checkpoint 1 — model, picker and direct playback

Deliver one useful board with a standard toolbar/menu launcher, gesture-only multi-selection picker, Inventory drag/drop and clickable tiles.

- Reuse an Inventory panel and gesture filter; validate each chosen/dropped Inventory item and linked target. Support ordinary and linked gestures without requiring copy/modify permissions just to reference a playable item.
- Use the shared controller for every playback action. Track pending loading by resolved item, account/session, board/tile IDs and a cancellation generation. Bound pending loads to 30 seconds, report failures, and never queue repeated clicks to replay later.
- Recheck login/session, current tile target, gesture-enabled state and restrictions immediately before activation and again before playback. Add/edit/replace/delete/cancel, board switching, floater closure, logout and shutdown cancel pending board playback. Completion after cancellation must not start a gesture. A gesture already playing follows normal native behavior.
- Activation uses the native Inventory item/asset path, with `deactivate_similar=false`. Explain once in concise help that playing an inactive gesture activates it; do not auto-deactivate it later, since that could interfere with other viewer gesture controls.
- Use manager/Inventory observers, updating only when relevant state changes. Avoid scanning Inventory or assets per frame; any fallback polling runs only while a bounded load is pending. Unregister observers and avoid dangling floater captures on destruction.
- First click starts or requests loading. A click on the same already-playing gesture stops it through the native manager instead of starting overlapping copies. Loading/blocked/missing status must remain distinguishable.
- **Stop board gestures** cancels pending board requests and stops gestures tracked as started by this board session, leaving unrelated gesture IDs alone. Native playback is shared per gesture ID, so it cannot provide separate private instances of the same gesture used elsewhere.

Acceptance: select or drag a gesture, then play it without chat configuration; inactive/loading/active cases work; repeated loading clicks do not queue playback; broken links, failed loads, disabled gestures and restrictions explain the result; cancelling or logging out prevents delayed playback.

## Checkpoint 2 — editable tile text, colors and ordering

- Add a tile editor with a gesture reference/name, **Display text**, theme/custom color selection and the existing color picker. Replacing the gesture requires Inventory selection, not a pasted UUID.
- Preview style changes in the editor only; commit on Save. Cancel also cancels a pending color-picker result. Bind editor callbacks to tile ID, board ID, edit generation and account/session so stale dialogs cannot edit another tile/account.
- Store text as plain display text; do not interpret it as a chat command, URL, markup or executable input. Render tooltips using authoritative Inventory information and the user's label separately.
- Apply custom color through standard button styling for normal/hover/pressed/disabled states. Keep readable foreground contrast, a visible focus outline and a separate loading/playing indicator. Use theme colors by default and avoid full skin-resource copies or forced opacity across the floater.
- Provide drag reordering plus **Move earlier/later** alternatives in the context menu. Persist tile order independently of labels. Distinguish tile drags from Inventory drags and ordinary playback clicks.

Acceptance: custom text/color survive reopening and relogin; resets follow renamed gestures/current skin; Save/Cancel work including delayed picker callbacks; reordering does not play tiles; long/Unicode labels, disabled state and dark/light/pastel skins stay readable.

## Checkpoint 3 — saved boards and integration polish

- Add create/rename/duplicate/delete management and the board dropdown. Duplication generates new board/tile IDs but retains references, labels, colors and ordering.
- Persist the active board per account and remember floater geometry using ordinary viewer mechanisms. Login to another account displays only that account's boards. Do not replay anything at login or on reopening.
- Finish command/menu/floater/CMake registration, localized strings and Settings-search aliases such as gesture board, gesture launcher and soundboard where they lead to a real control.
- Keep this management floater and its picker/editor excluded from workspace capture, like the other workspace management windows. Opening it should not make a saved workspace appear modified.
- Confirm configuration changes affect only board settings; opening it during Preferences must neither commit pending Preferences settings nor interfere with later Cancel.

Acceptance: several boards retain independent ordered/customized tiles; rename preserves identity and selected board; duplicate/delete affect only board configuration; toolbar/menu entry works with supported skins; missing Inventory at login recovers once ready without auto-playing.

## Optional follow-up — associate a board with a workspace

This was suggested as a later extension and is not required for the first three checkpoints. Keep it separately reviewable if adopted during implementation.

- Add an opt-in workspace choice **Gesture board: Do not change / selected board**. Refer to the stable account-local board ID, not its mutable label.
- Applying a workspace selects that board only; it never plays or activates gestures. Opening the board floater is a separate explicit option, default Off. Do not expand the supported-window geometry list merely to store the board selector.
- Integrate selection with Preferences preview/Cancel, Previous, Update-current capture-choice preservation and named/Last startup restore. Keep partial camera/graphics/window loads from changing the selected board unless the board group is explicitly chosen.
- Missing board references leave the current board unchanged and appear in the restore report. Account-local references are omitted from portable workspace exports by default. Decide the schema/group representation at this checkpoint using the then-current workspace model.

## Focused verification and delivery

Use separate reviewable commits for model/playback, customization and saved-board integration, combining closely related UI/model changes when useful. No subagents, viewer/build/packaging/GitHub builds or unrelated Go tests.

- Small model checks: validated bounds and finite colors; ordered serialization; label/style fallback; duplicate and link identity; failed edits preserve originals.
- Focused pending-action checks or a small harness: loading repeated clicks, cancel/close/remove/replace/board switch, logout/account change, restrictions/disable changes and stale completion. Do not introduce a broad test framework just for this feature.
- Parse changed XUI/settings XML; inspect command/floater/source registration, minimum-size layout, callback ownership and native API types; run `git diff --check`.
- Manual Windows acceptance on the user's build: Inventory picker/drop, linked/inactive/loading gestures, play/stop, missing assets, customization Save/Cancel, reorder, board management, relogin, Preferences Cancel and two supported skins at normal/larger UI scale. Verify sound/chat/animation behavior with a few known gestures; do not claim Stop mutes an emitted sound.

Definition of done: users can fill, customize, order, save and use gesture boards entirely through the UI; native playback and restrictions remain authoritative; no stale request plays after cancellation/session change; actual focused checks and remaining native acceptance are reported honestly. Keyboard shortcuts remain out of scope.

## Testing feedback — compact board refinement

Included in PR #9 after the user tested the first Windows delivery:

- One top strip: board selector plus Add, Stop and Options icons with hover tooltips. New/Rename/Duplicate/Delete and Compact tiles live in Options. Every anchored control follows window resizing; no wide management rows or persistent instruction/status paragraphs.
- Default board size 280 × 190; minimum 220 × 120. Existing saved floater geometry is retained, so an already-open large board can be resized by the user. Compact tiles are 100 × 28 and regular tiles 132 × 44. Density is per board and defaults to compact for both new and migrated boards.
- Tile editor offers Use board tile size or explicit width 48–320 and height 24–160. Save/Cancel stage these fields alongside colors, label and gesture reference; Duplicate preserves them. A sample renders the requested size without playing. The board-name editor uses a shorter window.
- Fixed, single-line ellipsized labels; no name-dependent resizing and no repeated Play label. Hover retains full label/name/path and non-ready state details. Playing uses native pressed styling. The warning marker offers full controller errors without reserving a bottom status row.
- Ordered shelf packing with mixed sizes and no scrollbar. Only the current page is instantiated. Number and Previous/Next appear only when necessary; resizing recomputes pages, clamps the current page and retains configured sizes. A tile larger than the available window is constrained for rendering, without changing its stored dimensions. Add or duplicate focus selects the page containing that gesture. Cross-page ordering remains available through Move earlier/later.
- Inventory drag/drop is handled before child dispatch. The previous scroll container always returned handled even for rejection, and mouse-opaque child views could also swallow drops. Item source/type/reference/link validation, duplicate handling and login/session guards are retained; a drop adds a tile and never plays it.
- Stored schema version 2 accepts original version 1 collections. Presentation fields are bounded and validated; no Inventory or gesture contents are introduced. Downgrading to the original version-one board implementation after saving new presentation data is unsupported; that older reader preserves unknown data rather than replacing it.

Actual checks and pending native acceptance are in the validation document. No native viewer build was run for this refinement.

The compact board's entire frame is now positioned at runtime from final window dimensions and the actual skin header height. This accounts for the viewer's legacy header stretch, which leaves initial XML controls unmoved, and prevents tiles from covering the control strip. Page controls are footer-local; the XML Next fallback uses negative right-edge anchoring. Final-frame, populated-tile and click-area bounds checks are recorded in the validation document; native Windows acceptance remains pending.
