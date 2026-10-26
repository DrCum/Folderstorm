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
- Sustained preview rendering can omit most textured controls until a resize.
  Restoring GL state after offline texture uploads is under test. Window-size
  control types and first-run maximization were corrected, and offline Close
  bypasses the ordinary quit-confirmation prompt. These fixes need a new viewer.
- Native panel drawing uses image-overlay colors when a background image exists.
  The floater-tint proof now sets those attributes as well as fallback flat
  colors; both validators share the additional typed color attributes.
- Ten additional shared boundary probes pass, covering JSON depth/shape, finite
  font sizes, typed XUI geometry, color references and ordered slice bounds.
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
