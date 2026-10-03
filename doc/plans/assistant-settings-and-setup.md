# Local assistant settings and connection setup

Status: implementation plan only. Covers review items 1 and 4. Baseline: `9aa5cbb0d6`.

## Outcome

Users can find assistant controls in one preferences page, read all nine permissions without clipped explanatory text, and get a working client configuration for the running Folderstorm installation. The interface distinguishes a ready viewer from a successfully configured external assistant.

## Scope and decisions

- Add a top-level **Local assistant** preferences page. Use two inner tabs, **Permissions** and **Setup**, with the access switch and concise bridge status above them. Remove the existing assistant section from Privacy -> General.
- Keep the existing `EnableLocalEventAPIBridge` and `LocalEventAPIPermissionClasses` settings, saved values, permission defaults, and command-line override behavior. Do not introduce permission presets, a new session toggle, or automatic client configuration writes.
- Keep runtime preview and Preferences Apply/OK/Cancel semantics. Cancel restores both saved and runtime control layers, including an unsaved `--mcp-api` override. If that override is active, display a localized session-override indication.
- Use **Allow local assistant access** for the checkbox. Introductory text: **Connect your assistant separately. Choose what it can do below.** Add one concise explanation that other programs under the same OS account can use this access. Put port lifetime, discovery-file mechanics, and troubleshooting in expandable details/help.
- The first release supports configuration copy for Codex, Cursor, and Claude Code. It launches no external assistant and edits no external configuration files. Windows installer/launcher changes from review item 10 remain separate work.

## Current code and ownership

- `indra/newview/llfloaterpreference.cpp`: `LLPanelPreferencePrivacy`, its local-assistant control callbacks, `snapshotLocalAssistant()`, `restoreLocalAssistant()`, and panel injectors. Extract assistant ownership into a dedicated `LLPanelPreference` subclass; preserve the current control-layer restoration behavior.
- `indra/newview/skins/default/xui/en/panel_preferences_privacy.xml`: remove the assistant block and reclaim its layout space.
- New `panel_preferences_local_assistant.xml` and dedicated panel source/header (for example `fspanelpreferencelocalassistant.{h,cpp}`), registered in `indra/newview/CMakeLists.txt`; register the panel in default, starlightcui, and vintage English `floater_preferences.xml` definitions. Other skins inherit these definitions; verify resource resolution rather than copying every file blindly.
- Language overlays currently contain their own assistant block under Privacy. Remove those obsolete entries while introducing localized string resources/new-page fallbacks. Check overlay merging and preferences search for orphaned entries.
- `indra/newview/fseventapibridge.{h,cpp}`: add a small read-only diagnostics snapshot and sanitized startup-error state. Coordinate edits after the permissions/approvals owner stabilizes bridge changes.
- `indra/newview/llinventorylistener.cpp::status()`, `gInventory.isInventoryUsable()`, and existing background-fetch state supply login/inventory readiness. UI reading its own local state must work when external Read permission is Never.
- `tools/fs-mcp/cmd/fs-mcp/main.go`, `internal/discover`, and `internal/viewerapi`: add an explicit diagnostic CLI mode while preserving no-argument MCP stdio behavior.
- `doc/help.md` and `tools/fs-mcp/README.md`: update preferences navigation and configuration/check instructions.

## UI behavior

Permissions shows all nine rows, a consistently aligned Allow/Ask/Never choice, and brief per-row help. Read retains its current Allow/Never choices. Keep editing possible while access is off so users can configure before enabling. Use wrapping and content-aware spacing; if the available height truly requires scrolling, scroll the whole permissions content instead of burying only its lower rows. Ensure keyboard focus and preferences search reach each setting.

Setup contains a client selector, **Copy configuration**, **Check connection**, and **Open setup guide**, followed by readable results. Full generated configuration can be shown in a selectable, bounded text area. Technical details remain collapsed by default.

Status is composed of independent facts:

| Fact | Examples / rule |
| --- | --- |
| Viewer access | Off; Ready for local connection; Could not enable access (actionable sanitized reason) |
| Inventory | Log in to use inventory; Loading inventory; Inventory ready |
| Installed sidecar | Found at resolved path; Missing, reinstall/build instructions |
| Recent external request | No requests observed this session; Last authenticated external request at a timestamp |
| Connection check | Not checked; Checking; Passed for this viewer; Failed with an actionable stage |

Do not label a listening socket or an old request as **Assistant connected**. A check proves that the installed sidecar can reach this viewer at that moment; it cannot prove that Codex/Cursor/Claude has loaded its configuration. Tag diagnostic probes so they do not masquerade as external-client activity.

## Configuration and diagnostic contracts

1. Resolve the packaged sidecar beside the running installation using the actual platform package layout (`LLDir::getExecutableDir()` and existing manifest conventions). Never assume the documented Release path exists. On Windows, use the shared space-free link only if it exists and resolves to this installation's sidecar; otherwise use a client-specific invocation verified with paths containing spaces. Do not change the link or installer in this task.
2. Resolve discovery to the running viewer's actual settings directory. Configuration contains the executable command, appropriately escaped arguments, and `FIRESTORM_MCP_DISCOVERY`. Preserve existing environment names for compatibility. Never copy the discovery file or its token.
3. Serialize JSON/TOML using format-aware escaping; paths must handle spaces, Unicode, quotes, and backslashes. Codex output retains the documented tool timeout sufficient for viewer approval. Clearly label snippets as entries to add; do not suggest replacing the user's entire configuration.
4. Add a diagnostic invocation such as `fs-mcp --diagnose --viewer-pid <pid> --discovery <directory>`. It emits bounded token-free JSON and exits, with a documented nonzero failure status. It performs a minimal authenticated read-only bridge-health request, separate from inventory Read permission. Any new health operation must return only bridge/API readiness, no inventory data or bearer secret, and retain existing authorization checks.
5. Run diagnostics asynchronously via an argument-vector subprocess API, with a short bounded timeout (target 5 seconds), cancellation, and process cleanup. Do not block rendering or invoke a shell for the check. Distinguish launch failure, no discovery, rejected authorization, bridge unavailable, and inventory still loading. Retry is explicit.
6. The bridge diagnostics snapshot records effective enablement, readiness/failure category, last authenticated external request timestamp, and optionally its API/operation identifier. Do not log parameters, item names, request bodies, tokens, or full authorization headers. Refresh only while the preferences page is open and stop observers/tasks when it closes.

## Implementation sequence

1. Extract the existing controls into the new preferences page and add localized concise copy. Keep all behavior intact. Update skin definitions, language overlays, settings search, and help navigation.
2. Define the small read-only diagnostics contract with the permissions owner; add safe bridge failure reporting and local inventory readiness display.
3. Implement token-free client configuration generation and display/copy from actual installation/settings paths. Smoke-test each supported client format and the Windows path-with-spaces adapter.
4. Add the sidecar diagnostic mode and asynchronous viewer integration. Preserve the default MCP startup path and account for Read=Never.
5. Validate the combined page in real viewer builds, update documentation, and capture before/after screenshots for review.

## Acceptance criteria and validation

- Privacy General contains no assistant remnants. The new page is discoverable in preferences search and supported skins; all permission labels, controls, and help are legible at supported minimum preferences size and 100%, 125%, 150%, and 200% UI scale.
- Existing custom permissions survive upgrade. Cancel restores values after toggling/editing; Apply establishes a new rollback baseline. Opening/canceling with `--mcp-api` does not persist the session override.
- Off, pre-login, loading, ready, bind/discovery failure, and Read=Never show truthful independent states. A discovery file alone never becomes proof of inventory/client readiness.
- Generated entries parse as JSON/TOML and point at this installation/settings directory. Test spaces, Unicode, and custom user-settings paths. On Windows, prove that the real client launch convention starts the sidecar when the shared link is absent or points elsewhere.
- Diagnostics pass with a reachable bridge even when inventory Read is Never, and fail cleanly for missing binary, bridge off, stale token, wrong PID, disconnect, and timeout. Closing preferences cleans up the subprocess and observers.
- Clipboard, diagnostic output, logs, and UI never expose the bearer token. No external client config is written by this feature.
- Run existing fs-mcp tests plus focused serializer/diagnostic tests. GUI and packaged Windows/macOS/Linux checks are required; pure unit tests cannot establish layout or launch correctness.

## Handoff

One UI/setup owner implements this track. They may start the extraction while backend planning proceeds, but final bridge diagnostics edits should land after the permissions track. New status widgets consume only a read-only snapshot and must not duplicate permission logic. Review text and screenshots before declaring the UI complete.
