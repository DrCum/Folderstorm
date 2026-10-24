# Checkpoint 14o: main-frame phases and remaining worker candidates

Plan written after implementing and validating 14n. Start a new performance
branch from its completed PR head. Preserve the frozen feature stack through
PR88 and every published checkpoint.

## Objective and boundary

Review the whole viewer's current main-thread work and existing workers before
choosing another implementation. Recent default-playback changes removed
demonstrated allocation/collection work; the last hint-cache experiments did not
justify adoption. Native component results still do not establish whole-viewer
crowd gain, and live sampling dispatch remains gated by real coverage/window
and quality evidence.

This checkpoint audits source and evidence. It does not enable workers, change
animation/visibility rates, operate logged-in viewers, change settings or tune
other processes. The independent builder retains ownership of full builds,
the shared cache, monitoring and installer archives.

## Work sequence

1. Verify the published 14n dependency/head and current review/build evidence.
   Record exact source revisions. Keep build retention, native checks and
   rendered/crowd acceptance distinct; maintain the deferred question queue.
2. Trace the actual main-frame call chain into avatar updates, draw preparation
   and existing asynchronous queues. Identify reads/writes, shared resources,
   callback/reentry points and the last same-frame consumer for each candidate.
   Check whether batching across avatars has an existing phase boundary; do not
   infer independence from one pure numerical function or a synthetic gap.
3. Review visibility/detail, geometry preparation, procedural/flexible/particle
   simulation and terrain work in roadmap order. Verify work already handled
   by workers and current serial caches/invalidation. Avoid duplicating texture,
   mesh or network queues. Document the owner context needed by any proposed job.
4. Rank candidates using existing measured complete costs, preparation/queue/
   wake/consumption overhead and available real CPU attribution. Source loops
   establish behavior and dependencies, not runtime hotspots. Mark missing
   evidence explicitly, including normal/hidden/local/remote/preview paths and
   the six-core target's existing worker load. Do not rerun noisy microbenchmarks
   merely to manufacture an adoption result.
5. If independent redundant work or a credible owned kernel is established,
   write its implementation plan before changing production and deliver the
   separately testable change in another PR. Require bounded memory/lifetimes,
   serial fallback, exact results and no added frame of latency for any worker
   experiment. Major concurrency/default enablement still needs runtime gates.
6. If the remaining choices require runtime attribution/quality/window evidence,
   record the concrete missing evidence and next decision it would resolve.
   Continue only work supported by the available evidence. The performance goal
   stays incomplete until real viewer performance and quality are verified.

## Deliverables

A source-linked phase/ownership map, a short candidate decision table, exact
existing validation/build identities and an updated acceptance queue. Keep
private locations, names and raw user captures out of committed documents.
Existing Q4/Q7 requests remain deferred; the user is working and has not tested
PR114 yet. Do not ask the same question again while useful independent work
continues.

## Results and next step

The source audit is complete in
[main-frame findings](viewer-performance-14o-main-frame-findings.md), with exact
source hashes in `results/main-frame-source14o.json`. It covers the frame/queue/
avatar ordering, role/hidden/session differences, configured worker widths,
existing texture/mesh/GL workers and the remaining visibility, geometry,
physics, flexible, particle and terrain candidates. No source loop is claimed
as a runtime hotspot. The viewer's production code and defaults are unchanged.

The decision is [plan 14p](viewer-performance-14p-worker-wait-acceptance.md):
strengthen the existing private stock-scheduler experiment with sustained CPU,
real-queue/fiber/lifecycle checks before proposing any production replacement.
This follows the measured wake bottleneck and preserves the historical CPU
concern. Runtime scene attribution/coverage/window and quality gates remain
open for the overall performance objective.
