# 14u: bounded readiness checks at later callbacks

Written after completing 14t and before implementation. Start from its published
exact head on a new performance branch, preserving earlier programs and data.

14s's first-consumption cancellation applies no work at immediate deadlines.
14t removes recurring binding allocation but serial capture/sample/apply remains
more expensive than direct. Neither proves that a later same-frame consumer can
use a completed result cheaply. Test that premise in the controlled cohort;
do not invent a real viewer overlap window or enable a scene dispatcher.

1. Keep direct and matching cached first-consumption controls. Add private event
   and polling modes that check the one pending ticket at each actual callback
   until ready. An early miss executes that motion directly at once; it does not
   cancel the entire batch yet. A ready result transfers exclusively once, and
   each later application still performs production current-context validation.
2. At frame finish, cancel/discard unconsumed work using the existing bounded
   acknowledgement rules. No retry spinning, main-thread wait, extra frame,
   callback reordering, live scene pointer in a job or unbounded pending batch.
   Keep actual production width-1 policy selection and General-sized private
   capacity. Retention stays inside the fixed Scene lifetime with final cleanup.
3. Use character/ID/time callback traces and byte-exact direct pose/hand oracles
   over the same query trajectory. Exercise early misses followed by readiness,
   no readiness, failure/rejection, staleness between consumption points,
   cancellation and final cleanup. Reuse the existing owned numeric/context and
   executor lifetime suites as distinct evidence. Unsupported paths remain direct.
4. Instrument readiness checks, first ready callback, applied/fallback work,
   sampled-but-unused rows, capture/application/cleanup and complete owner plus
   worker windows. Preserve equivalent first-consumption and direct controls.
   Freeze a small paired/reversed matrix before timing; do not use an artificial
   gap as a substitute for the actual generated controller work between callbacks.
5. Time only with full-host/private-builder idle and exact source/program
   identities; retain every exclusion. Publish a separate test-only checkpoint
   and write the resulting next plan. Reject the candidate if complete costs do
   not support useful owner savings. A promising private result still requires
   a separate ownership/lifecycle/real-deadline proposal and Q4/Q7 evidence.

No production dispatcher, general viewer binding cache, quality reduction,
additional profiling variant or live viewer/account operation is authorized by
this private experiment. Existing human testing questions stay deferred.
