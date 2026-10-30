# Checkpoint 14h: avoid the idle motion-purge collector allocation

Plan written before implementation. Continue independent serial CPU work while
14g's manual same-frame-window and crowd acceptance remain pending. This plan
does not authorize or substitute for a production worker trial.

## Evidence and objective

The actual warmed three-joint controller fixture makes 128 replacement-new calls
/ 3,840 requested bytes for 32 updates, with shadow diagnostics disabled.
Source shows three active-blender list nodes per update and an unconditional
empty `std::set<LLUUID> motions_to_kill` in `purgeExcessMotions`. MSVC's empty set
allocates its sentinel even when the loaded count is at/below the cap of 32.
Confirm that attribution separately before changing the production function.

Construct the eviction collector only in the existing over-cap branch. Preserve
deprecated-motion removal, blender clearing, collection-before-deletion, UUID
deletion order, active-motion checks and final loaded-count bookkeeping. Do not
change the cap, retain extra animations, or alter cache/animation behavior.

## Work sequence

1. Create a new performance branch from the published 14g checkpoint. Freeze its
   actual purge function as an independent oracle for a controlled native
   controller fixture, using real registry/motion lifetime handling.
2. Measure purge alone for empty, below-cap and exactly-at-cap controllers.
   Separate warm construction/allocation probes from timings. On this MSVC
   target the candidate should remove one call per ordinary purge invocation;
   other standard libraries may already avoid an empty-set allocation.
3. Move collector construction and its final consumption under the existing
   over-cap condition. Keep the deprecated-removal prepass and final bookkeeping
   in place. No vector/container replacement, callback reordering or early
   return that skips bookkeeping is part of this checkpoint.
4. Compare actual owning containers, active/loading/deprecated membership,
   destructor/callback order and blend behavior against the frozen oracle across
   below/at/above-cap, all-active, mixed inactive, deprecated duplicate-ID and
   noncanonical/canonical cases. Include normal/minimal controller updates and
   safe teardown. Repeat relevant phase, mask and shadow regressions.
5. Repeat the full warmed controller allocation probe and paired native costs.
   Expect only the idle purge sentinel cost to disappear; active-blender nodes
   still allocate. Preserve raw reports and component scope limits. Full viewer
   linking, cache/loading/outfit/arrival/departure and crowd acceptance remain
   separate evidence, with diagnostics OFF for normal FPS comparisons.
6. Publish a separately testable PR. Write the next plan for the measured active-
   blender collection cost: its per-frame list allocations and linear duplicate
   searches should be investigated without changing first-admission/reverse
   application order, additive/priority behavior, quantum cache/interpolation or
   joint lifetimes. Keep that structural change separate from this small guard.

## Threading gate that remains open

No real viewer shadow capture has been collected. Follow 14g's manual capture
instructions when a retained build is available. Match coverage, conservative
first-callback gaps, preparation/current-check cost, existing queue wake cost,
buffer pressure and rendered lifecycle behavior must support a bounded trial.
Generated native fixtures cannot close that gate. Continue useful independent
source work rather than silently enabling workers or declaring an FPS gain.

## Results

Implemented on `codex/perf-idle-motion-purge`, from PR 111 at
`1ad2444867c70566619913b092b42cde0f2e25ab`. The only production change moves the
temporary set and its consumption into the existing second over-cap branch.
The deprecated prepass, cap, final logging bookmark and callback order remain.

Native optimized MSVC x64 AVX2 checks with assertions enabled:

- 5,838 checks / 45 real-controller cases pass in each map/contiguous mode.
  Cases cover 0/1/31/32/33/64 loaded motions, inactive/mixed/all-active sets,
  loading motions, genuine deprecated owners with and without same-ID canonical
  replacements, threshold reduction by the prepass, repeated purge, retained
  blending, ordinary/minimal updates and complete teardown. UUID-ordered
  eviction and callback/destructor sequences match the frozen parent function.
- `motion_purge_reference.h` preserves the parent's exact function except its
  class name, literal cap spelling and inaccessible private logging bookmark.
  The latter remains unchanged in production. Restoring those three spellings
  reproduces the saved parent function; its normalized-LF SHA-256 is
  `6e79131aff06641f455cb752c9a5ec7e26bc52a1ab245bd76198ed0fded2ad09`.
  The two fixture clocks are seeded before controller construction.
- Separate replacement-new binaries: 32 purges at each of 0/1/32 loaded motions
  fall from 32 calls / 1,536 requested bytes to zero calls/bytes. Both modes
  return to their allocation live baseline. See `results/motion-purge-*alloc*`.
- The actual warmed three-joint controller falls from 128 calls / 3,840 bytes
  to 96 calls / 2,304 bytes over 32 updates. Disabled observation has identical
  counts; enabled diagnostic/failure cleanup still retains zero owned bytes.
  The remaining three list-node allocations per update belong to plan 14i.
- Focused map regressions pass: 5,956 phase checks / 1,474 frozen updates,
  5,063 shadow checks, 26 allocation/failure/cleanup checks, and 241 mask checks
  / 15 poisoned-storage cases. Standalone test sources and the affected
  controller with Tracy/contiguous enabled pass independent source checks.

Tests compile fresh affected sources against the immutable internal-library
snapshot documented by checkpoint 14g. External packages remain read only.
Nothing in this chat configures, edits or builds the shared viewer cache.

The allocation attribution is established. Uninstrumented paired measurements
ran in parent/current/current/parent order after the independent builder released
ownership. State/worker idle and the full native/package process inventory were
checked before and after every run. Purge uses a non-inlined wrapper, 100,000
calls per row; the unchanged full controller fixture uses 1,000 updates per row.
There are 324 raw rows / 11,016,000 total invocations. Full-controller reports
also retain enabled-shadow rows; the table uses only ordinary direct rows.
Source/executable/dependency/report identity is in `results/motion-purge-provenance.json`.

Medians of 18 samples per entry on the Ryzen 5 3600, nanoseconds per invocation:

| Component | Parent | Candidate |
| --- | ---: | ---: |
| Purge, 0 loaded | 37.037 | 2.917 |
| Purge, 1 loaded | 37.408 | 2.917 |
| Purge, 32 loaded | 37.317 | 2.917 |
| Full controller, 3 joints | 727.85 | 677.45 |
| Full controller, 32 joints | 5,470.25 | 5,454.45 |
| Full controller, 128 joints | 31,469.45 | 31,563.50 |

This isolates an allocation saving and a small controller cost. Larger full
fixtures differ by less than 0.3% in either direction in this sample; the
128-joint candidate is slightly slower. These generated fixed-time components
do not establish a whole-viewer frame-time improvement or a real queue window.
Full viewer linking, crowded-scene frame times, visible animation quality,
cache/loading/outfit/arrival/departure and other-platform acceptance remain
pending. The next source plan is `viewer-performance-14i-active-blenders.md`.
