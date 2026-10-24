# Checkpoint 14e: owned sampling floating-point context

Plan written before implementation. Parent: protected phase contract and defined
initial controller masks. No production worker dispatch is enabled.

## Objective

An owned sampling result must not depend on which unrelated job last ran on a
worker. Capture the owner's relevant numeric controls with its query, establish
those controls while sampling, restore the worker's previous environment on all
exit paths, and reject a result if current numeric controls no longer match.
Keep ordinary live serial playback free of this experimental preparation cost.

## Work sequence

1. Audit the actual viewer startup, platform intrinsics/CRT headers and supported
   compiler floating-point behavior. Identify rounding, denormal handling,
   exception masks/status and any precision controls that affect the kernel.
   Do not assume standard fenv alone captures FTZ/DAZ or compare opaque padded
   environment objects byte for byte. Use primary platform documentation where
   the local implementation does not establish the contract.
2. Define a nonallocating owned numeric-control descriptor and scoped restore
   guard for the target x64 AVX2 build. Fail preparation/sampling safely when a
   context cannot be captured or established. Unsupported platforms retain the
   normal direct path; do not silently claim equivalent background math.
3. Capture the descriptor on the owner thread in checkpoint 14c's preparer.
   Carry it in the move-owned request, include the relevant semantic controls in
   independent current-input validation, and establish/restore context inside
   the pure sampler. Keep workers away from live state/settings/caches. Update
   manually assembled test requests explicitly rather than relying on an
   accidental default context or copying context back from a completed result.
4. Test actual curve/JointMotion math with underflow-sensitive values and
   different rounding/denormal modes. Deliberately alter a reused worker's prior
   controls, require exact results against the owner's direct computation, and
   verify the prior worker controls/status are restored after success, rejected
   requests and failure paths. Include same-thread use and ordinary serial-path
   preservation; tests must restore their own caller state too.
5. Repeat owned-sampling and phase/controller fixtures in both curve modes and
   compile supported platform branches independently where available. Record
   any limits of compiler optimization, CRT capture or platform execution; avoid
   broad scheduler changes to solve a per-job numeric contract.
6. Measure the guard/preparation/application overhead separately using the
   existing complete-cost experiment. Do not combine allocator probes with timing
   or compare a new guarded worker path against an old cost report as if only
   sampling time mattered.
7. Publish this checkpoint separately and write the next plan for explicit
   ordinary-motion eligibility, independently current frame/activation/skeleton/
   edit identity and exact same-frame controller-time prediction. Preserve
   subclass callback order and real pose-consumer barriers. A predicted query
   still requires validation at application, and a real overlap window remains
   a measurement requirement before live dispatch/default enablement.

## Acceptance limits

No inherited stale worker mode, leakage into the next worker job, accepted
results from a different current numeric context, worker access to live objects,
changed ordinary serial behavior, or unsupported-platform equivalence claims.
Numeric controls do not replace asset immutability or lifecycle validation.
Native fixtures do not establish real crowd FPS or rendered/lifecycle acceptance.

## Results

Not implemented yet.
