# Workspace context and snapshot implementation

Implementation branch: `feat/workspace-context`, stacked on PR #6 (`feat/workspace-usability`, head `658e5f2d58`), including PR #5. PR #7 themes are independent. Main and open dependency heads were rechecked before publication; existing worktrees and user plan edits were preserved.

## Completed feature checklist

- [x] Native snapshot capture and thumbnails use the custom viewport by default. An explicit Full window option is available. Current resolution, output aspect, letterboxed preview, freeze frame, guides and pixel-read origin use the selected source; renderer/viewport/tile state is restored on early failure too.
- [x] Optional graphics: Do not change, Capture current, or Use named preset in Preferences and quick save. Only a typed, bounded rendering subset applies; display mode, window placement and restart-only options are excluded.
- [x] Optional camera: the same choices for avatar-relative offsets, FOV, smoothing and compatible preset/zoom settings. Camera changes respect locks, Mouselook and scripted cameras. Workspace zoom cannot change selected-HUD zoom.
- [x] Recover windows now and optional debounced display/scale recovery, Off by default. Fits visible supported utility windows to visible monitor/client regions, keeps the live viewport reachable, preserves named files and Previous, and defers automatic recovery during Preferences. Manual recovery in Preferences rolls back on Cancel.
- [x] Select currently attached Inventory-backed HUDs in either save UI, retaining saved missing selections. Apply adds missing selected HUDs after native review/OK or viewer-owned MCP approval. Unrelated HUDs, body attachments and clothing stay worn. Reports distinguish unavailable/restricted/full-capacity/pending/observed/timeout results; callbacks are bounded to the account/session and report generation.
- [x] Versioned MCP `workspace_list`, `workspace_status`, `workspace_preview`, `workspace_apply`, `workspace_previous` on `LLWorkspace`. Workspace permission defaults Ask; camera/HUD groups require their own classes. Approval binds to exact saved definition, resolved values, worn HUD state and login; older viewers have no mutation fallback.
- [x] Schema 2 accepts schema 1 saves and validates whitelisted settings, finite ranges, preset names and bounded HUD references. Previous, Last, Update current, selective groups, modified state, previews and reports share the controller. Portable exports omit HUD references; HUD-only exports report why no portable group remains.
- [x] Compact switcher: save/capture controls start collapsed, expand on demand and reflow the scroll document without a blank gap. Favorites, full switching, Previous and utility actions remain outside the save section. Expanding/collapsing retains the current name and capture choices while the floater is open.
- [x] Apply individual groups: select a saved workspace, choose **Apply only…**, then **Apply group**. Choices include window arrangement, camera, graphics and available individual window/HUD/toolbar groups. Only groups present in the validated save are offered. Window arrangement includes its saved viewport/bars, Inventory/folders, maps, chat and toolbar sets; camera, graphics and HUDs stay unchanged. Full Switch, double-click, favorites, shortcuts and MCP still load all saved groups. Saved definitions/capture options remain untouched. Partial combinations clear the active named-workspace marker, retain session-only Previous, and explain intentionally unselected groups in the restore report; HUD-only loads keep native review/restrictions.

## Actual focused checks

- Changed settings/XUI XML parsed successfully. Source registrations and every allowlisted graphics/camera setting type were checked against viewer definitions, including Vector3D focus offsets. No skin-specific replacement of the new workspace panels was found; new resources inherit from default.
- Standalone snapshot geometry assertions passed: nonzero origins, portrait/wide/matching aspects, invalid dimensions and translated bounds.
- Standalone workspace geometry/name checks passed.
- Workspace LLSD roundtrip/validation checks passed, linked only to the existing llcommon library: schema 1 compatibility, schema 2 graphics/camera/HUD groups, malformed masks/fields, unsupported settings, NaN, bad/duplicate HUD references and bounds.
- Standalone assistant permission/editor and approval-gate checks passed. Workspace-only, Camera/Wear compound class selection, independent Deny and obsolete/revoked callbacks were covered.
- Targeted MCP Go tests passed in `internal/mcptools` and `internal/viewerapi`: workspace registration/in-memory calls, dedicated API routing, exact targets, Read versus Workspace policy, unsupported versions without fallback, annotations, policy/capability refresh and the affected camera snapshot path.
- `git diff --check` passed. Runtime API signatures, Windows numeric conversions, source offset reads, Preferences ordering and delayed HUD review were inspected.
- Follow-up switcher/group checks passed: the standalone workspace geometry/name and LLSD suites (settings-only exclusion of all Inventory/HUD/toolbar effects; window, maps, HUD and toolbar subsets; absent/empty/unknown selection rejection; full-load equivalence and source immutability). Changed XUI parsed; control names, expanded/collapsed panel bounds and default-resource inheritance were checked. The scroll document shrinks from 1160 to 649 UI units. No Go code changed in this follow-up, so the earlier Go checks were not repeated.

The targeted Go selection was:

```sh
go test ./internal/mcptools ./internal/viewerapi \
  -run 'TestWorkspace|TestToolAnnotations|TestNamedAPIRouting|TestParseStatusPolicy|TestPolicyGenerationBumpRefreshes|TestCameraSnapshot' -count=1
```

No viewer/native compilation, packaging, GitHub build, unrelated Go suite or subagent was used. These checks do not establish native rendering correctness or an observed server-side HUD attachment.

## Remaining Windows and skin acceptance

1. Use a wide viewer with an offset narrow viewport. Compare preview and saved color/depth snapshots at current/custom portrait and landscape sizes, then Full window, Interface/HUD toggles, freeze frame and one high-resolution capture. Resize/change layout and confirm preview invalidation; verify live framing after capture.
2. Save two graphics/camera workspaces using captured settings and named presets. Preview and Cancel with unsaved settings; switch and use Previous; confirm Update current retains capture choices. Delete a referenced preset and try a restricted/scripted camera, checking the report rather than expecting a forced change.
3. Select one worn HUD, save, detach and restore. Confirm review and observed Pending-to-Restored behavior, unchanged unrelated attachments, decline and Cancel, unavailable/locked item and attachment limit. A later unrelated Preferences OK must not repeat HUD review. Export and confirm HUD item IDs are omitted.
4. Enable display recovery, disconnect a monitor/change DPI and try negative-coordinate arrangements. Check title-bar reachability, viewport and smart bars, one automatic report, Previous and Preferences Cancel. Ordinary viewer moves must not trigger repeated recovery. Repeat manual recovery with automatic Off.
5. Exercise list/preview/apply/Previous through MCP with Ask and Deny for Workspace, Camera and Wear; change the saved workspace/preset or log out during approval. Check accepted versus pending results. Inspect new controls in Folderstorm/default and Anastorm at normal and larger UI scale.
6. Open the switcher at normal/minimum size and larger UI scale. Expand/collapse save options repeatedly; retain an entered name and capture choices; save successfully and check that status stays visible. Select a workspace with mixed groups: apply camera only, graphics only, window arrangement, Inventory, HUDs or toolbars, confirming other groups stay unchanged, saved definitions stay identical and Previous returns to the prior setup. Check the report's unselected groups and unsaved active marker. Check a single-group workspace, missing preset, changed/deleted definition, restriction, Preferences open/Cancel and logout; verify full Switch/double-click/favorites/shortcuts still apply every saved group. The diagram continues to preview the full saved workspace.

## Defined limits

Graphics presets restore the supported subset, not every graphics control; active preset metadata is cleared instead of claiming a complete preset load. Named camera saves are avatar-relative, not absolute free-camera photographic poses. Previous captures compatible effective camera/graphics values and unsaved window geometry; it does not detach HUDs added by a later switch. Last arrangement at orderly quit captures compatible live graphics/camera settings but does not capture HUD selections.

HUD script state, transformations, contents and internal pages are outside scope. No replace/detach mode or account-specific HUD export was added. Recovery handles supported utility floaters inside the viewer; it does not reposition the native main OS window, reopen closed windows or move unsupported editors. Automatic recovery has no monitor enumeration while Off and a two-second fallback poll while enabled. Named-preset modified indicators cache bounded resolution for up to five seconds to avoid repeated directory/file reads; real loading and MCP approval/validation always resolve fresh.

Schema-1 saves load on the new viewer. New schema-2 saves require the new viewer; older viewers reject them. Named presets remain local references and may be unavailable on an import destination. Native acceptance items above remain unverified until observed.
