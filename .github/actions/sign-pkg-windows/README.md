# Windows signing action provenance

Vendored from
[secondlife/viewer-build-util v2.1.0](https://github.com/secondlife/viewer-build-util/tree/a3452dbb9d0c93fbe0b6211d62d7429b0763e97c/sign-pkg-windows),
commit `a3452dbb9d0c93fbe0b6211d62d7429b0763e97c`.
`LICENSE` and `sign.py`, including its original license notice, are copied
unchanged.

The sole action change adds `.app/fs-mcp-launcher-maintenance.exe` to the
existing NSIS executable signing list. Upstream has no input for extra signing
targets. The helper is therefore signed before `makensis` embeds it in the
installer; a missing helper or signing failure fails the signing step rather
than producing an installer with an unsigned maintenance executable.
Velopack's existing `--signTemplate` behavior is unchanged.

The calling workflow checks out this repository before invoking the local
action. When refreshing from upstream, preserve this whitelist addition and
run `indra/newview/tests/test_windows_mcp_installer_contract.py`.
