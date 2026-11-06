# 14t: bounded reuse removes allocations; serial owned still costs more

This private experiment is implemented at
482809653ddafa1a541396186c12b3997a7f5734, based on published PR 121
81c90614cd1351b6825be2a2e5f19ad2049916db. Production source, settings,
worker selection and readiness strategy are unchanged.

The private retained mode keeps the existing owner binding vector alive only
inside the controlled Scene lifetime. The fixed cohort admits one ordinary
motion per character. Bounds/cleanup are checked; final release occurs on the
owner before Scene destruction and is charged to the complete owner window.
This does not implement a general viewer cache, deprecated-motion resolver or
reentrant scene lifetime protocol.

## Verification and costs

Normal and allocator-instrumented native builds each pass 67,824 checks.
The actual controller oracle now records character index, motion ID and exact
callback time bits. It compares all 64 query frames for all five modes at
1/8/32 characters, with frame/skeleton/asset/permission stale controls and
explicit final cleanup. Normal/Tracy syntax passes. CMake target integration,
other-platform runtime and live viewer acceptance remain distinct pending gates.

All 12 paired/reversed timing cases and six separate allocator cases pass the
fixed-matrix validator, exact executable/argument/frame identities, resource/
callback accounting and nonoverlap checks. Timing uses 1,024 frames per case,
12,288 frames total; allocation uses 64. Every case observes full-host/private
builder idle before and after. No synthetic gap or worker pool is used in these
three serial performance modes. Desktop/power/timer conditions remain otherwise
uncontrolled; the requested 1ms timer is recorded. Native checks still cover
the unchanged private queue paths separately.

| Characters | Later allocation calls, release / retain | Total requested bytes, release / retain | Held bindings before final cleanup |
| --- | ---: | ---: | ---: |
| 1 | 63 / 0 | 34,576 / 2,320 | 512 bytes |
| 32 | 2,016 / 0 | 1,117,910 / 85,718 | 16,384 bytes |

Direct controls allocate zero. Peak tracked bytes are unchanged by retention:
2,320 for one character and 75,518 for 32. Every case has zero binding bytes
after final cleanup. The allocation split is first owner boundary versus later
work/final drain; it is not a separate drained warm-up trial. Requested-byte
counts are replacement-new instrumentation, not total process memory.

Across the two timing orders, retention uses 0.915–0.924 times the released
path's owner cycle window for one character and 0.932–0.986 times for 32.
It still uses 1.186–1.198 times direct for one character and 1.149–1.168 times
direct for 32. Crowded p50 owner wall time is 246.4/245.5us retained versus
214.4/215.7us direct. Crowded p95 varies between rounds, so no tail improvement
is established. Cycle windows include complete Frame lifetimes, bookkeeping,
resource checks and final cleanup; per-kernel cycles are attribution within
them and must not be added again.

## Decision and next plan

Bounded reuse removes the identified recurring allocation in the controlled
cohort, but does not make serial capture/sample/apply cheaper than direct.
Keep this as private cost evidence. There is no viewer optimization, FPS gain,
new cache or worker dispatch to enable in this checkpoint.

Plan 14u tests a separate same-frame consumption strategy: when early callbacks
miss, use immediate direct fallback while leaving the one owned batch available
for later callbacks, then cancel remaining work at the frame boundary. Preserve
the same current-context checks and callback order, measure wasted sampling and
complete CPU cost, and retain a matching first-consumption control. This is a
private experiment only. Q4/Q7, real deadlines, live General lifecycle, crowd
quality and FPS acceptance remain deferred while the user is working.
