# Folderstorm workspace context, monitor recovery and snapshot plan

Status: implemented on `feat/workspace-context`, based on PR #6 head `658e5f2d58` (which includes PR #5). Current main remains `33e371fb3f`; PR #5/#6/#7 were still open when checked. Snapshot, schema, runtime/UI and MCP work use separate commits. Focused checks passed; native Windows/skin acceptance is pending. No viewer build or packaging was run.

Implemented scope and actual validation: [workspace-context-validation.md](workspace-context-validation.md). Graphics/camera use supported subsets; HUDs are additive and Previous does not detach them. Recovery fits supported utility windows inside the viewer, without relocating the main OS window. Portable exports omit HUD references.

Authorized follow-up additions are also implemented in PR #8: the switcher's save/capture section is collapsed by default and reflows on demand; **Apply only… / Apply group** loads a saved camera, graphics, window arrangement or other available group through the shared controller without editing the definition. Full switching/shortcuts/MCP retain all-group behavior. Partial combinations use an unsaved active marker, Previous and explicit report details. Focused subset/serialization and XUI bounds checks passed; native GUI acceptance remains pending.

## Scope and evidence

Extend workspaces with optional graphics settings, camera settings and selected HUD attachments; recover arrangements after monitor changes; add MCP workspace/layout control; fix the in-viewer snapshot capture and distorted preview when a custom viewport is enabled.

Inspected the combined local source at `/workspace/Folderstorm-theme-build` (head `f9bd52f738`) and the theme branch at `/workspace/Folderstorm-skins` (head `cefc464edb`). These are local inspection baselines, not a claim that every dependency is merged. Before implementation, check current main and PR #5–#7 merge/head states; create an isolated follow-up branch with the required dependencies and preserve user edits/published history.

Source-backed findings:

- `FSWorkspaceLayout::Workspace` currently covers chrome, supported windows, optional Inventory folder context and toolbar sets. Graphics, camera and HUDs are not part of its schema. Schema version 1 and its component mask are validated strictly.
- `FSWorkspaceController` supplies shared switching, Preferences transactions, Previous, startup restore, modified detection, Update current, fitting and restore reports. Extend these rather than creating a second apply path.
- `LLPresetsManager` already manages graphics and camera presets. Graphics presets contain settings such as FullScreen and CameraFieldOfView; loading an entire preset blindly would conflict with monitor recovery or camera choices. Its current load method returns no success result.
- The MCP sidecar registers Inventory, appearance and camera tools; no workspace/layout switching tool was found. `fseventapibridge.cpp` also limits external API access to Inventory, appearance and camera. New MCP tools require a viewer endpoint and permission integration.
- The underlying snapshot API already accepts `viewport_only`. The in-viewer live preview does not pass that argument, and several thumbnail/aspect calculations use full-window dimensions. With hidden UI, `rawSnapshot` uses the full window unless viewport capture is explicitly requested. This is a plausible explanation of the reported framing and squashed preview, not a confirmed reproduction.
- `LLWindow::getMonitorRectsInClient`, the viewport editor and `FSWorldViewGeometry` already provide monitor/client geometry. Workspace loading already fits saved windows; automatic live recovery needs separate change detection and fitting.

## Shared contracts

- Keep every new capture/restore group optional; older workspaces retain their existing behavior. Default graphics/camera capture Off, HUD selection empty, automatic monitor recovery Off. Provide a manual recovery action regardless of the automatic setting.
- Preserve the existing supported-window limits, skin resources, Preferences OK/Cancel, account/session guards, restrictions, capture choices on Update current, and startup-readiness checks.
- Use named Graphics, Camera and HUD groups in selective restoration. Define which fields each group owns so graphics and camera cannot overwrite each other. Layout-only switching stays limited to viewport/bars.
- Use an explicitly versioned workspace schema accepting old version-1 saves. Reject unknown versions, oversized records, malformed types and invalid references. Extend export/import version checks too; do not merely widen the component mask and let older viewers silently accept partial profiles.
- Saved workspace records must not contain conversation content, Inventory contents, search filters, item selections, arbitrary setting names, scripts, asset bytes, bearer tokens or arbitrary filesystem paths.
- Extend the session-only Previous baseline and modified comparison only for captured groups. Preserve unsaved graphics/camera values, not just active preset names. HUD changes require reviewed appearance operations, so Previous must not promise an instantaneous or unrestricted reversal.
- Keep the read-only diagram side-effect free. Display attached graphics/camera choices and HUD names as annotations; opening a preview must never apply them.
- Extend restore reports for missing presets, hardware-limited graphics, restricted camera changes, missing HUD items, attachment limits, pending/failed attachment operations and monitor fitting. Distinguish accepted requests from observed completion.

## 1. Snapshot viewport framing and preview repair

Goal: the snapshot tool photographs the chosen world viewport and shows an accurate, undistorted preview at the requested output resolution.

Implementation:

1. Trace ordinary, simple and freeze-frame snapshot paths plus disk/Inventory destinations. Reproduce with a wide window and a narrow custom viewport; the user's screenshots can refine reproduction later without blocking independent work.
2. Define one capture-source helper: World viewport or Full window. Default to World viewport for an active custom viewport; when no custom viewport is active this covers the usual world view. Offer Full window explicitly for users who want it, preserving UI/HUD toggles as separate choices.
3. Pass the source consistently into full capture and thumbnail capture. Keep existing MCP explicit `viewport_only` behavior backward compatible.
4. Separate source geometry in raw pixels, destination image resolution, UI-scaled preview bounds and letterboxed image bounds. Use the source rect's offset as well as its size, not a crop of an already distorted full-window render.
5. Use the same source rect/aspect policy for current-resolution choices, Keep aspect, framing overlays, collapsed/expanded thumbnails, freeze-frame display and final capture. Show UI captures and Include HUD must honor the selected source instead of silently changing it.
6. Invalidate preview geometry when the viewport, layout, window size or UI scale changes. Preserve render state, camera projection, HUD/UI flags and viewport on all success/error paths, including high-resolution tiled capture.

Touchpoints: `llfloatersnapshot.*`, `llsnapshotlivepreview.*`, `llfloatersimplesnapshot.*`, `llviewerwindow.*`, snapshot XUI and the existing snapshot geometry helpers.

Acceptance: normal and custom viewport previews match saved framing without stretching; exact custom resolution remains exact; portrait/landscape, Keep aspect, Show UI/HUD and no-custom-viewport behavior remain usable. Check tiled/high-resolution code by focused inspection; native confirmation uses only a few snapshots.

## 2. Optional workspace graphics settings

Goal: Driving and Photography workspaces can select different rendering settings without moving the viewer between display modes.

Save/edit UI:

- Graphics: Do not change / Capture current / Use existing graphics preset.
- A preset chooser shows existing names. Explain that Capture current preserves the workspace's settings while a named preset follows future edits to that preset.
- Existing workspaces and new saves default to Do not change. Both Preferences and the quick-save switcher expose the choices, with a compact advanced section if needed.

Implementation:

- Define a typed, bounded allowlist drawn from the existing graphics preset controls. Include useful draw-distance, shadows, reflection, quality and frame-limit controls. Exclude fullscreen/window placement, startup-only/restart-required values and camera-owned settings.
- Capture only valid live values for Capture current. For named presets, resolve a legitimate preset name through the manager, parse/validate only the supported subset, and expose an apply success/result API. Never use imported names as arbitrary file paths or load unrestricted settings from a preset file.
- Reuse hardware capability clamping and existing refresh/change notifications. Coalesce expensive refreshes; no per-frame polling or repeated graphics reload during fitting.
- On missing/corrupt presets, leave the current graphics unchanged and record a report entry. Applying a named preset must not create, overwrite or silently select a replacement preset.
- Preferences preview records all touched effective values and active-preset metadata; Cancel restores those values. Previous and Last capture bounded live values. Update current preserves its selected capture mode.

Acceptance: two workspaces select different graphics; viewport/window mode is unchanged; old saves work; custom unsaved settings return on Cancel/Previous; deleted presets and unsupported hardware produce useful reports.

## 3. Optional workspace camera settings

Goal: remember a driving or photography camera setup that remains useful after moving to another region.

Save/edit UI: Camera: Do not change / Capture current settings / Use existing camera preset.

Implementation:

- Reuse the camera preset system, with a strictly typed subset for avatar-relative camera/focus offsets, camera distance behavior, field of view and supported camera mode/preset selection.
- Do not serialize absolute region/world camera positions into named workspaces. A relative camera preset is distinct from restoring an arbitrary free-camera photograph. Exact transient pose can remain a session-only Previous baseline where supported.
- Give Camera ownership of camera fields even where existing graphics presets include them. Validate named preset existence and apply only supported values through existing camera APIs.
- Honor RLVa camera locks, Mouselook and scripted/vehicle camera behavior; skip/report conflicting parts instead of forcibly exiting a mode or overriding a restriction. If a pose cannot apply now, do not leave a stale callback that unexpectedly applies later.
- Include the new group in Preview/Cancel, Previous, Last, startup, modified comparisons and Update current. Restore compatible graphics first, then camera, followed by the final viewport-dependent refresh.

Acceptance: avatar-relative setups work after teleport; distance/FOV choices survive switching; Preview Cancel/Previous restore unsaved compatible settings; missing presets and locked/scripted camera cases report accurately.

Graphics and camera can share one schema/controller commit, followed by separately reviewable UI and behavior commits.

## 4. Monitor and display-scale recovery

Goal: after a monitor disconnects or display scaling changes, the current arrangement remains reachable without overwriting saved workspaces.

UI: Recover windows now plus an optional Recover arrangement when displays change setting. Explain that recovery changes the live arrangement only.

Implementation:

- Reuse platform monitor/client geometry and existing fitting helpers. Distinguish actual monitor topology/DPI changes from ordinary window movement, resizing or a translated client-space monitor list. Prefer platform notifications; if a fallback poll is necessary, make it infrequent and cheap.
- Capture live supported-window geometry before a change where available; debounce event bursts and wait for the main client frame and scale to stabilize. Fit visible windows with reachable title bars and valid minimum sizes, recompute smart bars/gutters, and ensure the world viewport remains usable.
- Scope v1 to the supported workspace windows and existing chrome/viewport controls. These floaters live inside the viewer; account for the main viewer window's new client size rather than treating them as separate OS windows. Evaluate main-window relocation through platform APIs separately and move it only if genuinely inaccessible.
- If monitor geometry is unavailable, offer manual fitting within the current usable viewer frame and report the limitation. Do not fabricate monitor mappings or write display identities into portable exports.
- Do not restore graphics/camera/HUDs, reopen closed windows, replace named profiles, or automatically choose a workspace as part of recovery.
- During Preferences, defer automatic recovery while the preview transaction is open and resolve it after OK/Cancel. Preserve a session-only pre-recovery arrangement through the existing Previous mechanism, without stacking multiple snapshots for one event burst. Account for restoring Previous on a still-smaller display.
- Refresh active/modified status and report meaningful adjustments once. Do not periodically force the user's subsequent manual placements back into bounds.

Acceptance: disconnect/reconnect, different DPI and negative monitor coordinates leave a usable layout; ordinary moves do not trigger recovery; manual fitting works without monitor support; saved workspaces and graphics/camera/HUD choices stay untouched.

## 5. Selected HUD attachments in workspaces

Feasibility: yes. The viewer already exposes attachment points and Inventory item references. Save a desired list of selected HUDs, not the objects themselves or their internal state.

Save/edit UI:

- Remember selected HUDs, default Off, with a checklist of currently attached HUD names and attachment points; select none initially and offer Select all HUDs explicitly.
- Default restore behavior: add missing selected HUDs. Do not remove unrelated HUDs, body attachments or clothing.
- If a later explicit Replace workspace HUDs option is included, define its managed scope clearly and review detachments. Do not interpret the selected list as a replacement of the entire outfit.

Implementation:

- Store bounded Inventory item references (proposed maximum 64, subject to the viewer's attachment limits), validated HUD attachment points and optional display names. Resolve Inventory links to stable underlying items; reject non-HUD/non-object targets, temporary attachments without an Inventory item, invalid IDs and duplicate selections.
- Verify that the referenced owned item and saved HUD point are still valid at apply time. An already worn item at a non-HUD point must not be silently moved. Use existing appearance/attachment APIs with additive semantics, preserving multiple items on a point where supported.
- Apply only after Inventory, avatar and session are ready. Use session/generation guards, bounded wait/timeouts and one attachment operation batch. Handle locked attachments/folders, unavailable assets and capacity limits through the restore report.
- Preference preview and diagram preview show the intended HUD list without wearing/detaching. Commit HUD application only on accepted OK or a reviewed quick switch. Cancel must not require reversing server-side attachment operations.
- Startup HUD restoration offers a review after login instead of silently changing appearance. Previous may offer to undo only session-tracked changes made by this workspace controller; review the exact actions and never remove later manually added HUDs.
- HUD selection changes participate in Update current and active/modified status, using Inventory/attachment notifications rather than a per-frame tree scan. Completion is asynchronous; report Pending until attachments are observed, then Applied/Skipped/Failed as appropriate.
- Portable exports omit HUD item references by default, as they already omit Inventory folder references. Explicit account-specific transfer must warn and validate destination account before applying any HUDs. Imported workspaces must never auto-attach on import.
- HUD script state, internal pages/buttons, permissions and edited object transforms are outside this feature. Saving a HUD reference cannot serialize those states.

Acceptance: choose one of several worn HUDs, save, detach it, then restore that workspace and observe it reattach; unrelated HUDs/clothing stay in place; missing/locked/temporary HUDs report correctly; cancelled previews and default exports never change or expose attachments.

## 6. MCP workspace and layout tools

Current answer: workspace/layout switching is not exposed through MCP in the inspected source. Add a small dedicated viewer API and sidecar tools using the shared controller.

Initial tool surface (proposed names):

- `workspace_list`: list existing account-local workspaces/layouts, favorites, capture groups and availability.
- `workspace_status`: active entry, modified state, Previous availability, pending restore and last restore report.
- `workspace_preview`: read-only fitted geometry and a summary of graphics/camera/HUD effects for an existing entry.
- `workspace_apply`: apply a saved workspace or layout by explicit kind and exact existing identifier/name.
- `workspace_previous`: return to the session-only previous arrangement.

Keep creation, replacement, import, arbitrary settings mutation and raw HUD IDs outside the initial MCP tools. The user's immediate need is to ask an assistant to switch to a saved setup.

Implementation:

- Add an explicit `LLWorkspace` event API and bridge allowlist/classification entries; sidecar registration alone is insufficient. Reuse `quickSwitchWorkspace`, `quickSwitchLayout`, `returnPrevious`, diagram/report APIs and the account/session guards on the main thread.
- Publish a versioned capability so a sidecar connected to an older viewer returns Unsupported rather than trying settings injection or another mutation path.
- Add a Workspace switching permission with Allow/Ask/Deny, default Ask, to the existing local-assistant permission UI/presets and effective-policy reporting. Read-only tools require Read. Camera restoration additionally requires Camera; selected HUD actions require Wear. Evaluate compound permissions for the actual selected restore groups, including Previous's live snapshot.
- Resolve the saved definition and effective operations before approval; show profile name, restored groups and exact HUD changes. Bind authorization to viewer PID, account/session, policy generation and profile revision. If the profile or required operations change before acceptance, invalidate/review again.
- Do not accept caller claims of approval, disable restrictions, switch while Preferences has pending changes or use unrestricted floater/setting identifiers. Target a particular viewer when more than one is running; retain existing bearer/loopback authentication.
- Return structured Applied/Adjusted/Skipped/Pending outcomes and a report identifier. Avoid claiming successful HUD attachment when only the request was submitted. Exclude conversation/Inventory contents and tokens from responses/logs.

Acceptance: an assistant can list, preview and switch an existing Driving workspace, apply a layout and return to Previous; wrong names, old viewers, multiple-viewer targeting, Deny/Ask, profile changes and logout are handled correctly; restricted camera/HUD groups do not slip through Workspace permission alone.

## Delivery and focused verification

Suggested order: snapshot repair; shared graphics/camera schema and controller integration; graphics/camera UI; monitor recovery; selected HUDs; MCP workspace tools. If snapshot reproduction needs screenshots, continue the other independent checkpoints and retain the snapshot item as unverified.

Use an isolated follow-up branch and separately reviewable commits. Do not modify unrelated feature history, merge PRs, or publish a new PR solely for this plan. At implementation time preserve the user's existing budget limits: no subagents, large tests, viewer builds, packaging or GitHub builds without a new explicit request.

Focused checks for implementation:

- Parse changed XUI/settings; inspect registered callbacks, skin fallbacks and minimum-size layouts.
- Extend existing small workspace serialization/geometry checks for schema compatibility, invalid masks, bounded presets/HUD lists and Previous baselines.
- Use small pure snapshot geometry checks for nonzero viewport origins, different aspect ratios and scaled preview fitting; inspect affected capture paths and state restoration.
- Add focused display-change/fitting cases and verify recovery cannot save over named profiles.
- Only when MCP Go paths change, run the targeted workspace-tool, capability and compound-permission tests. Do not run unrelated Go tests or a broad suite.
- Record native Windows acceptance separately: snapshots, two graphics/camera workspaces, HUD attach/skip cases, Preferences Cancel, startup/Previous, monitor disconnect/DPI and one alternate skin. No claim of native success until observed.

Completion checklist: all six sections implemented or explicitly reported incomplete; existing saves and UI transaction behavior preserved; accurate restore reports; targeted checks recorded; remaining Windows/skin checks listed in the eventual PR and final report.
