# Windows legacy MCP launcher maintenance

`fs-mcp-launcher-maintenance.exe` is packaged beside the Windows viewer and
sidecar. Install and update never run it. Uninstall calls it before removing
the packaged files:

```bat
fs-mcp-launcher-maintenance cleanup-owned-legacy --install-dir "C:\Program Files\Folderstorm-Release\."
```

The only deletion candidates are `Folderstorm\fs-mcp.exe` and
`Folderstorm\fs-mcp.exe.replacing` beneath the shell's ProgramData known folder.
There is no caller-supplied deletion path. Candidates are assessed separately.
The helper leaves regular files, directories, redirected legacy parents,
foreign targets, dangling links, and busy or unverified objects unchanged.
It never creates or retargets a link, repairs ACLs, deletes a target, or removes
the parent directory.

Ownership requires both the accepted installation target path and matching
directory/file IDs. Directory ancestors, including redirector destinations,
are pinned with no-follow handles that deny competing write/delete access.
Candidate deletion uses the inspected symlink's retained handle. Exact path
spelling is intentionally conservative: different case or an unrecognized
historical layout can leave an owned alias for manual removal.

The JSON report uses fixed result categories: `removed_owned_link`, `absent`,
`not_owned`, `unverified`, `busy`, and `access_denied`. Refusing cleanup is a
successful conservative result; invalid invocation returns nonzero. Neither
tokens nor discovery files are read.

## Validation

```sh
go test -v ./...
go vet ./...
GOOS=windows GOARCH=amd64 CGO_ENABLED=0 go build .
GOOS=windows GOARCH=amd64 CGO_ENABLED=0 go test -c .
```

Linux runs parser and lifecycle tests. The Windows workflow additionally runs
real file/junction/symlink fixtures and deterministic barriers that attempt
replacement, rename, retargeting, and target mutation before disposition.
Symlink tests explicitly skip if the runner lacks symlink privilege; that
skip does not verify native cleanup. Cross-compilation does not execute these
fixtures. Packaged NSIS/Velopack installation, update/rollback, uninstall and
real Cursor/Codex/Claude launch tests still require Windows acceptance testing.

## Stable command evidence

The viewer recognizes Velopack's `root/current/sq.version`, `root/Update.exe`,
and `root/current` binary layout, verifies package ID/version, and retains the
logical command rather than canonicalizing a version directory. The pinned
autobuild SDK is `0.0.1535-r2`; upstream
[locator.rs at 0.0.1535-gb21da2a](https://github.com/velopack/velopack/blob/b21da2a788300884559cb77197c4d0a976b4f4e1/src/lib-rust/src/locator.rs)
defines that layout in `create_config_from_root_dir`. Unknown layouts retain
the actual executable path with recopy guidance. A changed package version
blocks configuration copy until the viewer restarts.
