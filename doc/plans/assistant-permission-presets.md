# Assistant permission presets and session settings — item 5

Planning only. This document authorizes no implementation. Baseline: unmerged [PR #1](https://github.com/DrCum/Folderstorm/pull/1), head `65e6ebd115`. Implement as a separate follow-up; do not add this feature to PR #1 while its local build is being evaluated.

## User experience and fixed scope

Extend PR #1's Local assistant > Permissions page with a preset selector, a short derived summary, and a collapsed **Individual permissions** section. Keep the enable switch. Add **Use these settings for this session only**, covering both access and permissions. Help: “Your saved settings resume when the viewer restarts.” This wording remains accurate when saved access is already enabled; session-only does not secretly change the next-start preference to Off.

The main selector contains **Read only**, **Ask before changes**, and **Custom**. Choosing Custom merely expands the individual controls; it does not restore an earlier map or introduce a separate preset configuration. Any individual edit refreshes the derived selector and summary. Existing mixed defaults display Custom on upgrade. Opening this page, collapsing rows, or viewing a preset never changes a setting.

| Class key | Read only | Ask before changes |
| --- | --- | --- |
| `read` | `allow` | `allow` |
| `camera`, `create`, `edit`, `move`, `trash`, `nocopy`, `wear`, `links` | `deny` | `ask` |

Camera movement and picture capture are included in Ask before changes; say that explicitly in the summary. Read does not gain an Ask option. Permanent deletion remains unavailable and is not a preset key. No Allow-everything preset and no changes to existing fallback/default values are included.

Derive the preset from the nine effective values, using the same valid-level and fallback rules as the bridge. Exact Read-only/Ask matches get those labels; other combinations get Custom. Unknown map keys must survive edits and preset application. A future permission class must be explicitly incorporated into the preset descriptor before claiming the broader preset's meaning for that class.

## Existing implementation touchpoints

- `indra/newview/fspanelpreferencelocalassistant.{h,cpp}`: PR #1's nine rows, `onLocalAssistantToggled()`, `onLocalAssistantPermission()`, `snapshotLocalAssistant()`, `restoreSavedControl()`, status, and diagnostics lifecycle. These currently write saved control values directly on editing.
- `skins/default/xui/en/panel_preferences_local_assistant.xml`: Permissions inner tab and scroll container. Add the compact summary without shrinking Setup or requiring all nine rows to fit at once. English fallback and default/Starlight CUI/Vintage registrations come from PR #1.
- `indra/newview/fseventapibridge.cpp`: `CLASS_DEFS`, `effective_level()`, `notePermissionClassesChanged()`, pending `ApprovalGate`, and active `ExecutionGate`. Preserve compound permission, quote-cap, and revocation behavior.
- `indra/llxml/llcontrol.{h,cpp}`: runtime/saved control layers. `setValue(value, false)` changes the runtime layer; `getSaveValue()` is what `saveToFile()` serializes. `setValue(value, true)` also removes unsaved layers even if the effective value did not change. Writing an equal unsaved value need not create an override layer; scope cannot be inferred solely from `hasUnsavedValue()`.
- `indra/newview/llappviewer.cpp`: existing `--mcp-api`/temporary control overrides and bridge/permission signal hookups. Do not create a second enable flag or bypass these hooks.
- `indra/newview/llfloaterpreference.cpp`: on open, `saveSettings()` captures Cancel's baseline; `onBtnOK()` calls `saveSettings()` before `apply()` and writes settings afterwards. `LLPanelPreference::postBuild()` also calls `apply()`. These methods must not be mistaken for a unique explicit acceptance event.
- `tools/fs-mcp/internal/viewerapi/policy.go`: existing nine-class wire policy; preset names need not become MCP policy fields.

## Model and persistence contract

Use a small viewer-independent preset/map helper plus a viewer-owned session controller for accepted scope. The panel owns a pending editor transaction; destroying/recreating the preferences panel must not lose an accepted session scope. Keep `EnableLocalEventAPIBridge` and `LocalEventAPIPermissionClasses` as the only persisted values. Preset name and session scope are derived/runtime state, not additional saved truth.

At opening, capture both controls' saved value, effective value, and override state, plus accepted scope. Initialize pending controls from effective values. If startup has an unsaved bridge or policy override, show session scope and its source indication without promoting it into the saved layer. Existing saved users begin in saved scope.

Treat equal-valued overrides explicitly: a control can have an unsaved layer equal to its saved value, but reconstructing that layer using an equal `setValue(..., false)` may do nothing. Restoring the saved/effective pair must not manufacture a transient opposite value merely to force `hasUnsavedValue()`. Keep scope/source metadata in the session controller and use it for the status indication. Preserve actual existing layer shape when possible; require saved/effective equality and correct scope even when the control implementation elides a redundant layer. No global `LLControlVariable` behavior change is part of this feature.

All editor changes preview through unsaved values, including changes intended for saved scope. This avoids autosave persisting an edit before acceptance and allows the user to switch to session-only after editing without already having altered saved values. Build a preset map in memory and write it once, preserving unknown keys; one map change produces one policy-generation update rather than nine transient policies.

Add an explicit assistant acceptance hook called by `LLFloaterPreference::onBtnOK()` after outstanding text input commits and before `saveSettings()` captures the new baseline. If another explicit Apply entry point is provided, call the same hook there. Do not commit from `postBuild()`, `onOpen()`, generic `apply()`, or baseline capture. No global preference-lifecycle refactor is needed.

| Event | Required behavior |
| --- | --- |
| Edit preset, individual row, access, or scope | Preview effective state; saved values stay at the last accepted saved baseline. |
| Accept with saved scope | Explicitly promote the visible access/policy pair with `setValue(..., true)`; clear their old unsaved layers; update accepted scope and Cancel baseline. |
| Accept with session scope | Preserve both saved layers; retain the current pair as unsaved effective state and accepted runtime scope. |
| Change session scope back to saved, then Cancel | Restore the last accepted session state; no promotion. |
| Change session scope back to saved, then accept | The deliberate saved-scope choice promotes the displayed values; explain this beside the checkbox. |
| Cancel or ordinary Preferences close | Restore both saved/runtime layers and scope to the last accepted baseline. Other panels' settings are unaffected. |
| Close/reopen Preferences | Accepted session state survives; unpublished editor changes do not. |
| Logout/relogin within the same process | Session scope remains a viewer-process setting. Login-specific inventory availability still changes as usual. |
| Viewer restart | Saved values resume. Re-supplying `--mcp-api` creates a new command-line override normally. |

Order updates safely: when turning access off, disable it first; when enabling access, install the policy before enabling the bridge. Cancel restores the pair through the same rules. Restoring a setting never resurrects approvals or active operations that PR #1 already cancelled.

Detect changes made by Debug Settings or another settings controller while Preferences is open. Refresh clean editors immediately. If an external change conflicts with a dirty assistant transaction, discard that pending transaction, adopt a fresh baseline, and show “Assistant settings changed elsewhere; review your choices again.” Do not overwrite a newer external policy on Cancel or acceptance. Own-setting signal writes require a reentrancy guard; no raw security settings enter UI history/workspace profiles.

## Delivery stages and ownership

1. **Pure preset model.** Factor/reuse the viewer's class metadata so the panel, preset matching, and bridge cannot drift. Keep Go's published wire semantics unchanged. Add tests for effective fallback matching, read's lack of Ask, preservation of unknown keys, and exact preset maps.
2. **Session editor transaction.** Implement pending versus accepted scope, explicit acceptance hook, runtime-preview writes, and layer-preserving Cancel. Add tests against real `LLControlVariable` behavior for equal-value writes, saved layers, and settings serialization. This stage is required before displaying the session checkbox.
3. **Preset UI.** Add collapsed details, concise summaries, scope help, search terms, keyboard flow, and localized strings. Refresh status/diagnostics through PR #1's existing signals.
4. **Acceptance.** Exercise a live open approval and active link replacement while applying Read only. Denial must remain terminal after switching back to Ask or cancelling Preferences.

One UI/policy agent owns this feature. Coordinate its small `llfloaterpreference.cpp` acceptance hook with item 7's workspace transaction work. Neither track changes the other's settings baseline or ownership.

## Acceptance checks

- Every known modifying class is Never under Read only and Ask under Ask before changes; read stays Allow. Thumbnail/texture compound requirements and paid-upload prompts still behave as PR #1 defines.
- Upgrade with mixed/default/missing/invalid keys preserves behavior; opening Preferences performs no writes. Preset application is a single policy-map transition.
- Saved-off → enable with session-only → accept → settings-file write → restart yields saved-off and the original saved map. Repeat with saved-on and different session permissions; saved-on resumes.
- Equal-runtime values, unsaved command-line access, map-only override, switching scope twice, Cancel after acceptance, panel recreation, and conflicting external settings writes all preserve the correct saved/runtime pair.
- Cancelling a pending preview restores the UI/policy baseline, but cannot revive old approval callbacks or cancelled asynchronous mutations.
- Minimum Preferences size, multiple UI scales, all three skins, English language fallback, settings search, and keyboard-only operation remain usable. Expanded rows scroll; collapsed summaries describe the actual effective map.

## Branch and merge boundary

Implement after PR #1 merges, or on a separate feature branch stacked on its current head for early testing. An early stacked PR targets PR #1's feature branch so its diff contains item 5 only; it remains dependent and unmerged until PR #1 is accepted. After PR #1 merges, rebase/retarget to main using the actual resulting merge/squash commit, then repeat integration checks. Do not duplicate PR #1's UI extraction or permission fixes and do not merge/rebase PR #1 merely to start this work.
