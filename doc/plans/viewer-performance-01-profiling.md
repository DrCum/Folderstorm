# Checkpoint 1: avatar profiling coverage

Plan written before implementation. Depends on the roadmap checkpoint #89.

## Hypothesis

Crowd FPS is often limited by per-avatar CPU work. Existing Tracy zones cover
avatar idle, motion controller, complexity, rigging and skinning palette builds,
but `updateCharacter` and its root/head/world-matrix phases are not separated.
Without those boundaries, a profile can attribute unrelated avatar work to one
large idle zone. This checkpoint supplies attribution, not an FPS optimization.

## Implementation

1. Add avatar-category function scopes to character update, root transform and
   head offset. Give the caller's child-world-matrix update a distinct scope.
   Avoid adding a profiler event to each recursive joint update.
2. Separate palette matrix calculation from GPU-layout packing within its
   existing cache-miss zone. Preserve the current once-per-frame cache.
3. Retain all existing update ordering, visibility/animation policies, formulas,
   defaults and thread ownership. Add no settings, queues, log output, avatar
   identifiers or new timers to normal non-Tracy builds.
4. Document a matched baseline/checkpoint profiling build and capture protocol.
   Defer automatic capture/export tooling until an actual input format is needed.

## Validation

- Inspect the diff for identical non-instrumentation operations and order.
- Compile the affected native translation unit in syntax-only mode against the
  installed Windows dependencies when possible, with Tracy off and on. Keep
  temporary compiler output in this checkout's separate test directory.
- Do not add tests that only assert the presence of profiling macros.
- Full viewer linking, live zone visibility, profiling overhead and rendered
  equivalence remain local acceptance items. Do not report them as passed.

## Building agent handoff

Normal build defaults: 64-bit `ReleaseFS_open_AVX2`, Nightly, NSIS. No change is
required for a normal compatibility build; new zones compile out without Tracy.

For detailed CPU attribution, build both frozen baseline
`d0f05af90f4ed788cb59a5b2d91dd405ed06717f` and this PR head with the same
configuration plus `-DUSE_TRACY=ON -DUSE_TRACY_ON_DEMAND=ON
-DUSE_TRACY_LOCAL_ONLY=ON`. Add these CMake options to the normal configure
arguments; do not change the shared cache while another build owns it.
Use a Tracy client compatible with the bundled Tracy version. Keep GPU profiling
off initially to reduce perturbation; use a separate GPU capture if necessary.

Enable Develop > Profiling/Telemetry > Start when connected, connect locally
after assets warm, and confirm Profiling is active. Use a short initial capture
to confirm the new zones, then the roadmap's matched solo/crowd samples. The
viewer can retain the active flag after a client disconnect; verify profiling
is disabled before collecting uninstrumented FPS measurements.

Look for character update, root/head transforms, child world matrices, motion
controller phases, rigging cache checks, complexity, skinning matrix generation
and palette packing. Compare self time and inclusive time carefully: child
zones are already included in their parents. Record time per frame as well as
per call and total call counts. Compare local avatar, remote avatars and animesh
by their call paths without adding names/UUIDs to captures.

## Acceptance and rollback

- Normal and profiling builds compile/link.
- Requested zones appear only while collection is active.
- Normal animation, posing, attachments, sit/stand, animesh, outfit swaps,
  character switching, teleport/logout and exit remain unchanged.
- No material steady-frame regression with collection off. Measure active
  collection overhead separately before interpreting a gain.
- Any scope collision, unexpected work in non-Tracy builds or excessive capture
  volume is fixed in this checkpoint before proceeding to dependent changes.

## Results

Implementation and focused compilation pending. Hardware capture/acceptance
pending. Next plan: remove a demonstrably redundant avatar CPU operation with
production-path equivalence and operation-count evidence.
