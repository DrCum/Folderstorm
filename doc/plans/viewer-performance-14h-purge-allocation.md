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

Not implemented yet.
