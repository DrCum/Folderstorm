# 14s complete-path results: do not integrate this batch design

The test-only controller experiment is complete. Its corrected implementation is
ed052e24b57a84e75856f886af80c17501bb146b, based on PR 120 exact 55f66ec851.
The production controller, schedulers, animation dispatch and settings are
unchanged by this checkpoint. The viewer compiles the original controller;
only explicit private test targets use the two generated seams.

## Evidence and scope

All 72 prewritten timing cases and 12 separate allocation cases completed:
4,608 timing frames, 71,680 real controller callbacks, 27,577 owned applications.
Every accepted case has matching executable/argument identities and before/after
full-host/private-builder idle observations. Accepted intervals do not overlap.
The original unrun build-busy precheck is preserved and replaced by a new attempt.
The analyzer validates every planned cell, exact frame index, callback accounting,
admission/mask counts, cycle windows and executable identity. No measured stale
application occurred. The private correctness fixture passes 4,240 checks in
normal and allocation-instrumented builds; normal/Tracy syntax and five generator
byte/boundary checks also pass. CMake target integration and non-Windows runtime
acceptance remain unproved by these standalone checks.
An additional 30,861-check native oracle compares every one of the same 64 query
frames, including loop wrapping, across 768 normal/busy/closed controller frames.
It compiles the same frozen fixture/core, forces readiness only for correctness,
and performs direct pose/order/hand comparisons outside the measured matrix.
No oracle work is charged to or hidden inside the timing cycle windows.

The generated dataset has 1 or 32 ordinary characters, four overlapping motions per
character, 32 joints and 32 keys, with four shared immutable assets. Exact current
mask prediction admits one motion per character. Private pools use actual
production width/policy selection, effective width 1, capacity 1,048,576 and one
bounded pending batch. Busy/closed controls use 8 characters. No live viewer,
account, real scene window, quality change or extra animation frame is involved.

PR 120's exact head is separately retained as 7.2.5.82290, with 39 focused checks,
normal Tracy/contiguous-key settings OFF and a verified installer hash. Its
packaged startup trial is still Boolean FALSE/Persist0, with preceding controls
unchanged. The builder corrected a helper dependency guard without a product
patch or rebuild. This is build/package evidence, not live General acceptance.

## Findings

An unready batch is cancelled at its first actual callback. Both schedulers
therefore apply 0% of work at immediate consumption in both cohort sizes and
rounds. In the 32-character immediate cases, combined owner/worker cycle-window
cost is 1.27â€“1.64 times direct. Faster delivery cannot supply a nonexistent
independent window for this design.

The crowded-cohort observations, across paired/reversed rounds:

| Explicit synthetic gap | Polling applied | Event applied | Event combined cycle window / direct |
| --- | ---: | ---: | ---: |
| 0us | 0% | 0% | 1.35â€“1.64 |
| 100us | 0% | 0% | 1.12â€“1.32 |
| 500us | 0â€“3.1% | 96.9â€“100% | 1.09â€“1.25 |
| 2000us | 4.7â€“92.2% | 100% | 1.03â€“1.05 |

These cycle windows include the explicit busy gap, loop bookkeeping and final
drain. Per-job counters are attribution within those windows and must not be
added to them again. They are neither energy measurements nor viewer frame
ratios. Removing the synthetic gap from owner wall time, the 500us event cases
have p50 277.6/169.3us versus direct 248.6/238.3us. That round-to-round variation
does not establish repeatable owner savings. The small cohort also provides no
consistent whole-path win. Desktop load, power and timer state remain uncontrolled
beyond the documented requested1ms timer and build/probe-idle guards.

Current owner preparation/cleanup deliberately releases its binding snapshot at
each frame, matching the scoped diagnostic's footprint. The serial owned path
records 2,123 replacement-new calls and 1,117,910 requested bytes across 64 crowded
frames; direct records zero. After the first owner boundary, exactly 2,016 calls
and 1,032,192 bytes remain, equal to 32 binding allocations of 512 bytes in each of
the 63 later frames. Worker readiness affects the queued paths' additional
allocations, so their counts cannot be treated as interchangeable warm cases.
This is a concrete next hypothesis, not proof that the pure kernel is unhelpful
or that a general viewer cache is already safe.

## Decision and next plan

Do not integrate this first-consumption whole-batch prototype or enable animation
workers. Retain the source/raw results for independent review. Faster delivery
remains useful evidence for the separately default-off General trial; it does
not establish crowded steady-FPS improvement.

Plan 14t isolates bounded owner-binding retention in the private controlled
cohort, keeping arithmetic, current-context validation, callback order and
same-frame fallback unchanged. First determine capture/memory economics; any
later staggered-consumption proposal needs its own plan and lifecycle/deadline
evidence. Real Q4/Q7, crowd, quality and General lifecycle acceptance remain
deferred while the user is working.
