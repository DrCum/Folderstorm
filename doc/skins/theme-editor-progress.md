# Theme editor implementation

Base: PR #88, `85f620154fe0da09d2a64d7fcc0f9f352f358c49`.
Development branch: `codex/ansastorm-theme-editor-a`.

The user authorized implementation on October 26, 2026 and expects work to
continue while they are unavailable. Preserve any candidate awaiting human
validation, then continue on a new PR. This overrides stopping development at
the build skill's human release-test gate; publication still requires approval.

## Required sequence

1. Gate A: shared contracts, catalogs, projects/packages, native overlay/fonts,
   isolated offline preview, focused automated and actual native visual checks.
2. Complete AnsaStorm palette, image, font, widget/layout and host tools,
   undo/redo/library, launchers, distribution, documentation and acceptance.
3. Add and validate the Folderstorm/Firestorm base adapter.
4. If the user has not intervened, plan and begin customization that no longer
   requires an installed base theme. Retain compatibility/recovery guarantees.

## Current evidence

- Isolated worktree created from the exact pinned PR #88 commit.
- Shared performance source restored after each native build attempt. Retained
  performance artifacts stay preserved; the incremental cache is reused under
  the exclusive build lock.
- Official portable .NET SDK 10.0.403 downloaded and SHA-512 verified outside
  the repository. The global SDK and PATH are unchanged.
- Core and CLI implement project checkpoints/undo, deterministic package
  export/import, strict validation, immutable library publication, base catalogs
  and linked/alpha color compilation. Eight focused regression groups pass.
- C# and C++ agree on all 40 shared manifest fixtures, including Unicode metadata
  and control characters. Native C++ accepted an
  actual managed-exported color/image revision. Long Windows paths are covered.
- The v2 inventory generator hashes asset/XML/font bytes and records all six
  classic schemes. The supplied planning snapshot remains untouched.
- A minimal self-contained x64 WPF editor compiles and publishes. Matching
  viewer 7.2.5.82308 (source `95fce16ca2`) is retained with NSIS, runnable files,
  private symbols, editor and provenance under `D:/Folderstorm-theme-builds/gate-a`.
  It has not been installed or published. Gate A has not passed.
- Computer Use approval succeeded. The editor opened, and its tab contrast
  defect was fixed and visually rechecked. Native viewer checks remain pending.
- Actual native screenshots now cover the built-in sample, no-op overlay and
  combined proof. Custom button artwork, linked accent text and the package-local
  font are visible. Warm owned previews reported ready in 6–8 seconds; the cold
  built-in startup took about 18 seconds. These are preliminary runtime results.
- Viewer 7.2.5.82309 (source `ba79b7829a`) is also retained, with the corrected
  window-size control types, first-run maximization suppression, image-overlay
  tint support and ordinary owned-preview Close. Close completed in 1.16 seconds
  without a quit-confirmation prompt.
- Computer Use can capture an incomplete OpenGL window even while the window
  remains fully drawn on the user's screen. The user confirmed this in the
  ordinary combined preview, and supplied a fully drawn diagnostic reference.
  A later Sky capture of that diagnostic window also matched. This is a capture
  limitation; the earlier evidence does not establish a viewer renderer failure.
  The speculative GL-state refresh has not demonstrated an independent benefit
  and is removed from the next candidate.
- The ordinary combined 82309 preview visibly loads the green image-overlay
  floater tint, mint link, green button artwork and package-local
  monospace font with Cyrillic/CJK fallbacks. Native floater resizing was
  exercised. Capture limitations and remaining state checks are recorded in
  `theme-editor-native-evidence-ba79b7829a` in the local workspace.
- Six native normal-startup recovery cases pass: valid selection, startup
  rollback, explicit reset, missing revision, changed base and corrupt payload.
  All preserve the original skin/font settings and a personal color override.
  Normal-close confirmation was left untouched; the test driver's bounded
  fallback closed only its owned process after eight seconds.
- A two-preview ownership check passes: refresh replaces only the primary
  child/profile, leaves its sibling and sibling request/status unchanged, and
  Close affects only the selected preview. Neither test logs in or installs.
- The asymmetric fixture visibly preserves red/cyan top and green/amber bottom
  corners in the stretched native button. The state-family check found that
  replacing `PushButton_Over`/`PushButton_Press` alone does not bind them in the
  inherited default widget. The proof now supplies a complete button template
  with explicit hover/pressed references. Both validators accept and check the
  two native pressed-image attributes; the new viewer check remains pending.
- Native panel drawing uses image-overlay colors when a background image exists.
  The floater-tint proof now sets those attributes as well as fallback flat
  colors; both validators share the additional typed color attributes.
- Sixteen additional shared boundary probes pass, covering JSON depth/shape, finite
  font sizes, typed XUI geometry, color/image references (including both pressed
  image attributes) and ordered slice bounds.
  The native harness now uses C++20 and warnings as errors, matching the viewer.
- The first native build attempt exposed a Windows environment casing collision
  from a lowercase version override. The second correctly stopped before compile
  when the legacy configure script replaced the requested build ID with Git count.
  A process-local `FOLDERSTORM_BUILD_REVISION` override now preserves monotonic
  private package versions without changing later builders' cache defaults.
- The first full viewer compile found a nonexistent splash header and C++20
  filesystem UTF-8 conversion errors. The fixes preserve Unicode paths, and
  the standalone native validator compiles with the viewer's language settings.
- A subsequent viewer compile found that the settings API returns a smart
  pointer; the session-setting helper now keeps that ownership type.
- The native verifier uses an indexed case-insensitive path set and lexical
  relative paths for already-contained files. Dependency enumeration visits the
  captured resource directories. No-op native verification took 10.54 seconds
  with the build idle; the earlier loaded measurement was 221.36 seconds.
  Package compilation performs the full base verification once.
- No native visual acceptance, human acceptance, publication or installation
  is claimed. OpenSim login testing is authorized if a live check needs it.
