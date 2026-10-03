# Local assistant and viewport validation

This change implements review items 1–4, 6, and 9 from the [feature plans](plans/README.md). The plans record intended acceptance criteria; this document records what has actually been checked.

## Automated evidence

- All `tools/fs-mcp` Go tests, `go vet`, formatting, and sidecar build pass on Linux. Tests cover token-free diagnostics, compound snapshot policy admission, and preservation of partial link-replacement results. The default MCP initialization handshake also passes.
- Five viewer-independent C++ checks pass: chrome layout, snapshot upload dimensions/cost, compound assistant permissions and terminal approval callbacks, viewport geometry, and asynchronous link replacement. Run them without downloading viewer dependencies:

  ```sh
  cmake -S indra/newview/tests/folderstorm -B build-folderstorm-tests
  cmake --build build-folderstorm-tests --config Release
  ctest --test-dir build-folderstorm-tests --build-config Release --output-on-failure
  ```

  The added workflow runs this suite on Linux, Windows, and macOS. Local execution covers Linux only.
- Linux viewer CMake configuration and the `llui` library build pass with GCC 14. The touched viewer C++ translation units pass syntax checks using the generated viewer compiler flags. An existing overloaded-virtual warning in `LLCameraListener::reset` is downgraded for these syntax checks. A complete viewer executable has not been linked or launched.
- Changed/new XUI files parse as XML. Registration and control names were checked for the dedicated assistant page in default, Starlight CUI, and Vintage skins.
- The actual C++ configuration serializer was exercised with spaces, Unicode, quotes, backslashes, and percent signs. Generated JSON and Codex TOML parse and preserve those paths.

## Runtime acceptance still needed

1. Open Preferences at its minimum size and supported UI scales. Review the Local assistant page, search, tab/keyboard navigation, approval pagination, long labels, and supported skin/language fallback.
2. Exercise packaged Windows/macOS sidecar launching and real Codex, Cursor, and Claude Code configuration. In particular, verify Cursor's Windows command adapter when the installed path contains spaces and the shared launch link is missing or stale.
3. Build with Windows and macOS SDKs. Check viewport alignment on unequal monitors, negative desktop coordinates, mixed DPI/Retina, partial spanning, and window moves/resizes. SDL2/X11 source checks passed; Wayland intentionally offers manual editing when global placement is unavailable.
4. In a logged-in viewer, check Apply/Cancel and profile rollback; revoke approvals with a dialog open; and test link replacement under inventory loading, locks, delayed callbacks, disconnect/relog, and policy changes.

## Deliberate result limits

Link replacement verifies the new link before submitting the original's move to Trash. Results report **Trash move submitted**, because the existing inventory move API supplies no remote acknowledgment. They do not claim server-confirmed deletion or movement.

An uncertain creation keeps its original and reserves the link for inspection for the rest of the login session. A retry reports recovery required rather than creating another replacement. Late callbacks can record a created UUID but cannot trigger a Trash move after cancellation. There is no persistent recovery journal; inspect inventory before retrying after a new login.

Connection diagnostics verify this viewer's authenticated local bridge. They do not verify the assistant client's configuration or imply a persistent external-client connection.
