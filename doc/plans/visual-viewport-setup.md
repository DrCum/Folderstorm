# Visual, monitor-aware viewport setup

Status: implementation plan only. Covers review item 6. No viewer code changed.

## Outcome and scope

In Preferences > Move & View > Viewport, show a miniature diagram of the viewer client area, its normal world area, the selected world viewport, and the monitors intersecting the window. A user can drag four viewport boundaries or select a visible monitor and press **Use this monitor**. Keep numeric percentage controls, Full window, and the existing fixed-fraction presets as optional shortcuts.

The first release stores the same percentage inset settings as today. **Use this monitor** fits the current window's world area to the selected monitor once; moving or resizing the window later preserves percentages and redraws the diagram. Explain this directly in the UI: “Fits the part of this monitor covered by the viewer window. Fit again after moving or resizing the window.” Do not add persistent monitor bindings, automatic window spanning, complete workspace saving, or new render/camera behavior in this feature.

## Existing implementation to preserve

- `skins/default/xui/en/panel_preferences_move.xml`, `tab-viewport`: enable checkbox, four editable sliders, Mouselook option, four preset buttons. Numeric sliders stop at 90%; their explanation says opposing insets are reduced to leave 5%.
- `LLFloaterPreference::onWorldViewPreset()` in `llfloaterpreference.cpp`: writes `FSWorldViewInsetLeft/Right/Top/Bottom` and `FSWorldViewEnabled` directly, so changes preview immediately.
- `LLViewerWindow::updateWorldViewRect()` in `llviewerwindow.cpp`: derives a base rectangle from `mWorldViewPlaceholder` or the whole raw client window, then applies custom insets. The base is not always the entire window. The internal `readWorldViewInsets()` clamps individual values to 95% and scales opposing margins to a combined 95%. `applyWorldViewInsets()` rounds into raw pixel coordinates. `getWorldViewRectRaw()`, `getWorldViewRectScaled()`, `getWindowRectRaw()`, `getDisplayScale()`, and `setOnWorldViewRectUpdated()` already expose the resulting geometry.
- `FSChromeLayoutController::captureSnapshot()/applySnapshot()` and `applyProfile()/saveUserProfile()` use the same inset settings; chrome placement updates through the existing world-rectangle signal. `Snapshot` currently stores no monitor identity. `FSWorldViewInMouselook` is not part of that snapshot.
- `LLPanelPreference::saveSettings()/cancel()` snapshot control-bound values. Profile-map and active-profile settings are changed by callbacks, so they require explicit inclusion in the preferences transaction if Cancel is to roll back all layout actions.
- `LLWindow` provides coordinate conversions and a resolution-string list, but no monitor bounds. A list of resolutions cannot establish monitor placement or an intersection with the client area.

## User interaction

1. Add an aspect-correct diagram above the retained numerical controls. Draw client boundaries, the normal world rectangle, selected viewport, and reserved areas with labels and distinguishable fills/outlines. No world rendering or screenshot is needed inside the diagram.
2. Draw monitor boundaries in their actual positions relative to the client; clip outside portions to the diagram. Give intersecting monitors selectable labels such as “Display 1” plus a name when available. Show an explicit selection outline rather than relying on color. Select the monitor with the largest overlap with the current world viewport initially, using a deterministic tie break.
3. **Use this monitor** acts on the selected monitor, never on the monitor containing the Preferences floater or the mouse. The action uses only the intersection of the selected monitor and the normal world rectangle. If the viewer is partially stretched onto a monitor, fit that visible portion. A monitor with no viable overlap has a disabled action with a short reason.
4. Drag one of four edge handles to change its margin. Support mouse capture, release outside the control, and Escape to restore the state at drag start. Keep opposite edges stationary and stop before the minimum viewport size. Clicking inside the diagram selects a monitor; it does not reposition the viewport accidentally. Numeric controls remain the keyboard-accessible alternative.
5. Dragging or fitting enables the custom viewport. Turning it off draws the actual full normal world area and retains the configured insets for reuse. Keep Mouselook's existing checkbox and behavior.
6. All paths—dragging, sliders, preset buttons, profile application, and Debug Settings—refresh the diagram from the resulting settings/rectangle. Do not maintain a second persisted source of geometry. Redraws do not write settings.
7. Use a compact or scrolling panel layout so the diagram and numeric controls fit supported preferences sizes and increased UI scales. Localize new labels, tooltips, disabled reasons, and help text through XUI strings; audit translated variants for overlays/stale positions when the English base panel changes.

## Geometry contract

Introduce a small default-unsupported query on `LLWindow` (proposed name `getMonitorRectsInClient()`). It returns monitor descriptors with session-local IDs, optional display names, and full monitor rectangles already expressed in **raw drawable client coordinates**, bottom-left origin and rectangle-edge semantics. Include a valid/unsupported result, not an invented single-monitor result. Use full monitor bounds rather than desktop work areas: the intersection with the actual client already excludes title bars and areas the window does not occupy.

The OS backends own all transformations from desktop coordinates and logical units to this contract. Preferences must not combine `getPosition()` and `getSize(LLCoordScreen*)` to guess the client bounds. Retain off-client monitor coordinates until the drawing/intersection step. Do not persist session IDs in settings or profiles.

Extract the existing base-world-rectangle calculation from `LLViewerWindow::updateWorldViewRect()` into a side-effect-free accessor/helper (proposed `getWorldViewBaseRectRaw(bool use_full_window)`). Share its implementation with rendering so preferences can obtain the pre-inset rectangle without temporarily disabling the viewport. The diagram normally edits the standard UI-visible base; current Mouselook/UI-hidden state is a temporary rendering mode and must not overwrite stored insets.

For base rectangle `B` and selected monitor rectangle `M`, compute `T = intersection(B, M)`. Reject empty intersections and targets smaller than 5% of `B` on either axis; report “Too little of this monitor is covered by the viewer window” rather than stretching the fitted viewport across the monitor edge. Insets in percent are:

```text
left   = 100 * (T.left   - B.left)   / B.width
right  = 100 * (B.right  - T.right)  / B.width
bottom = 100 * (T.bottom - B.bottom) / B.height
top    = 100 * (B.top    - T.top)     / B.height
```

Place normalization and rectangle-to-insets/insets-to-rectangle conversion in a small pure helper, with the current renderer's normalization and rounding rules shared rather than copied. Round pixel edges once at the renderer boundary; retain sufficient float precision when storing fitted insets. Diagram handles clamp combined opposing margins to 95%. Align numeric sliders' per-edge maximum with the existing runtime 95% so a valid fitted slice between 5% and 10% can be represented. Preserve proportional normalization for legacy or Debug Settings values above the combined limit. Draw effective normalized geometry and explain normalization when raw numeric values exceed the combined limit; do not silently rewrite legacy settings during a redraw or profile load.

## Platform implementation and refresh

| Backend | Implementation task and limits |
| --- | --- |
| Windows, `llwindowwin32.{h,cpp}` | Enumerate bounds with `EnumDisplayMonitors`/`GetMonitorInfo`; existing `LLMonitorInfo` only returns resolution strings. Translate desktop edges to client coordinates using the native client origin (`ClientToScreen`) and drawable size, with the existing DPI-awareness policy. Convert top-left origin to bottom-left edge coordinates explicitly; existing `convertCoords()` methods are point conversions containing a `-1` and must not be blindly applied to rectangle edges. Keep queries/caching on the correct side of the existing window-thread/main-thread boundary. Validate mixed 100%/150%/200% displays and monitors with negative desktop origins. |
| macOS, `llwindowmacosx.{h,cpp}`, `llwindowmacosx-objc.{h,mm}` | Enumerate `NSScreen` frames and convert through the actual window/content view into drawable backing coordinates. Screen geometry is in points, but `getSize()` uses `getBackingViewRect()` and raw rendering uses backing pixels; transform edges using the window/view's backing conversion, not each monitor's independent scale factor or `getPosition()+size`. Existing coordinate helpers also change the screen Y origin. Preserve the existing rendering surface's single backing-coordinate system across mixed Retina/non-Retina monitors. |
| Linux SDL2, `llwindowsdl2.{h,cpp}` | Use `SDL_GetNumVideoDisplays`/`SDL_GetDisplayBounds`, window client placement, and logical-window/drawable ratios where meaningful. The existing SDL2 screen/window `convertCoords()` methods are identity mappings and are unsuitable for fitting a windowed client. Use a scoped implementation for this query instead of silently relying on those methods. On environments such as Wayland where global window placement is unavailable or not trustworthy, return unsupported. |
| Linux SDL1, headless and other unsupported backends | Keep the new base virtual implementation returning unsupported, so optional configurations still compile. The diagram and numerical controls work without monitor overlays; disable monitor selection/fitting with “Monitor positions are unavailable; use the diagram or percentages.” No new OS dependency solely to bypass this fallback. |

Refresh when the Viewport panel becomes visible and when its client size, placement, world base, UI scale, or display topology changes. Existing relevant hooks are `LLViewerWindow::handleResize()`, `handleDPIChanged()`, `handleDisplayChanged()`, and `handleWindowDidChangeScreen()`; Windows already handles `WM_DISPLAYCHANGE`/`WM_DPICHANGED`. Window placement changes can occur without world rectangle changes, so do not depend only on `setOnWorldViewRectUpdated()`.

Prefer backend-cached topology with event invalidation; check current placement while the diagram is visible at a modest rate if a backend has no placement notification. Query again before **Use this monitor** and reject stale/disappeared selection. During a drag, if geometry changes, cancel that drag to its starting settings, release capture, redraw, and let the user start again. A refresh never refits or resets the viewport automatically. Unsupported/transient failure must leave current settings intact. Disconnect subscriptions and release mouse capture when preferences close.

## Preview, Cancel, and profiles

- Keep immediate preview through current settings and `updateWorldViewRect()`; the existing `FSChromeLayoutController` continues applying chrome placement. Batch a four-inset fit so only the final geometry is rendered/applied, rather than showing intermediate edge combinations.
- Include `FSWorldViewEnabled`, all four inset settings, `FSWorldViewInMouselook`, the current chrome snapshot, `FSChromeActiveProfile`, and `FSChromeLayoutProfiles` in the preferences layout baseline. Use `LLFloaterPreference::saveSettings()` and `cancel()` or a scoped preference-panel override; do not change cancellation for unrelated preferences.
- OK/Apply refresh the baseline through the existing preferences flow. Cancel and the window close button restore the last baseline, then recompute world/chrome rectangles and refresh controls. Profile save/overwrite/rename/delete during the open preferences session participates in this same transaction. Include a regression check because profile settings are not captured by ordinary bound sliders.
- Existing saved profiles remain percentage based with the same schema. Applying a profile redraws against the current window/monitors; it does not rescale to a named monitor. Saving after fitting captures the resulting percentages. Manual edits mark the displayed profile as modified so the selected saved-profile name does not imply that it still matches current values; use a visual status rather than inventing a persistent profile state.

## Implementation phases and handoff boundaries

### Phase 1: shared geometry and visual editing

Extract the base-world calculation and pure inset normalization/conversion helpers. Add the diagram control (a focused `LLUICtrl` with `draw()`, mouse handlers and capture handling), its XUI registration/build entries, and the numeric synchronization. Integrate the preferences rollback baseline. Deliver functioning visual editing on every platform with monitor fitting initially disabled. Suggested new viewer files: `fsworldviewdiagram.{h,cpp}`; keep the mathematical helper independent of viewer globals so it can be exercised by standalone tests.

### Phase 2: monitor query and fitting

Add the narrow `LLWindow` query and implement Windows first against real mixed-monitor hardware. Add the monitor overlays, selection, one-shot intersection action, and geometry invalidation. Implement macOS and SDL2 against the same contract; keep default unsupported backends buildable. OS backend work can be delegated independently once the coordinate contract and helper fixtures are fixed. Do not label a backend supported until its native geometry has been checked on that OS.

### Phase 3: integration and polish

Verify profile and Preferences lifecycle, Mouselook/UI-hidden transitions, toolbar/chat placement, resizing and hotplug behavior. Finish clipping/scrolling, tooltips, localization scaffolding, keyboard focus, and high-contrast selections. Ensure the automatic fit matches the diagram and the actual rendered world within one drawable pixel after rounding.

This plan touches `llfloaterpreference.{h,cpp}` and Preferences XUI, which are also likely to be edited by the Local Assistant preferences work. Give the diagram/control and monitor-query work separate owners, then integrate the shared preferences wiring sequentially or in one owner branch to avoid conflicting lifecycle edits.

## Validation and acceptance

Automated pure geometry cases should cover unequal 1920/2560 widths, vertically offset monitors, a monitor left of the primary, partially spanned windows, non-zero normal-world origins, asymmetric four-edge insets, a monitor spanning all of the base, no intersection, a slice below 5%, and opposing margins above 95%. Check round trips within one raw pixel, finite settings, non-empty normalized rectangles, and preservation of legacy profiles. Extend the existing standalone geometry test arrangement (`tests/test_fschromelayout.cpp`) or add a companion test compiled with only the pure helper; these tests do not establish native DPI correctness.

Native/manual acceptance on supported platforms:

- Dragging and numeric edits display the same effective viewport; camera center, HUD and picking remain centered correctly. Reserved space still accepts Inventory and other floaters.
- An unequal two-monitor stretched window fits the selected monitor's actual covered area, rather than a half-window guess. Test portrait/landscape and vertically offset displays, plus three displays with the center selected.
- Client borders/title bars and existing top/bottom viewer chrome do not offset the fit. Monitor gaps have no selectable monitor; a tiny partial overlap is explicitly rejected.
- Mixed DPI/backing scales, negative origins, maximized/borderless/fullscreen windows, and UI scale changes preserve alignment. Temporary Mouselook and UI hiding preserve the saved standard layout.
- Moving/resizing refreshes the display without changing percentages or following a monitor. Display unplug invalidates selection safely and preserves the viewport. Unsupported monitor geometry retains visual editing and shows the fallback message.
- Drag Escape, capture loss, Preferences Cancel and close restore the expected baseline; OK/Apply retain changes. Reopen confirms persistence. Cancel after applying/saving/renaming/deleting a profile restores both the layout and profile collection.
- Existing builtin and custom profiles still load; saving a fitted view and reapplying it uses stored percentages with no schema migration. A manual change displays a modified indication.
- Diagram and labels fit the smallest supported Preferences size and larger UI scales; all actions have a keyboard/numeric route. Build all configured backends affected by the `LLWindow` header, including default unsupported implementations.

No native viewer or OS display checks were run while producing this plan. Those are delivery gates for implementation, not evidence already obtained.
