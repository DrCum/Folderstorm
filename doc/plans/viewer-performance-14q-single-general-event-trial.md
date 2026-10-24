# Checkpoint 14q: opt-in single-worker General wait trial

Plan written after the complete14p2 evidence, before implementation. Publish
14p2 independently; integrate the native-checked14r read cleanup as the next
separate checkpoint, then branch this trial from that combined performance
stack. Keep every published head stable for its independent build.

## Evidence and scope

The bundled stock scheduler passes the private queue/coroutine/lifetime checks
and has much lower wake p95 without reproducing the historical idle-spin problem
in48 controlled cases. Faster generated bursts cost more active cycles at
widths3/8. The packaged General override is1, and its real consumers already
include audio, large HTTP responses, media startup, material parsing, inventory
cache loads and folder preparation. Source boundaries are recorded in the14p2
General appendix. These facts support a one-worker experiment, not changing all
pools, adding workers or claiming a crowded FPS improvement.

## Work sequence

1. Add an explicit wait-policy constructor option with the existing platform
   behavior as its default. Resolve effective policy once using the same actual
   configured width stored by the constructor. An event-wait request is eligible
   only for a single-worker pool on Windows; widths0/3/8 or other platforms retain
   existing behavior. Do not guess the resolved width at the caller, read saved
   settings on workers or switch a running scheduler. Preserve all old call sites.
2. Supply that request only at General's owner-side startup call through a
   default-FALSE nonpersistent debug setting. It is selected before start and
   requires restart. Preserve names, automatic shutdown, queue capacity/admission
   and sizes. ImageDecode, ImageGL, mainloop and all callbacks remain on their
   current policies/threads. Log the requested/effective policy and resolved
   width once so a later tester can verify the path without disclosing raw logs.
3. Compile fresh production threadpool.cpp/workqueue.cpp into a native fixture.
   Test legacy/default, eligible one-worker request and rejected wider requests,
   including a real CommonControl width override. Reuse meaningful queue/fiber/
   close/lifetime coverage but route it through production selection rather than
   installing an algorithm only in the test override. Verify opt-in settings
   packaged default/persistence and the only supplying call site. Keep failures
   bounded and preserve real admission/rejection semantics.
4. Inspect normal/Tracy compilation and retain exact provenance. Any comparison
   uses isolated private pools and the existing build-idle controls. Do not
   silently change14p2's frozen source/executable or rerun its full matrix for
   unchanged code. Do not manufacture a quality/visibility-rate improvement.
5. Publish a separate exact-head PR, then write the next plan from actual results.
   Its normal default installer must continue using the original policy. Later
   manual acceptance can use an explicit startup override and verify the startup
   line, correct values in the performance UI, normal loading/audio/media/assets,
   inventory/folder preparation, region churn, idle and shutdown. Full viewer
   CPU/frame-time and lifecycle acceptance remain gates before default adoption.

Q4/Q7 and the user's runtime findings stay deferred. This adds no animation
workers, frame delay, job cancellation policy, priority change or new graphics
context. A failing candidate returns to the existing startup selection and
retains its evidence. No live login/upload/cache operation is part of this plan.
