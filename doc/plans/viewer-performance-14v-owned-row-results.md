# 14v results: early owned motion publication

Private experiment on top of exact PR123
6daba8aa6619e678f19fbada673c8295922f867b. Implementation and frozen matrix:
783a615808a80362e99d42c34823d10d2121fa6c. Viewer runtime source is unchanged.

Individually publishing motion results produces repeatable owner savings in this
controlled32-character fixture, with higher combined CPU. Keep the candidate
private and investigate the crossover and real owner phase before any viewer pilot.
This is not rendered FPS, a live deadline, or general motion-lifetime acceptance.

## Ownership and correctness

Two fixed32-cell groups hold only owned request/result/observation data. A worker
publishes a finished cell with release; its real owner callback acquires, validates
current context and applies that const result or immediately runs direct. A
published cell stays immutable through the owner's read epoch. Cancellation and
executor discard never clear cells. Reset requires finished owner reads AND the
exact ticket's acquired Expired acknowledgement; shared_ptr counts are not used.

Normal and separate allocation controller builds passed161684/161716 checks,
including64 query times at1/8/32 characters, byte-exact pose/controller/hand output
and character/ID/time callback order. Counts include cycle reads and bounded
correctness waits. The36 new deterministic row transitions cover partial
publication while the worker remains held, early misses/later hits, rejection,
individual failure and mid-callback frame/skeleton/asset/permission staleness.
All preceding52 whole-batch transitions remain covered. Timed requests contain
none of these gates/hooks/forced waits.

The primitive passed85 normal checks before allocation-only fault controls and98
in its current allocation build. Queued/running callbacks safely outlive their
fixture, finished lease, pool and executor using only owned data; weak asset leases
expire after acknowledgement. Allocation failure publishes failed cells, and
acknowledged reuse recovers. Normal/Tracy syntax passes for both current private
units. Explicit CMake targets are excluded from default builds; full CMake target
integration and other-platform execution remain unproved.

## Fixed comparison

All24 planned observations passed exact executable/argument/count identities,
resource/useful-waste accounting, full-host/private-builder idle guards and accepted
window nonoverlap. No exclusion or retry was required. There are16 timing cases
of1024 frames and8 separate64-frame allocation cases:1/32 characters, direct,
whole-batch event later consumption, event row publication and platform-default
row publication. Timing has two paired/reversed orders, actual generated controller
work, zero artificial gap, one bounded callback and requested1ms timer.

The32-character cases, paired against their same-order direct control:

| Mode | Applied callbacks | Owner cycles / direct | Combined cycles / direct | Owner frame p50, us |
| --- | ---: | ---: | ---: | ---: |
| Direct | 0 | 1 / 1 | 1 / 1 | 219.9 / 220.6 |
| Whole event batch | 46.31% / 47.08% | .879 / .944 | 1.392 / 1.483 | 196.9 / 207.6 |
| Event rows | 90.52% / 91.69% | .684 / .674 | 1.186 / 1.183 | 151.2 / 150.5 |
| Default rows | 0% / 0% | 1.067 / 1.067 | 1.070 / 1.070 | 240.8 / 241.9 |

Event rows first apply at median callback3 rather than whole-batch callback18.
About8.2–8.7% of sampled joints remain unused, versus52.9–53.5% for the whole batch.
Event-row p99 is238.1/307.4us versus direct400.9/401.6us. These are private fixture
latencies, not actual viewer frame times. Solo event rows apply only1/1024 callbacks
in each order and cost1.343–1.425 owner cycles and2.195–2.319 combined cycles relative
to direct. The broader viewer should not dispatch this workload indiscriminately.

In the separate crowded allocation observation, event rows request206 allocations
and74320 bytes versus the whole event batch's302 and179115; direct requests zero.
Post-first-frame counts are110 versus195. These instrumented runs affect worker
progress and are not latency/throughput comparisons or a warmed identical-progress
allocation comparison. Both keep16384 binding bytes inside Scene lifetime and
release those bindings at final cleanup.

Resident group cells plus vector capacities are116336 bytes for crowded event
timing and71280 for its allocation run; fixed cells alone are18032. Both are below
the fixed-cohort bound. Different progress warms one or both groups. Shared asset
storage, allocator/control-block overhead and observation storage are excluded from
that capacity figure. Pool/executor/group and observation setup precedes measured
windows; complete owner windows include resets, capture, queue/check/apply/fallback,
cancellation, bookkeeping, final drain, group destruction and binding cleanup.
Complete worker cycles include queue/scheduler work; per-job cycles are attribution
within that window. Desktop/power/timer load is uncontrolled beyond the idle guards.

## Retention and next boundary

Raw48 files, hashes, program/compiler identities,32 verified frozen dependency
libraries, generator provenance, validator and summary are under
`results/owned-rows14v`. Historical primitive failure and partial progress remain
preserved. The failure was request metadata carrying over to a new epoch; reset
now clears identity/context/time/usage size while retaining capacity.

Parent PR123 build7.2.5.82303 has verified retained source/configuration and
installer SHA256EAA32A3AA1EF40E41199E0A2EF63B55181200B33010F3D3AD375CDBE37B5D037.
Its receipt has32 native result records, five passed configuration records and
one explicit experimental-coverage record, which says these private cost programs
were not run by the builder. This experiment's programs were run separately here.

Next follow14w: audit the real controller owner phase and measure the intermediate
cohort crossover privately. No production dispatcher, scheduler default, cache,
quality rate, extra frame, live login, install or additional profiling variant is
introduced. General deprecated/reentrant lifetime, mutable live-asset ownership,
Q4/Q7 and eventual crowd/quality/General/FPS acceptance remain pending.
