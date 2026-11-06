# Checkpoint 14p2: sustained private scheduler results

All 48 planned cases completed on the Ryzen5 3600 host, covering widths1/3/8,
default/scoped requested-1ms timers, empty/finite-coroutine idle phases and
current/stock order followed by stock/current. Accepted windows do not overlap.
Two early overlapped cases are excluded and replaced; one build-preparation
precheck never launched a probe and is retained alongside its later Attempt2.
The builder's work/retention was left untouched. Total accepted idle observation
is1,446.56 seconds, with3,072 admitted wake samples, zero rejections and196,608
exact active-task results. No resource cap or correctness failure occurred.

## What the measurements show

| Context | Current polling wake p95 | Bundled stock wake p95 |
| --- | --- | --- |
| Default timer | 15.9975-16.0212ms | 31.3-142.5us |
| Scoped requested1ms | 1.9979-2.0359ms | 15.2-25.1us |

These are ranges of per-case nearest-rank quantiles, not pooled confidence
intervals. Polling p50 varies with phase and sometimes lands in microseconds;
the stable difference here is in the tail, not an always-2ms median. Wake and
active phases precede finite-wait coroutines. Their wait label describes the
following idle control, not a loaded-coroutine wake experiment.

Stock empty pools record no accumulated idle worker ticks/cycles in the accepted
samples. With the timer request, stock finite waits use fewer raw cycles; with
default timers the finite-wait totals are closer and sometimes cross between
variants. Quantized worker CPU readings are low in both: the maximum total-pool
observation is1.98% of one core for current and0.573% for stock. Those percentages
are not of the whole12-logical-processor machine. Zero ticks/cycles in one empty
sample do not establish zero power use or general idle behavior in the viewer.

The generated4,096-task burst completes in5.2-20.7ms with stock, versus0.43-4.75s
with polling. Capacity512 intentionally forces queue/admission interaction;
General's default capacity is1,048,576. Active wall time includes producer
admission and delivery, not just arithmetic. At widths3/8, stock uses more
active worker cycles despite shorter completion time: width8 ranges165-194M
cycles versus42-75M current. At width1 the ranges overlap:18-33M versus16-31M.
Do not translate this fixture into a viewer throughput/FPS or universal CPU
saving claim. More workers and quicker wakeups can increase active contention.

## Correctness and provenance

The unchanged private harness passed6,686 queue, scheduled deadline, coroutine,
admission, producer/close, lifetime and shutdown checks across both modes and
widths1/3/8 after the separately published queue prerequisite. It had originally
stalled before that fix; the failure remains retained. The independent PR117
builder also reports that private correctness check passed at its exact runtime
head. The test-only checkpoint leaves production threadpool.cpp identical.

Normal/Tracy standalone checks passed for the harness and benchmark. Exact
sources/executable/response identities, source/compiler review, raw CSV/JSON
case records, host description and reproducible analyzer are in
`results/worker-wait14p2`. The analyzer verifies retained LF-normalized hashes,
unique matrix cells and wake indexes, matching context labels, complete30-second
idle controls, active checksum, before/after idle flags and nonoverlapping
accepted windows. Internal dependencies are the32 reverified immutable libraries;
Boost1.90.0 packages are read only. Desktop activity, frequency/power policy and
global timer state remain uncontrolled; there are only two ordered observations
per context. These data are suitable for a scoped experiment, not default adoption.

## Decision and next plan

Publish this test-only checkpoint separately. Its viewer installer retains the
current Windows scheduler, every width and quality setting. PR117 is independently
retained as7.2.5.82278 at exact22ea4cee92, with installer hash and37 focused checks
verified. This checkpoint's own exact-head full build remains pending.

The next proposal is plan14q: an explicit default-OFF, restart-selected Windows
General trial only when its constructor's resolved width is one. Packaged General
is1 despite the fallback3 argument; actual overrides remain unobserved. The
wider active-cycle tradeoff is a reason to keep width3/8 on existing policy in
the initial trial. Preserve real queue/admission/owner callbacks and all widths.
Exercise the production selection path in native tests, then require ordinary
startup/assets/audio/media/inventory/materials/folder preparation, region churn,
idle, shutdown and CPU/frame-time acceptance before any broader adoption. There
is still no production animation dispatch or demonstrated live overlap window.
