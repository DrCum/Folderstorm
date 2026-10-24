# General pool: source boundaries for a possible later trial

Source review while the independent PR117 build owns the shared cache. This is
an appendix to 14p2; partial data does not support scheduler adoption.
No production source or settings change is made here.

## Packaged width and startup

`LLAppViewer::initGeneralThread` requests three workers, but the packaged
`ThreadPoolSizes` setting supplies the integer `General: 1`. The constructor's
`getConfiguredWidth` uses that named integer when the settings listener is
available. Configuration loads before initThreads, and the viewer control
listener is statically registered. Therefore the packaged ordinary path selects
one General worker, rather than the call site's fallback three. User overrides
and unusual/failed settings access can differ; no active viewer count was read.
The existing 1/3/8 matrix covers both the packaged path and constructor fallback.
Keep width 1 central in the eventual assessment and preserve existing sizing.

The packaged ImageDecode integer 9 is not a verified running decode width:
initThreads computes and replaces that entry before creating the decode pool.
Keep the separate source/runtime distinctions from 14o. The General template's
default capacity is 1,048,576; changing wait policy must not change capacity,
admission, thread counts or scheduling priority.

## Existing General consumers and application acceptance

These jobs already leave the main thread. A wait-policy trial would change
their delivery timing, not create new ownership permission or make them parallel
with other jobs in a one-worker pool. Long running work still delays following
FIFO jobs. The generated active benchmark includes admission/delivery costs and
does not reproduce these real job bodies.

| Consumer | Worker/owner boundary in current source | Acceptance consequence |
| --- | --- | --- |
| Audio decode (`llaudiodecodemgr.cpp`, startMoreDecodes) | Decode state is returned to a mainloop reply. Admission is bounded by twice the General pool's actual width. | Preserve sizing, decode admission, audible readiness, error completion and shutdown while decode/write is in flight. More workers would also change memory/IO pressure. |
| Large HTTP bodies (`llcorehttputil.cpp`, HttpCoroHandler) | Bodies above 65,536 bytes parse on General, then replyPost/release returns to main. Rejected posts retain the existing direct path. | Large inventory/AIS replies, error/admission fallback, retained response lifetime and closing with replies pending need application testing. |
| Media process startup (`llpluginprocessparent.cpp`, STATE_LISTENING) | Copied process parameters go to the worker; the main reply checks the instance registry before installing the result. | Test ordinary media startup, close/reopen and shutdown during creation. Private lambda/payload checks do not reproduce a plugin process lifecycle. |
| Material assets (`llgltfmateriallist.cpp`, onAssetLoadComplete) | Worker reads/deserializes a retained asset payload/model. Main reply applies the model, completes the material and releases the payload. | Material readiness, asset errors, arrivals/departures and shutdown remain viewer gates. A faster worker is not permission to apply models off-thread. |
| Inventory disk cache (`llinventorymodel.cpp`, requestCacheLoad) | Main resolves paths/generation; the worker loads an owned result; the main reply rejects a stale generation. | Startup, cancellation/login reset and stale-result cleanup require application acceptance. Preserve generation and owner checks. |
| Folder upload preparation (`llviewermenufile.cpp`) | Worker scans/validates an owned plan; main replies validate the current session/destination and advance the operation. | Preserve feature-stack behavior. Later manual testing can exercise scan/cancel before confirming any upload; no upload was performed for this audit. |
| Cache maintenance (`llviewermenu.cpp`, llappviewer.cpp) | Explicit disk purge and moved CEF-cache cleanup use General; normal source owns the paths and lifetimes. | Startup/exit behavior and long-job queue contention matter. Do not alter cleanup ownership or invoke those operations as a benchmark. |

`LLAppViewer::forceErrorWorkQueueCrash` also deliberately posts a throwing test
job; its existence is not an ordinary lifecycle workload. The private harness
catches its own test exceptions before callbacks return and does not exercise
the application's crash reporter.

The initThreads comment mentions LLPerfStats, but current StatsRecorder::send
calls processUpdate synchronously. Source contains no General dispatch there.
Do not attribute stats aggregation work to General based on the comment. It
uses mutable double-buffered maps and owner-side tuning consumers; moving it
would require a separate ownership/deadline audit and measured benefit.

## Shape of a later proposal, still conditional

The completed 14p2 matrix now supports only the scoped14q proposal: explicit,
default OFF, limited to Windows General with resolved width1, selected before
start. Wider burst active-cycle costs argue against a broader initial trial.
Snapshot the policy on the owning thread; never read live saved settings
from each worker or switch a running scheduler in place. Leave ImageDecode,
ImageGL, mainloop, queue semantics and all widths unchanged. Native tests must
exercise the real production selection path, not just the probe's run override.
Retain rollback to the current Windows scheduler across a restart.

The private tests establish queue/fiber/lifetime behavior, not these application
paths or a crowded-animation deadline. Startup/loading/audio/media/materials,
inventory/folder preparation, idle, region churn and shutdown acceptance still
precede default adoption. No forced extra profiling build or live account
operation follows from this source appendix. Q4/Q7 remain deferred.
