# Checkpoint 14s: complete animation-path cost with production event waits

Plan written after implementing/native-checking14q, before this experiment.
Start from its published exact head on a separate performance branch. Preserve
14p2's frozen controls and all published heads for separate builds.

## Why revisit this path

Plan13/14e's complete-path work found that polling delivery and preparation can
consume the apparent owned-sampling benefit. The real production event path now
has much lower measured delivery p95, while later mask admission/controller/
allocation changes alter preparation costs. That changes a material premise;
kernel speed alone still proves neither spare CPU nor a useful real viewer
window. Return to avatar-heavy work rather than extrapolate an asset-queue
result into steady FPS.

## Work sequence

1. Audit the current native whole-controller/current-context/batch fixtures and
   latest phase/admission interfaces. Identify the exact input/result ownership
   and first-consumption points. Use actual production pool construction with
   the real resolved-width gate and General-sized capacity; give private pools
   unique names. Do not borrow a viewer pool or install an algorithm override.
2. Reproduce direct/fallback/applied outputs on identical ordinary-character
   cohorts and deterministic input data before timing. Preserve controller/
   callback order, hand publication and source math. Jobs own immutable asset/
   query/result data; workers never traverse live characters, controllers,
   bindings, skeletons or scene pointers. Maintain per-job numeric guards and
   current owner validation. UI/self/control/unsupported paths stay direct.
3. Compare default/event private paths and the unchanged direct owner path with
   latest advisory mask admission. Include immediate consumption, explicit
   synthetic overlap intervals and a busy/rejected queue. Label simulated gaps
   honestly; none proves a live window. Keep one bounded pending batch/capacity,
   no retry spinning and same-frame direct fallback for misses or stale context.
   Cancellation/destruction/asset/clock/skeleton changes must not apply old work.
4. Measure preparation, queue/delivery, pure work, validation/application and
   direct fallback together, including owner wall time/raw cycles and allocator
   calls/bytes/peak in separate instruments. Include small and crowded cohorts;
   reject a candidate that merely hides its capture/apply cost or raises total
   CPU for no useful owner saving. Reuse existing meaningful numeric/lifecycle
   oracles instead of another broad collection of smoke tests.
5. Time only after the independent build/worker state and full host compiler/
   packager/probe inventory are idle before/after each bounded case. Preserve
   source/data/executable identities, paired/reversed order, failed admissions
   and interrupted/overlapping observations. Do not alter power, affinity,
   priority, quality or any live viewer/account to obtain a result.
6. Publish an independently reviewable test-only checkpoint with exact evidence
   and the resulting next plan. If whole-path economics remain poor, retain and
   reject/defer that candidate. If promising, design a separately gated viewer
   pilot only after the ownership/deadline evidence supports it. Do not enable
   animation workers or reorder scene/controller callbacks in this experiment.

Real Q4/Q7 spans, configured pool load, rendered/lifecycle and crowded FPS
acceptance remain missing. Keep those questions deferred; this experiment
answers whether the latest complete path merits the next proposal, not whether
the overall performance objective has been achieved.
