# Follow-up implementation: items 5, 7, 8 and 10

Implemented on `feat/next-improvements`, based on PR #1's inspected head
`65e6ebd115d7ef182e787a1541efb0305d95aa53`. PR #1 remains open, draft and
unmerged; its branch and original validation record were not changed. This
follow-up is reviewed against `feat/assistant-and-viewport-improvements`.
After PR #1 merges, rebase against the actual merge/squash result before
retargeting to main.

## Behavior delivered

| Item | Implementation |
| --- | --- |
| 5 | Read only, Ask before changes and Custom presets over the existing nine permission classes. Individual controls can be collapsed. Access and permissions preview in the runtime layer; explicit Preferences OK accepts them. Session-only settings never become saved defaults; Cancel and external-control changes preserve the separate saved/runtime layers. |
| 7 | Preferences → Move & View → Workspaces: account-local named arrangements, Save/Preview/Rename/Delete, Inventory sorting and Driving templates, and one deliberately owned extra Inventory window. Versioned allowlisted data covers viewport/chrome, supported Inventory/maps, compatible chat geometry and the primary Received Items panel. Explicit OK commits; Cancel restores live/persisted placement while preserving conversation state and manual extra windows. Restore after login remains deferred as planned. |
| 8 | Inventory gear-menu review/history, exact rename/move previews, additive capability-gated MCP tools, bounded session history, idempotent execution, Stop/Clear, and narrow guarded undo. Submitted and confirmed results are distinct. AIS updates require authoritative readback for confirmation; UDP results remain unconfirmed. Existing permissions, revocation, target validation and one-operation consent stay authoritative. |
| 10 | Copied Windows commands belong to the current installation. Verified Velopack layouts retain the stable logical `current` path; uncertain layouts use the actual executable with recopy guidance. Install/update no longer creates or retargets a shared ProgramData alias. A packaged uninstall helper removes only legacy file symlinks whose target ownership and retained handles are proven. |

## Automated evidence

Checks below ran against the integrated tree in the managed Linux environment
on 2026-10-03. They establish the listed behavior, not native GUI acceptance.

- Eight portable production-helper C++ tests passed: chrome layout, snapshot
  upload, assistant policy, world-view geometry, link replacement, assistant
  permissions, inventory bulk, and workspace geometry/names.
  A ninth check linked the production provenance helper to the viewer's
  Boost fiber/context libraries and passed overlapping coroutine, native
  edit, nested yield and out-of-order scope completion cases. The bulk model
  regression also rejects undo after observed native edits while confirmation
  is pending, even if the final name/parent returns to the expected value.
- All fs-mcp, settings migrator and launcher-helper Go tests and `go vet`
  passed. The sidecar built; the launcher helper and its Windows-native
  fixture binary cross-compiled for Windows amd64. Local builds used
  `-buildvcs=false` because these nested worktrees confuse Go's VCS discovery.
- The settings probe linked against the actual viewer LLControl/LLSD
  libraries and passed saved-file/session/Cancel/external-override checks.
  Workspace serialization tests also linked against the actual viewer LLSD
  libraries and passed strict schema, field/role bounds, profile limits,
  unknown-extension and future-version checks.
- Twenty changed production C++ translation units passed syntax checks
  against the existing viewer dependency cache, including LLFloater,
  Preferences, workspace adapters, bulk UI/backend, bridge, AIS and Inventory
  integration. Existing overloaded-virtual warnings were allowed; the
  pre-existing Windows path comment warning in `llvelopack.cpp` was also
  allowed. Windows-only code paths were not compiled by these Linux checks.
- C++ installation-path fixtures and seven Python installer-contract checks
  passed. Changed XML parsed, Python manifests compiled, new viewer source
  registration and literal UI control references were checked.
  The installer checks execute the vendored NSIS signing loop with a mock
  signer and verify that helper signing failure stops packaging. The local
  composite retains pinned upstream provenance/license and signs the helper
  before building the installer; actual Azure signing was not run locally.
- Touched-file precommit hooks passed except known baseline debt: missing
  license/copyright in `fs_viewer_manifest.py`, existing whitespace in the
  two manifest files and `llpanelmaininventory.cpp`, and mixed line endings
  in the NSIS template. Those existing checks were skipped for the four
  affected legacy files; unrelated formatting was retained. The complete
  follow-up diff passes `git diff --check`.

The portable suite is reproducible without viewer prebuilts:

```sh
cmake -S indra/newview/tests/folderstorm -B build-folderstorm-tests
cmake --build build-folderstorm-tests --parallel 2
ctest --test-dir build-folderstorm-tests --output-on-failure
(cd tools/fs-mcp && go test ./... && go vet ./...)
(cd tools/migrate-settings && go test ./... && go vet ./...)
(cd tools/windows-mcp-launcher && go test ./... && go vet ./...)
python indra/newview/tests/test_windows_mcp_installer_contract.py
```

The portable CI workflow includes the new bulk/workspace helpers. The Windows
ownership workflow runs native file/reparse/race fixtures on Windows; its
successful execution is distinct from local cross-compilation. A symlink
fixture skipped for lack of privileges does not verify cleanup.
The coroutine check is enabled when Boost fiber/context is available; it
does not add a dependency to the eight portable checks.

## Remaining native acceptance

No complete native viewer was linked/launched for this follow-up. The user's
local PR #1 build is separate evidence and does not compile this branch.

- Test minimum Preferences size, keyboard/search, both skins, language
  fallback, UI scale/DPI and actual OK/Cancel/session behavior. Pending
  approvals and bulk work must remain revoked after a terminal denial.
- Follow [manual workspace acceptance](testing/manual-workspaces.md),
  especially hosted/standalone chat, manual dependent windows, Received
  Items, repeated preview/Cancel, account changes, RLVa and tiny windows.
- On disposable inventory, test SL AIS and OpenSim/UDP rename/move under
  Allow/Ask/Never, partial/uncertain completion, relog verification,
  protected folders, conflicting native edits and narrow undo. Readback
  verification and permission checks do not prove every live grid accepts
  the new narrow AIS PATCH body. See [review and recovery limits](local-assistant-bulk-inventory.md).
- On Windows, run real helper fixtures and packaged NSIS/Velopack install,
  update/rollback and uninstall with two installations/channels, nonadmin
  accounts, spaces/Unicode paths, legacy links and real supported clients.
  See [launcher evidence and limits](../tools/windows-mcp-launcher/README.md).

These checks remain required before treating the draft as release-ready.
