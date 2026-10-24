# 14s native progress, timing pending

The private complete-controller fixture is saved at
8b2b248affde3454226e657c37742f377f6791ed on codex/perf-event-sampling-cost,
based on published PR120 exact55f66ec851. Its runtime source is unchanged.

The optimized native executable passes4,192 checks through freshly compiled
current core/controller/pose/keyframe and production threadpool/workqueue code.
Correctness compares ordinary1/8/32-character cohorts with direct controller
execution, including active-order/joint/hand state, actual mask admission, ready
application, frame/skeleton/asset/permission staleness, and bounded busy/closed
fallback. Correctness-only ready waits are excluded from performance trials.
The separate allocator executable compiles. Both translation units pass normal
and Tracy syntax; the final fixture's source identity is recorded. Five generator
checks verify LF/CRLF original byte recovery and rejection of ambiguous seams.
Full CMake target integration and other-platform runtime acceptance are pending.

The initial timing precheck refused launch at2026-10-24T21:42:27Z when the
independent monitor entered preparation for PR120. No timing/FPS/CPU advantage
is claimed. Its unrun metadata is retained; the executable was never launched.
Later full-host observation confirmed PR120's exact head, live build owner and
compiler/MSBuild activity. No build ownership, cache, feature source or monitor
was changed here.

The fixed matrix is written before measurement:72 timing and12 separate
allocation cases,64 frames each, with paired/reversed policies/order and explicit
0/100/500/2000us synthetic gaps for1/32-character cohorts. Busy/closed controls
use8 characters. Run only with full-host/private-builder idle before and after
each case. All rejection/late-work costs count; first consumption cancels an
unready batch, and direct same-frame callbacks continue. There is no real scene
window, callback reordering, extra animation frame, live viewer/account operation
or lowered quality in this experiment.

Provenance and matrix are in results/event-sampling14s/. Scratch scripts and both
final native executables remain in performance-validation. The driver resumes
accepted cases without overwriting them and refuses failed/interrupted cases
until inspected. Complete economics and the resulting next plan remain pending;
14s is not yet a completed research checkpoint and has no separate PR yet.
