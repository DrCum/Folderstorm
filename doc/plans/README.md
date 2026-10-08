# Folderstorm improvement plans

The remaining [workspace enhancement roadmap](workspace-enhancements.md) is implemented on `feat/workspace-tools`, based on merged PR #4/current `main`. It covers keyboard switching, Previous arrangement, snapping/alignment, startup restore, indicators and Update, selective groups, toolbar sets, export/import and a fitted diagram. Workspace favorites now share the landmark row using its skin style and a divider. Focused checks, the completed checklist and remaining native acceptance are in [workspace tools verification](../testing/workspace-tools-validation.md). The user authorized continuous implementation, affected-path checks and publication; viewer/packaging/GitHub builds and unrelated suites remain excluded.

The follow-up batch for items **5, 7, 8 and 10** is described in [next improvements](next-improvements.md). Its implementation and verification are recorded separately in [follow-up validation](../next-improvements-validation.md). The first batch below remains the baseline for PR #1.

Planning baseline: `9aa5cbb0d6` (2026-10-03). These documents cover the user-selected review items **1–4, 6, and 9**. They preserve the handoff plans written before implementation was authorized. The implementation is now included in the same change; see [validation and remaining acceptance checks](../assistant-viewport-validation.md).

## Feature plans

| Track | Review items | Deliverable | Owner |
| --- | --- | --- | --- |
| [Assistant settings and setup](assistant-settings-and-setup.md) | 1, 4 | Dedicated preferences page, concise copy, truthful readiness/activity, client configuration copy, connection diagnostics | UI/setup agent |
| [Permissions and approvals](assistant-permissions-and-approvals.md) | 2, 3 | All required permissions enforced, pending approvals revoked/rechecked, human-readable summaries of the exact operation | Assistant backend agent |
| [Visual viewport setup](visual-viewport-setup.md) | 6 | Interactive diagram, monitor selection with correct geometry, existing numeric controls retained | Viewport agent |
| [Safe link replacement](safe-link-replacement.md) | 9 | Confirm replacement creation before moving the original, accurate results, bounded asynchronous execution | Inventory agent |

## Recommended delivery order

1. **Permission correctness first (item 2).** Establish required-class checks and execution-time revalidation; cover Camera/Edit/Create combinations and policy changes while a prompt is pending. Keep this independently reviewable from new UI and richer summaries.
2. **Start UI extraction (item 1) and visual viewport controls (first part of item 6) in parallel.** They can progress while backend behavior stabilizes. The assistant panel should have its own source file/controller, leaving only registration in `llfloaterpreference.cpp`, to reduce conflict with viewport callbacks.
3. **Agree the prepared-operation/summary and lifetime contract before integrating items 3 and 9.** The backend and inventory owners share the exact target set, policy generation/cancellation state, and remaining request deadline. A prompt must describe the work actually executed. Preserve old links until replacement creation is validated. Neither owner adds an independent second prompt.
4. **Integrate safe link replacement (item 9), then complete descriptive approvals (item 3).** Both touch `llinventorylistener.cpp`, so land sequentially or explicitly divide non-overlapping functions and reconcile against one prepared-operation contract. Approval preflight must be side-effect free; UI text must not derive from caller-supplied item names.
5. **Finish assistant diagnostics/configuration (item 4) after the bridge changes settle.** Coordinate its minimal health request and read-only local status snapshot with the backend owner. A check must work with inventory Read=Never without exposing inventory contents. Do not claim that a bridge check verifies external client configuration.
6. **Complete native monitor support and combined QA (remaining item 6).** The visual editor can land first. Monitor-aware selection lands only where the platform can return correct client-drawable-relative monitor intersections; unsupported systems retain manual editing with honest fallback text.

Each step can be a small PR; the four feature documents define ownership, not a requirement to produce exactly four PRs. Use isolated implementation worktrees/checkouts when running agents concurrently. The integrator owns final conflict resolution and combined viewer validation. This planning task creates neither implementation threads nor PRs.

## Shared contracts and boundaries

- **Permissions:** thumbnail capture requires Camera+Edit; texture capture/upload/thumbnail assignment requires Camera+Create+Edit. All required classes must permit the action; any Never wins; any Ask or positive texture-upload cost prompts. Viewer enforcement remains authoritative, including revalidation before execution.
- **Prepared operation:** resolve authoritative names and bounded candidate IDs in the viewer. Freeze or validate the material target set between review and execution. Reprepare/reconfirm when a material change invalidates the reviewed operation. Keep this an internal contract, not a broad new public mutation-planning API.
- **Asynchronous lifetime:** link work consumes the remaining HTTP/request budget after any prompt. Do not start new destructive follow-up steps after cancellation, shutdown, policy revocation, or deadline. Already-created replacements may remain and must be reported/reconciled without deleting surviving originals.
- **Truthful results:** distinguish replacement link creation from submission of the original's move to Trash; the local inventory update is optimistic. Report per-item outcomes and partial completion without claiming unobserved server acknowledgment.
- **Status:** local preferences state, bridge reachability, inventory readiness, and external-client activity are separate. Keep tokens out of status/configuration/logging. Do not retain caller parameters or inventory content as connection diagnostics.
- **Compatibility:** retain existing settings keys, permission defaults, MCP tool names, environment variables, and default stdio behavior. Add optional metadata/result fields instead of silently changing existing meanings. No permission presets, complete-workspace saving, installer changes, or generic inventory undo journal are included in this batch.

## Review gates

| Gate | Evidence |
| --- | --- |
| Source-backed behavior | Actual touched functions and result/callback semantics checked in each feature plan |
| Backend correctness | Tests that exercise compound permissions, pending-policy changes, exact reviewed targets, async failures, cancellation, and partial outcomes |
| UI usability | Viewer screenshots at supported minimum preferences size and multiple UI scales; keyboard/search checks; supported skins and language fallbacks |
| Installation/setup correctness | Real supported-client invocation with spaces/Unicode in paths, missing/stale launch link, correct installation/settings directory, and diagnostic failures |
| Viewport correctness | Unequal monitors, DPI/Retina, negative desktop coordinates, partial spanning, window moves/resizes, manual fallback, Apply/Cancel/profile behavior |
| Combined regression | Existing fs-mcp/migrator tests and standalone geometry/snapshot-cost tests; focused new checks; packaged viewer smoke tests where feasible |

The preceding review ran the existing Go tests and standalone chrome-layout and snapshot-cost checks successfully. No graphical viewer or native multi-monitor environment was exercised. Planning does not replace implementation-time tests or the GUI/platform evidence above.

## Definition of done for this planning batch

All selected items have concrete scope, implementation stages, code touchpoints, acceptance criteria, failure behavior, and handoff boundaries. The original planning deliverable changed only these documents. The subsequently authorized implementation preserves the contracts above; graphical and native platform acceptance checks remain separate from the automated evidence.

## Workspace context and snapshot follow-up

[Workspace context, monitor recovery and snapshot plan](workspace-context-and-snapshot.md) covers the implemented follow-up to PR #6: optional graphics/camera settings, selected HUDs, display recovery, viewport snapshots and versioned MCP switching. [Validation and Windows acceptance](workspace-context-validation.md) separates actual focused checks from the remaining native checks.
