# Coordinated Windows Velopack updates

Checkpoint 3 is stacked above Sentry PR #69 and release checks PR #68.
NSIS remains supported; `USE_VELOPACK` remains Off by default. No installer,
release, hosting deployment, live download or installed update was produced here.

## Packaging and publishing

Build matching viewer, session host, MCP and `folderstorm-update.exe` with
`USE_VELOPACK=ON`. CMake generates `folderstorm-bundle.json` with exact version,
source commit, protocol and variant app ID. Packaging stages it plus every tool;
missing update helpers fail staging. App IDs separate flavor, channel, CPU and
architecture, avoiding incompatible updates. Normal NSIS packages do not gain an
in-place updater by guessing their install layout.

The existing manifest's Velopack path now uses this ID. Alternatively validate a
staged app with `python scripts/folderstorm_velopack.py STAGED --output OUTPUT`.
It prints the exact command only. `--pack` explicitly runs matching vpk 0.0.1535;
`--previous PREVIOUS_OUTPUT` retains prior full packages/feed so vpk can generate
full/delta output. Source and output remain separate. CLI execution, actual delta
generation, signing and installer staging require the native build environment.

Extend a Windows Velopack installer entry in the release publisher input with
`"velopack_feed": "Releases/releases.win.json"`. Its native feed must name one
matching Full package. The helper hashes the actual package, validates native
SHA1/SHA256/size and its embedded bundle identity/component set, and adds exact
package metadata to Folderstorm's bounded release feed. It uploads that full
package alongside the installer only when explicitly creating a draft. Packages
>=2 GiB need the configured download hosting. Publish complete artifacts before
publishing the release/feed. Native Velopack feeds/deltas may also be retained on
hosting per variant; the first coordinator deliberately downloads the full
package to make source identity and verification predictable.

Use GitHub Releases or configure the first checkpoint's HTTPS feed and trusted
hosts for self-hosted assets. No Velopack Flow account or runtime GitHub token is
required. Source/variant metadata and native signing are maintainer responsibilities.

## User flow and installation ownership

Viewer Help → Check for updates exposes **Update app** for a matching package.
Host Updates offers **Prepare update**, and its release dialog offers **Update
whole application**. NSIS/unrecognized/portable layouts get an honest installer
migration message. A channel/variant that changes the package ID requires its
installer; no silent cross-channel replacement occurs.

A separate update window checks the feed, displays version/notes and asks before
downloading. It streams with a 30-minute deadline, bounded exact size and SHA256,
then independently re-hashes the cached package, including SDK cache hits.
SDK downgrades/deltas are disabled for this initial coordinator. Download and
install remain explicit; inherited automatic install-on-worker-exit/startup is
not used on Windows. There is no automatic background download or install option
in this checkpoint. Cancelling leaves the verified package unapplied.

To install, close using the host's **Quit all characters** action, confirm native
logout and close the MCP client/sidecar. Normal lifecycle shutdown cancels pending
launches/restarts. Cancelling a native logout, Keep logged in, detached viewers,
standalone viewers and an open MCP sidecar all block installation. The coordinator
never kills an avatar process or retries an uncertain command. Retry checks all
processes again; Cancel exits without installing.

Each installed viewer, host and MCP sidecar holds a shared lifetime lease on a
stable root lock file. Installation requires its exclusive lease plus an exact
image-path process scan (including Crashpad/helpers), unchanged root/bundle and
an explicit final confirmation. A persistent transaction barrier is written
before handing off to Velopack, which waits for this helper to exit. Fresh app
launches are blocked across that handoff, even after the helper's lock closes.
Native update hooks execute before the viewer's lifetime lease, as required by
Velopack; normal application startup remains behind the barrier.

After replacement, only the exact expected target version/commit may clear the
barrier, after Update.exe exits. Old binaries refuse to start. A failed update
leaves the barrier intact instead of replaying installation; directly run the
installed `folderstorm-update.exe` to recover. Recovery requires no other app or
updater processes and explicit confirmation before clearing the barrier. The
helper also works after the host closes. Persistent profiles/host preferences
stay outside the application and are not copied over during replacement.

The root lease/pending files are process coordination, not a boundary against a
same-user program deliberately deleting files or an external installer ignoring
the protocol. Do not run competing installers manually during an update. OS
image queries can conservatively refuse when a relevant process is inaccessible.

## Focused validation and native acceptance

Three affected Release CTests pass: release selection/response bounds, consent
policy and update barrier/coordinator policy (kept-alive process, failed lease,
stale identity/version, cancellation and one apply). Ten affected Python tests
pass for publisher hashes/metadata, tampered/wrong-version Velopack packages,
complete/missing bundle components, NSIS refusal and symbol validation. Common
non-Windows update runtime compiles under strict C++17 warnings. Changed XML and
manifest Python syntax/diff checks pass. Windows MCP lease/command cross-compilation
is checked separately; native C++ compilation is not available here.

Still required on Windows: compile/link matching optimized binaries and SDK;
NSIS installer-link migration; initial Velopack install then a real newer signed
package; full package/feed output and optional delta generation; staged helper/
marker/licenses; five logged-in accounts and a cancelled logout; keep-logged-in
and detached/standalone blockers; a live MCP client; other installation isolation;
concurrent new launches at handoff; background Crashpad exit; download cancellation,
cache tampering, offline/404, changed channel/bundle, Update.exe failure, recovery,
exact target restart and persistent settings. No broad/native/packaging builds or
GitHub workflow builds were run as part of this work.

Correction pass: default NSIS startup now includes the common guard independently
of Velopack. SDK progress uses its actual int16_t ABI. Package validation requires
unique exact `lib/app` paths, valid schema/protocol, the bounded viewer guide and
its real named executable plus all helper components. Tests reject missing viewer
and missing host even when metadata hashes are recomputed correctly.

Stable root barriers now take precedence over temporarily missing current bundle
markers. A shared, standard-library-only Go updateguard is linked by MCP, migration
and launcher maintenance, closing the helper launch race as well. Its regression
exercises missing metadata with either stable lock or pending marker. Root image
paths are canonicalized for installation comparisons. Channel changes invalidate
a prepared install. The updateguard test passes, and affected MCP, migrator,
maintenance and guard Windows test executables cross-compile successfully; this
is not a native C++/SDK runtime build. All affected Go binaries rebuild when their
shared guard changes through explicit CMake dependencies.

Independent Sol 6.1 Extra High review and two correction reviews completed through
`07256febb1`; no actionable finding remained in the final source review. The final
pass closes uppercase/aliased launch paths during missing metadata and requires
the launcher-maintenance helper in both package validators. Native acceptance is
still outstanding; these source/portable checks do not establish Windows behavior.

## Refused-command exit status

Migration, MCP and launcher maintenance now exit with status 1 when the shared
update guard refuses startup. No copy/diagnostic/cleanup work runs and no success
output is written. Three focused subprocess tests inject refusal into each actual
command main and assert its OS exit code, empty stdout and refusal diagnostic;
all pass. This covers the installer-visible contract rather than a guard model
alone. Native Windows update and Sentry acceptance remain required before merge.
