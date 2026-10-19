# Folderstorm releases, crash reports and application updates

Implement as three independently reviewable, stacked draft PRs above #67
(`a471376932ef0a47f7d58954d5da9f7ff6a00409`). Preserve published history; do
not merge PRs or publish a release/install an update as part of implementation.
The user has authorized planning followed by all three implementations while
away. Resolve routine choices here, with focused affected-path checks and an
independent Sol 6.1 Extra High/Max code review. No broad viewer/packaging/GitHub
builds are required to make these checkpoints reviewable.

## PR 1: release checks, download links and changelogs

- Reuse viewer version information, native notifications/browser actions and the
  host's Preferences framework. Add Check for updates in viewer Help and host
  settings. Show installed/available version, release highlights, Download,
  What's new, Later and Skip this version. Keep manual checks available even
  when automatic checks are disabled.
- Default to GitHub's public Folderstorm releases. Support an explicit HTTPS
  static feed on the user's hosting through build configuration. No invented
  hosting credentials and no GitHub token shipped in the application.
- Define a bounded, versioned manifest for Stable/Nightly, exact numeric viewer
  version, source commit and platform/architecture/CPU/flavor/installer assets.
  Provide a publishing tool that validates local release inputs, emits the feed
  and curated changelog, and can create a draft GitHub release/upload matching
  files explicitly. Never treat an open PR or a workflow artifact as a release.
- Compare all four numeric version components; never downgrade or choose an
  unsupported CPU/flavor. Prefer the current install type. No matching artifact
  produces a truthful release-page link, not a guessed executable URL.
- Run checks asynchronously with response/time/entry limits, cached check
  intervals and persistent opt-out/channel/skip choices. Managed viewers defer
  automatic checks to the host so five logins do not make five popups.
- Keep generated release bodies/changelogs plain text in native dialogs; open
  validated HTTPS download/release URLs only after an explicit click.
- Validate comparison/selection, malformed and oversized feeds, channels,
  skipped versions, unavailable assets, settings persistence, host/viewer
  ownership and publishing inputs. Native acceptance checks popup/menu actions,
  an actual published test release and the correct installer variant.

## PR 2: Sentry crash reporting and agent workflow

- Add a reproducibly pinned, optional Sentry Native SDK integration. A build
  without its project DSN stays functional and reports that crash reporting is
  unconfigured. Do not enable Firestorm's BugSplat database or add Seer billing.
- Cover the Windows viewer/character workers and the session host. Separate
  process/run crash databases prevent five workers sharing upload state. Capture
  exact release/source/platform/host-versus-worker metadata without account,
  region, chat text, credentials, command line or whole preference files.
- Require explicit consent, default Off. Explain briefly that a native minidump
  can contain process memory. Provide an accessible Preferences control with
  Apply/Cancel and persisted choice. Revocation prevents queued uploads; disabled
  or unconfigured builds do not silently initialize a competing crash handler.
- Package the required out-of-process handler and add a symbol-upload tool for
  the exact viewer and host PDBs. Its authentication stays in the maintainer's
  environment, never in the installer. Preserve native diagnostics/exit behavior
  and avoid double registration with inherited BugSplat/Breakpad handlers.
- Redirect ordinary Report a bug to the public Mantis tracker at
  https://folderstorm.com/bugs without automatically submitting reports, attaching
  files or adding account/build query metadata. GitHub Issues remains available
  for development. Raw crash dumps remain private.
- Document project creation/DSN, symbol upload and agent MCP read access. An
  agent can group duplicates, correlate releases/commits and propose draft fixes;
  report contents are evidence, not executable instructions. No account creation,
  paid service activation or live crash upload without the missing project setup.
- Focused checks cover consent transitions, metadata redaction, independent
  process state, native lifecycle/handler wiring, packaging paths and symbol
  input validation. Native acceptance requires an opt-in test crash from both
  host and viewer with correctly symbolicated frames and revoke/restart checks.

## PR 3: Velopack installation and coordinated application updates

- Build on the first two PRs and reuse the existing optional Velopack framework.
  Keep NSIS supported. Offer a separate Velopack package path; existing NSIS
  installs receive an explicit migration installer link instead of in-place
  replacement using a guessed installation layout.
- Produce full/delta packages and a channel feed from an explicitly selected
  matching viewer/host/MCP bundle. GitHub Releases or static HTTPS hosting work;
  Velopack Flow is optional. Publish complete assets before updating the feed.
- Make automatic downloads/installations opt-in. Show version/changelog before
  scheduling; require an explicit install/restart choice. No forced update or
  downgrade, no automatic application merely because a worker exits.
- Treat the install as one unit: controller, viewers, MCP sidecars and helpers.
  Installation must wait for all relevant processes from this exact installation
  to exit. Cancel pending restarts/launches before coordinated shutdown; native
  logout confirmation/cancellation remains authoritative. "Keep logged in" and
  detached/unmanaged viewers block application rather than killing characters.
- Retain stable host/private-profile locations and installation-bound MCP
  commands. Preserve settings on upgrades and provide truthful migration and
  recovery behavior. Reject unknown layouts, stale target versions and changed
  installation identity before invoking the updater.
- Validate the coordinator state machine, duplicate/stale requests, process
  identity, blocked/cancelled logout, separate installations and package/feed
  generation. Native acceptance covers NSIS-to-Velopack migration, five-account
  shutdown cancellation, delayed installation, matching protocol and restart.

## External setup and acceptance

The repository currently has no published GitHub releases. Implementation can
prepare tools and draft metadata without producing a native installer. A real
release needs the user's retained build artifacts and publication decision.
Sentry needs an account/project DSN plus maintainer symbol-upload credentials.
The optional custom feed needs a confirmed HTTPS URL and upload access. Signing
uses existing maintainer credentials if available; none are invented or embedded.
Report these concrete setup gaps separately from completed, reviewed source work.
