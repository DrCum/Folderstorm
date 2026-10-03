# Next improvement plans — items 5, 7, 8, and 10

Planning baseline: [PR #1](https://github.com/DrCum/Folderstorm/pull/1), **open and unmerged**, head `65e6ebd115` on `feat/assistant-and-viewport-improvements`. Main remains at `9aa5cbb0d6` in the inspected checkout. These are follow-up implementation plans, not additions to PR #1's scope and not authorization to start implementation.

The plans were written in the separate `/workspace/Folderstorm-plans-next` worktree on `docs/next-improvement-plans`. The original PR #1 checkout, code, remote branch, and PR content remain unchanged. Its original six-feature plans and validation record remain the baseline for these features.

| Track | Review item | Concrete outcome | Dependency |
| --- | --- | --- | --- |
| [Permission presets and session settings](assistant-permission-presets.md) | 5 | Read only / Ask before changes / Custom, collapsed details, access and permissions for this session without saving them accidentally | PR #1's dedicated panel, compound policy, terminal revocation, and settings rollback |
| [Complete workspaces](complete-workspaces.md) | 7 | Account-scoped viewport/chrome plus allowlisted inventory/chat/map window arrangement, staged extra inventory-window support | PR #1's viewport/profile preview and Cancel behavior; existing account-scoped floater persistence |
| [Bulk review, history, and limited recovery](bulk-inventory-review-and-history.md) | 8 | Authoritative rename/move previews, truthful bounded session history, narrow undo only when completion/current state can be verified | PR #1's prepared operations, richer approval window, policy checks, and cancellation rules |
| [Windows launcher ownership](windows-mcp-launcher-ownership.md) | 10 | Installation-bound durable command, no new shared-alias retargeting, proven-owner legacy cleanup, nonadmin fallback | Installer cleanup helper can proceed independently; final Setup changes reuse PR #1's serializer/diagnostics |

## Recommended delivery order

1. **Finish reviewing PR #1's local build and graphical behavior.** Carry its fixes forward before accepting dependent implementation. Do not silently merge it, expand it with these features, or recreate its extracted assistant UI on main.
2. **Item 5 first:** small visible improvement plus a real persistence transaction. Implement the pure preset model before the UI, and make session-only serialization/Cancel correct before displaying that option.
3. **Item 10 next:** stop new installer shared-alias creation and make legacy cleanup ownership-aware. The isolated cleanup helper/tests can run in parallel with item 5; sequence their Setup-panel edits under one file owner. PR #1 already supplies copy-time fallback, but its saved shared alias could still be changed by a later installer.
4. **Item 8 in stages:** preview/readable results and session history first; enable the narrow undo subset only after authoritative completion verification is implemented and tested. Keep “submitted” and “confirmed” distinct. Older viewers keep existing tools and receive no unsupported preview/undo promise.
5. **Item 7 in stages:** account-scoped workspace schema and safe primary-window restore first; add deliberately owned extra inventory-window roles and wider useful arrangements after the lifecycle adapters are proven. Existing chrome profiles remain available and unchanged in meaning. Do not serialize every floater or live chat session.

Steps 4 and 5 are independent and may run in parallel once PR #1 is stable. This sequence is an integration order, not an estimated schedule.

## Branch and handoff rules while PR #1 is unmerged

- Default: build prototypes/pure helpers in isolated branches and start dependent feature integration from main after PR #1 merges.
- If early end-to-end testing is needed, create one feature branch per track based on PR #1's **actual latest head**, not on the old review main. Record that dependency and target an early stacked PR at PR #1's feature branch so only the follow-up diff is reviewed. Creating such implementation PRs is a later task, not part of this planning batch.
- Keep PR #1's branch untouched. The current planning worktree is disposable planning context; it is not a reason to append new source commits to PR #1.
- When PR #1 changes during review, integrate its latest version into dependent worktrees before combined testing. After it merges, rebase onto the actual main merge/squash result and retarget dependent PRs to main. Compare the resulting diff and rerun integration checks; do not simply change the base and leave duplicate PR #1 commits in the review.
- If PR #1 is closed or significantly redesigned, stop dependent integration and update these plans against the accepted replacement. Independent launcher-helper tests can remain separate. No automatic fallback to a weaker permission implementation.
- Give each implementation branch its own worktree. Agents must not share a source-edit checkout. One integrator owns shared-file edits and schema/API contracts.

## Shared contracts

**Settings scopes.** Item 5 uses global saved/runtime assistant layers; item 7 uses account-scoped workspace data with an embedded chrome snapshot. A workspace must not save, promote, or restore assistant permissions/session scope. Item 5 must not disturb viewport/profile transaction baselines.

**Approval and preview.** Item 8 uses viewer-owned IDs and authoritative labels. Public preview/history output is gated by Read; execution uses the operation's existing mutation classes. A plan, preview acceptance, or undo button is never a substitute for current permissions. PR #1's denial, price limits, and terminal cancellation stay authoritative. Combining review with an Ask prompt should avoid duplicate dialogs for one operation.

**Activity lifetime.** History is a bounded, login-session UI feature, not persisted request logging or a general transaction journal. Diagnostics, bearer tokens, scripts/notecard text, arbitrary request payloads, and assistant settings are excluded. Logging out clears/inactivates history and recovery work; a delayed callback cannot write into a later account's session.

**Installer identity.** Item 10 never uses a product-wide “last installed” path as newly copied configuration. Each installation owns its launch context; ambiguity leaves legacy paths untouched. No installation-aware cleanup depends on permission presets or inventory state.

**Shared-file ownership.** Item 5 owns `fspanelpreferencelocalassistant.cpp` while integrating its editor; item 10 supplies a small Windows path-resolution delta to that owner or integrates afterwards. Items 5 and 7 coordinate scoped additions to `llfloaterpreference.cpp`. Item 8 owns prepared batch/approval/history changes in `fseventapibridge.cpp`, `llinventorylistener.cpp`, and `fsassistantoperation.cpp`; do not have a second agent concurrently refactor their policy definitions. The integrator owns source registration/build wiring and combined documentation.

## Review gates

| Gate | Required evidence before calling a feature complete |
| --- | --- |
| Presets/session | Exact nine-class maps, unchanged defaults, no writes on opening, saved-file round trips, equal-value and command-line overrides, Cancel/external-change conflicts, live revoked approval/active-operation tests |
| Workspaces | Schema/version behavior, account isolation, existing profile compatibility, allowlisted floater adapters, readable-window recovery after DPI/size/topology changes, hosted-chat and deferred-login lifecycle tests |
| Bulk review/history | Preview/execution parser parity, frozen plan validation, partial and uncertain results, current-policy enforcement, bounded/redacted history, stale/conflicting undo rejection, confirmed-completion evidence |
| Launcher | Real Windows reparse/handle/race tests, both installers, two installations/channels, nonadmin mode, update/rollback, stable Velopack-root verification, real supported-client invocation |
| Combined UI | Minimum preferences size, multiple UI scales, keyboard/search, supported skins/language fallback, scoped OK/Cancel and actual viewer smoke tests |

PR #1's portable C++/Go suites and touched-file hooks are reusable checks, not evidence that these unimplemented features pass. Its native viewer build/graphical acceptance limits remain in `doc/assistant-viewport-validation.md`. Fixing unrelated fork-wide CI style debt, CLA credentials, or GitHub billing is not silently included in this feature batch.

Done for this planning batch means four source-backed plans with explicit scope, stages, ownership, failure behavior, tests, and a safe dependency strategy. Only new planning documents are written; no feature code, runtime settings, installer files, or external applications are changed.
