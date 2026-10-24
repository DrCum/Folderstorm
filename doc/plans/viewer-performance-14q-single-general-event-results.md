# Checkpoint 14q: single-worker General event-wait trial

Implemented at `9043662887c4675bc7ddfe8c413e1494e8ff3e75`, on the complete stack
through PR119. The packaged startup control FSGeneralPoolEventWait is Boolean
FALSE and Persist0. All preceding packaged settings are unchanged.

## Selection and limits

An explicit constructor request is resolved once against the same configured
width the pool will start. Only Windows with resolved width 1 accepts it. The
effective policy is immutable. Every old constructor call defaults to the
existing platform behavior; ImageDecode, ImageGL, mainloop and all widths retain
their previous selection. General is the sole production caller supplying the
request, before start. Its original capacity/automatic-shutdown arguments remain.

The worker startup path installs bundled round_robin for an eligible request,
or the existing sleepy_robin on the Windows default path. Other platforms keep
their existing installation behavior. No new wait primitive, worker thread,
callback owner, motion dispatcher, frame delay or quality rate is introduced.
The internal class layout/signature changes require a matching full rebuild;
native validation compiles fresh threadpool.cpp and workqueue.cpp.

## Verification

The actual production path passes 4,593 native checks, without a test scheduler
override. These cover defaults, eligible requests, rejected wider/zero-width
requests, actual CommonControl overrides, changes after construction, no worker
settings queries, FIFO/exactly-once values, payload release, coroutine progress/
affinity/timers, scheduled work and earlier heads, producer/close races, full/
closed rejection and the real LLApp automatic drain listener in a private
process. A private LLControlGroup verifies the unsaved true/false Boolean path
used by the startup command. No viewer/window/account is initialized.

All 18 source/packaging checks pass, including the only supplying call site,
Windows/resolved-width gate, immutable storage/default arguments and unchanged
preceding packaged controls. Normal compilation passes for the core, fixture,
viewer startup and image callers. Tracy compilation passes for the core,
fixture and startup. Full CMake LL_TESTS integration and other-platform runtime
acceptance remain unproved by standalone checks.

Four short actual-path delivery observations run default/event/event/default
with a scoped requested 1 ms timer and before/after full-host build-idle checks.
All 256 submissions are admitted with no overlap or rejection. Default p95 is
2,001.7/2,033.1us; event p95 is 18.0/21.3us. Constructor fallback 3 is overridden
to 1, then the test control changes to 8 before start; the actual pool stays 1.
This verifies installation/delivery through the new production path. There is
no timing pass threshold or new sustained-CPU/FPS claim. Prior 14p2's complete
algorithm evidence remains unchanged. Desktop load/power/timer state is not
controlled. Source/executable/response/dependency identities and raw cases are
in `results/worker-policy-provenance14q.json` and `results/worker-policy14q`.

PR118 is independently retained as 7.2.5.82283 with 37 passed focused checks;
PR119 as 7.2.5.82287 with 38. Both exact heads and installer hashes were verified
in `results/parent-build-receipts14q.json`. This newer checkpoint still needs
its own exact-head full viewer build and application acceptance.

## Later manual trial, deferred

Use the normal retained-viewer launch command with:

```
--set FSGeneralPoolEventWait true
```

The aggregate startup line must say
`GeneralWaitPolicy requested=event-single effective=event-single threads=1` to
classify an enabled run. Keep the existing pool sizes; wider resolved pools use
platform-default. In-session debug edits do not switch an existing pool. Relaunch
without the override to return to the packaged default; the setting is not saved.

Compare warmed normal builds with builds idle and unchanged quality settings.
Record startup/assets, audio, media open/close, materials, inventory/folder
preparation, region churn, idle and shutdown behavior as well as CPU/frame-time.
General's jobs already run off-thread; reducing their delivery delay does not
establish a crowded steady-FPS improvement. No manual action is requested while
the user is working, and no live operation was performed here.

## Next plan

Plan 14s re-evaluates the complete owned-animation path using actual production
selection and the latest controller/admission changes. It is private first:
same values/owner order/fallback, whole-path costs and bounded jobs before any
viewer dispatch proposal. Q4/Q7 and rendered acceptance stay deferred. Keep this
trial default OFF until application evidence supports a broader decision.
