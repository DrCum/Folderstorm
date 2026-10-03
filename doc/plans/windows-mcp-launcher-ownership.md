# Windows MCP launcher ownership and installation fallback

Status: **planning only**. Covers review item **10**. Source reviewed against the current implementation represented by PR1 commit `65e6ebd115`; **PR1 is unmerged**. This plan changes neither PR1 nor installer/source files.

## Result and chosen design

Each installation supplies its own durable MCP command. Installing, updating, or uninstalling one channel must not retarget or remove another installation's command. A nonadmin Velopack installation must remain fully configurable without a writable ProgramData directory, Developer Mode, or symlink privilege.

**Use the installation's actual `fs-mcp.exe` as the durable command and retire creation/retargeting of the shared ProgramData alias.** NSIS uses its stable installation directory. Velopack uses the framework's stable installation root plus `current\fs-mcp.exe`, after verifying it belongs to the installation running the viewer. Do not introduce another global alias, per-user “last installation wins” alias, launcher service, or automatic writes to external client configuration.

A single shared `C:\ProgramData\Folderstorm\fs-mcp.exe` cannot express which installation a saved client configuration belongs to. Even PR1's correct copy-time equivalence check cannot stop another installer from changing that alias after configuration is copied. The direct command removes that ongoing dependency. There is no need to create an additional launcher executable solely to avoid spaces: PR1 already supplies the client-specific Cursor adapter.

Keep existing legacy aliases untouched during install/update so existing configurations continue working until their owning installation is removed or the user copies a direct configuration. On uninstall, perform narrowly proven, installation-aware cleanup; leave anything whose ownership cannot be proved. In particular, do not retarget an existing alias to the “remaining” installation.

## What PR1 already provides; what is new

| Area | Already in unmerged PR1 | New item 10 work |
| --- | --- | --- |
| Configuration copy | Resolves the installed sidecar and actual viewer discovery directory. Uses the shared alias only when `std::filesystem::equivalent` matches this installation. | Stop selecting the shared alias for newly copied entries; resolve/verify Velopack's durable `current` path rather than retaining a version-specific executable path. |
| Client serialization | Codex TOML and Cursor/Claude JSON. Cursor falls back to `cmd.exe /d /s /c` with a quoted `%FOLDERSTORM_MCP_BINARY%` expression and the actual path in its environment. | Preserve serializer behavior; prove the real supported Windows clients launch paths with spaces and unusual characters. No new client adapter unless those tests expose a concrete issue. |
| Diagnostics | Runs the actual installed sidecar with an argument-vector process API; bounded, authenticated health check and token-free output. | Keep checks against the actual running installation. Clearly explain a stable-path mismatch during a transient Velopack update; never silently switch to another installation. |
| Install/update | Existing NSIS and Velopack hooks still replace the shared path. | Remove shared alias creation/retargeting and their shared `.replacing` backup handling. |
| Uninstall | Existing code removes the shared alias and `.replacing` without an ownership check. | Delete only a recognized file symlink proven to target this uninstalling installation, using handle-based inspection/deletion. |
| Nonadmin install | PR1 provides a usable direct-path/adapter configuration even if shared-link creation fails. | Eliminate the failing ProgramData creation attempt entirely; ordinary install/update must never request elevation for MCP. |

The original four-feature plans excluded installer changes. This is the follow-on delta, not a claim that PR1 already fixes ownership.

## Source-backed integration points

- `indra/newview/installers/windows/installer_template.nsi`: `CreateFsMcpLaunchLink`, the `FsMcpRemoveLaunchLink` macro / `un.RemoveFsMcpLaunchLink`, the installation call to create the alias, and the uninstall call before `un.ProgramFiles` removes packaged files. Current creation moves any existing path to the common `.replacing` name; cleanup also deletes directory-shaped entries and that backup without proof of ownership.
- `indra/newview/llvelopack.cpp`: `create_fs_mcp_launch_link()`, `remove_fs_mcp_launch_link()`, `on_after_install()`, `on_after_update()`, and `on_before_uninstall()`. Current removal takes no installation argument. Current `get_install_dir()` uses a fixed `MAX_PATH` `GetModuleFileNameW` buffer and is not sufficient evidence for durable update-root resolution.
- PR1 `fspanelpreferencelocalassistant.cpp::sidecarPath()/refreshConfiguration()` and `fsassistantconfiguration.h::generate()`: resolve/copy an installation-bound Windows command, retain existing serializer and env semantics.
- `indra/newview/viewer_manifest.py`, the Go-tool build/staging definitions, Windows package/uninstall file lists, and `doc/help.md` / `tools/fs-mcp/README.md`: package the small cleanup utility described below, remove the claim that the ProgramData alias is the universal command, and document migration through Setup.

Do not broaden the work to protocol registration, shortcuts, updater UI, settings migration, or executable signing policy unrelated to the added packaged helper.

## Durable installation identity and path resolution

Separate a **stable logical launch path** from **current file identity**. File IDs and resolved version directories change on update; persist neither as the durable client command.

| Installer | Stable identity | Durable command |
| --- | --- | --- |
| NSIS | Installation scope + product/channel/application ID + canonical registered installation root. Preserve the same root during an in-place update. | `<installation-root>\fs-mcp.exe` |
| Velopack | User SID/scope + framework application/channel ID + stable framework installation root. | `<installation-root>\current\fs-mcp.exe` |

Different channels, users, installation roots, and installer families remain distinct. Display names and a folder-name prefix are not ownership evidence. Reinstalling a channel to a different root intentionally requires a newly copied configuration; do not hijack a saved command belonging to the old root.

For NSIS, use the already-selected `$INSTDIR` / registered installation root. For Velopack, use the framework's authoritative root/context when available. First verify the actual installed SDK/package layout: do not guess that removing a final `app-*` path component is valid. If the SDK exposes only the hook executable location, resolve it against the framework's installation record and a validated stable `current` entry. Confirm that `current\fs-mcp.exe` and the running installation's packaged sidecar identify the same file while installation state is settled.

When launched from an old version during an update, `current` may already point to the new version. Show a short “Update in progress; reopen Setup after the viewer restarts” result for configuration copy rather than binding an old process to the other version. Connection diagnostics may still use the packaged sidecar beside the running process; do not reinterpret a successful diagnostic as verification of the framework's stable launch path. If durable-root verification is unavailable, retain the usable actual-executable fallback, label it as needing recopy after update, and record that limitation instead of inventing a stable path.

Use dynamically sized Unicode Win32 path APIs. Respect `\\?\`/UNC forms supported by the installation; do not truncate at `MAX_PATH`. Use handles and file IDs to compare existing files. For normalized lexical checks, compare Windows path components with an ordinal comparison that respects the actual filesystem's case-sensitivity rules; avoid locale-sensitive lowercasing or string-prefix containment. Distinguish DOS drive paths, extended paths, volume GUID paths, trailing separators, relative symlink targets, and similarly named sibling directories. Resolve relative symlink targets against the link's actual parent. Hard-link equality alone is not installation ownership: also require the accepted target path/installation context.

No product-wide mutable owner registry is needed because no new shared launcher is created. Existing installer/framework records establish context. If cleanup needs a record across a hook boundary, store it in the installation's own protected metadata, scoped to that installation and generated from trusted installer context; do not accept an arbitrary shared JSON “owner” file as proof.

## Legacy alias ownership and cleanup contract

Implement one Windows-specific cleanup routine used by both installers. Prefer a small packaged **`fs-mcp-launcher-maintenance.exe`** with a narrow `cleanup-owned-legacy` operation, built from a new `tools/windows-mcp-launcher` Go module using the existing Go toolchain. This avoids two independent implementations of reparse-point and race handling in NSIS and the viewer. It is an installer utility, not a new MCP command or MCP tool. Its inputs identify the invoking installation, not an arbitrary deletion destination; the legacy candidates are fixed by this program. The utility never accepts a bearer token, reads discovery files, or changes client configurations.

Invoke it during uninstall **before the installation's packaged target and helper are removed**. Both NSIS and Velopack pass their trusted installer/framework context. Do not execute an old/shared ProgramData executable to perform cleanup. If the helper cannot run or prove ownership, continue uninstall and leave the alias; MCP cleanup must not remove unrelated content or fail the viewer uninstall.

Inspection/deletion rules:

1. Obtain ProgramData from `SHGetKnownFolderPath(FOLDERID_ProgramData)`, not a caller's environment or an arbitrary registry fallback. Limit candidates to the old `Folderstorm\fs-mcp.exe` and `fs-mcp.exe.replacing` names. Treat the backup independently; never delete it simply because the main alias is owned.
2. Open and validate the `Folderstorm` parent directory as a directory without following its own reparse point. If it is a junction/symlink or its access/identity cannot be established, skip cleanup. Keep a directory handle preventing its rename/deletion while inspecting children; do not change directory ACLs or attributes to make cleanup succeed.
3. Open each candidate with `FILE_FLAG_OPEN_REPARSE_POINT`, sufficient attribute/reparse-read/delete access, and sharing that excludes competing write/delete access. Require a **file symbolic link** with the expected `IO_REPARSE_TAG_SYMLINK`; reject regular files, hard links, directory symlinks, junctions, mount points, and unknown reparse tags. Use `FSCTL_GET_REPARSE_POINT` and validate the buffer/ranges before reading its target.
4. Establish that the stored target is an accepted launch path for this installation and that its followed file identity matches this installation's packaged sidecar. Also validate any intentionally accepted Velopack `current` junction/version path against this installation's framework root/context. Do not infer ownership merely because the target exists somewhere under a similarly named Folderstorm directory.
5. If the link is dangling, target identity cannot be read, a historical version is no longer provably present, or an expected ancestor unexpectedly resolves elsewhere, **leave it**. Uninstall happens before files disappear specifically so live owned links can usually be proven. Do not weaken ownership checks to tidy stale files.
6. Retain the inspected no-follow link handle until deletion. Delete through that same handle using the appropriate file-disposition API; do not close it and then call `DeleteFileW(path)`. Denying competing write/delete sharing prevents in-place retargeting, rename/replacement, and check-then-delete races. If existing handle sharing or privileges prevent this, return a fixed “left unchanged” category.
7. Never call `SetFileAttributesW` on an unproven candidate, recurse into a directory, or delete its target. Remove the parent only if it is empty **and** there is positive evidence this installation created/owns that directory. Legacy versions recorded no such evidence, so leave the legacy parent by default. An empty global directory is preferable to removing an unowned one.

The helper returns fixed categories such as `removed_owned_link`, `absent`, `not_owned`, `unverified`, `busy`, and `access_denied`; report paths only where helpful, never sensitive configuration contents. Repeated cleanup is idempotent. This is deliberately conservative maintenance, not a promise to protect against an administrator who can alter the whole installation.

## Transition and installer lifecycle

**New install:** package `fs-mcp.exe` normally; create no global alias, backup, lock file, or ProgramData directory. Setup copies the durable direct command. A per-user install needs no MCP-specific elevation or symlink privilege.

**Upgrade:** stop invoking shared alias creation. Leave the legacy alias unchanged, even when it points to this installation. It remains a compatibility path, not the recommended new command. Retain the framework's stable `current` behavior; the installer must not delete or repair that framework-owned junction. The ownership helper should not run during update because removing a live legacy command would unnecessarily break existing configurations.

**Uninstall:** inspect/remove only proven aliases to this installation before deleting package files. Uninstalling B cannot delete A's alias, or vice versa. Do not restore a `.replacing` path, choose another installed channel, or perform a global cleanup scan.

**Failed installation/update and rollback:** no shared-path mutation means there is no launcher state to restore across package rollback. If the framework rolls `current` back, the durable command follows that framework operation. Do not independently rewrite `current`. If uninstall cleanup succeeds and the uninstall subsequently fails, do not fabricate a replacement legacy link: the installation's direct command remains available if its binary survives. Provide the normal Setup recopy route after repair.

**Old installer/uninstaller coexistence:** already-shipped old versions can still retarget/delete the shared alias using their old unguarded code; this plan cannot rewrite those binaries. New copied configurations no longer depend on that alias. Avoid claiming global alias stability during a mixed-version transition. Offer a concise documentation note to recopy each client entry from the intended viewer's Setup page. Do not automatically rewrite existing client files.

## Concurrency and rollback validation matrix

Run the helper's Windows filesystem tests plus real installer/client tests. Pure serializer tests do not establish Win32 deletion or client launch behavior.

| Case | Required result |
| --- | --- |
| Install A, install B in either order; NSIS + Velopack + private/OpenSim channels | No shared alias is created/retargeted by new installers. Each copied command starts its own sidecar. |
| Existing alias to A; install/update/uninstall B | Alias/backup to A remains byte-for-byte unchanged. B's direct command works. |
| Alias and backup target different installations | Evaluate independently; delete only the proven candidate owned by the uninstalling installation. |
| Regular file, directory symlink, junction, unknown reparse tag, or reparse `Folderstorm` parent | No attribute changes/deletion/traversal. Fixed “left unchanged” result. |
| Dangling alias, denied target access, absent expected binary, malformed/relative target | No destructive fallback. Valid relative owned target can be proven; otherwise leave it. |
| Case variants, Unicode, spaces, `%`, `&`, parentheses, UNC/extended paths, case-sensitive directories, and similarly named sibling roots | Correct installation identity; no prefix/locale/truncation mistake. Actual client invocation succeeds for supported install paths. |
| Velopack install/update/rollback across version directories | Newly copied command stays on the verified stable `current` path; old-version running viewer does not silently copy the new version's path. Framework rollback retains a usable command. |
| Nonadmin install, Developer Mode off, unavailable/read-only ProgramData | Install/update succeeds with direct configuration; no MCP-specific privilege prompt. |
| Two new installers/update/uninstall hooks at once | No shared mutation during install/update. Cleanup either acquires the inspected handle and deletes that object or leaves it busy; it cannot delete a replacement belonging to another installation. |
| Competing legacy updater tries retarget/rename between inspection and deletion | Race rejected by sharing/held-handle checks or target left busy; never delete a newly substituted object. Repeat with target/framework `current` change mid-check. |
| Crash before/after candidate open, verification, disposition, and package removal | Idempotent recovery; no common temporary backup to steal or restore; unrelated aliases untouched. |
| Cursor, Codex, Claude Code on real Windows with missing/wrong legacy alias | Each runs the installed sidecar via the generated direct entry; diagnostics still target the intended viewer; no external client files changed by the viewer. |
| A legacy installer later retargets/deletes ProgramData | Newly copied direct configurations keep working. Compatibility-only old configurations may require recopy; document this practical limit. |

Add a test harness capable of placing synchronization barriers after reparse inspection and before disposition, rather than relying on probabilistic races. Simulate sharing failures, rollback, and framework path switches. Confirm the target executable still exists after link removal. Preserve existing no-argument `fs-mcp` stdio initialization tests and PR1 serializer/diagnostic coverage.

## Implementation order, handoffs, and dependencies

1. **Independent helper design/tests:** implement the narrow no-follow cleanup library and Windows fixture harness, stable installation identity rules, and handle-based race tests. This can proceed independently of PR1 and the permission-preset work; do not merge installer callers before the helper contract is tested.
2. **Installer owner:** remove creation/retargeting hooks, package the helper, and invoke owned cleanup before NSIS/Velopack delete their files. Verify actual Velopack SDK root/current semantics and uninstall context, including long Unicode paths. Keep NSIS and Velopack integration under one owner to avoid divergent rules.
3. **Setup owner after PR1 lands:** amend PR1's Windows path selection to prefer verified direct/stable installation commands; retain serializer/diagnostic contracts. Resolve conflicts in `fspanelpreferencelocalassistant.cpp` sequentially with the item 5 UI owner. Do not cherry-pick a second version of PR1's complete panel.
4. **Windows QA/documentation:** exercise the matrix with packaged installers and real clients, update setup examples and transition guidance, and record known unsupported install paths/platform evidence.

Target the follow-on implementation branch from main **after PR1 merges**, or explicitly stack it on PR1 and document that dependency. Planning and an isolated helper prototype/tests may proceed now; this plan authorizes no changes to the unmerged PR1 branch. There is no dependency on new permission presets/backend enforcement.

Done means new installers never replace an unverified shared launcher; uninstall removes only a proven owned file symlink through its inspected handle; per-user/nonadmin configurations use a verified working fallback; Velopack commands survive ordinary updates; and concurrency/native-client evidence is recorded. If stable-root verification or a Windows client launch convention remains unverified, state the limitation and keep the actual executable fallback rather than claiming durability or platform support.
