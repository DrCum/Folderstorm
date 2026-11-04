# Gate A native validation

Gate A is ready for the full authoring stage at the observed desktop scale.
This records the architectural slice, not final feature or installer acceptance.
The base is the exact PR #88 head `85f620154fe0da09d2a64d7fcc0f9f352f358c49`.

## Retained candidates

- Native 7.2.5.82310: `97c217e00f4a50037095e95f81522106769362bc`.
  Retained under `D:/Folderstorm-theme-builds/gate-a/7.2.5.82310-head-97c217e00f-nsis-attempt-1`.
  The x64 AVX2 OpenSim Nightly NSIS installer, runnable copy, private symbols,
  source bundle, configuration, hashes and logs remain separate and immutable.
- Managed editor 0.1.1: `90f8a1882c2c319e4d39070ee672ab758e29cbf8`.
  Retained under `D:/Folderstorm-theme-builds/gate-a/editor-0.1.1-head-90f8a1882c-native-7.2.5.82310`.
  Native source is unchanged from 82310. The NSIS component does not yet include
  the editor; distribution integration belongs to PR D.
- Corrected combined proof: `1c6027a7ec689670a6213068411bec88319d692e887f0bd51805301006816f6e`.
  This scopes explicit hover/press images to `test_button`, preserving inherited
  checkbox and radio defaults.

## Evidence

Paths below are relative to the local handoff workspace, not this repository.
The accompanying `theme-editor-gate-a-audit.json` records evidence file hashes.

| Check | Evidence and result |
| --- | --- |
| Built-in versus no-op | `theme-editor-native-evidence-95fce16ca2/01-built-in-baseline.png`, `02-noop-after-redraw.png`, `03-built-in-warm-start.png`. Native 82308, 1920x1032 captures, equal widget scale. Base and no-op appearance match; outer activation color and floater position differ. |
| No-op continuity | Old and current no-op packages have zero `viewer/` payloads and identical dependency bytes: `ff5813a445e4a51b6e51f0649566539e38a4aa92d3c54e3b21aaf8de1bd1c108`. Between 82308 and 82310, resource lookup, package validation, fonts and base resources are unchanged. Native changes add preview inspectors, typed window sizing and offline Close behavior. |
| Accent, tint, font and resize | `theme-editor-native-evidence-ba79b7829a/03-combined-normal.png`, `05-combined-interactive-full.png`, `06-combined-resized-capture.png`. Native 82309 shows mint links, green image-backed floater tint and embedded monospace font with Cyrillic/CJK fallbacks; focused text input and resized floater were exercised. |
| Asymmetric stretch | `theme-editor-native-evidence-ba79b7829a/07-asymmetric-native-corners.png`. Red/cyan top corners and green/amber bottom corners retain the native bottom-origin slice geometry. |
| Hover, press, disabled | `theme-editor-native-evidence-97c217e00f/01-combined-normal.png` through `04-combined-disabled.png`, plus native interaction receipts. The user physically verified green hover, orange held press and green release. Disabled input leaves counters unchanged. Pinned press is separately labelled inspector evidence. |
| Checkbox/radio regression | `theme-editor-native-evidence-scoped-82310/human-hover-scope-acceptance.json`. The user confirmed the large hover rectangle is gone and the Button states still work with the corrected revision. |
| Actual editor Preview action | `theme-editor-native-evidence-scoped-82310/editor-end-to-end-ready.json`, `editor-owned-native-status.json`, `editor-02-native-preview.png`. Actual WPF Preview button launches an isolated child with matching request, generation and revision. |
| Lifecycle ownership | `theme-editor-native-ownership-ba79b7829a.json`: two independent children; refresh replaces only its owner; sibling request/status remain unchanged; Close leaves the sibling alive. `theme-editor-native-evidence-97c217e00f/08-owned-close.json`: 1.51-second Close without quit confirmation. |
| Persistence and recovery | `theme-editor-native-recovery-ba79b7829a/results.json`: 6/6 valid selection, rollback, reset, missing, changed-base and corrupt cases pass. Base skin/font choices and personal color override remain intact. Normal-startup recovery children used the driver's own bounded termination fallback; this does not claim graceful ordinary viewer quit. |
| Contracts and project fidelity | Eight core regression groups; 40/40 shared C#/C++ manifest fixtures; 16/16 shared boundary probes. Source project save/reopen/history remains distinct from compiled package import, which starts independent history and preserves payload bytes. |

## Limits and follow-up

- Numeric desktop DPI/UI scale was not captured with the early screenshots.
  Matched captures demonstrate the same observed size/scale only. Do not claim
  100/150/200% coverage. No desktop DPI setting was changed.
- OpenGL capture sometimes returns partial or occluded frames while the user
  sees the complete UI. Manual screenshots and physical state checks distinguish
  that capture failure from a renderer failure. Do not reuse mixed-window frames
  as native evidence. Several saved `.png` names contain JPEG bytes; inspect the
  content rather than assuming the filename describes its encoding.
- The actual editor Close button could not be retested after capture failures.
  Its product CloseAsync lifecycle and two-child ownership behavior are tested;
  exact UI-button coverage remains recorded as unavailable.
- Ordinary logged-in viewer surfaces, all six schemes' full visual acceptance,
  broader asset sizes, DPI, distributions and complete editor workflows remain
  in the later acceptance phase. An offline widget sample does not establish
  those results.
- No installation, publication, merge, authentication or personal-viewer control
  occurred. Human acceptance here covers the specific reported state tests only.
  Retained candidates stay available for later testing.
