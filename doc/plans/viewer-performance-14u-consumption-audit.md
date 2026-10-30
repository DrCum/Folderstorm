# 14u consumption implementation and control history

Started from published PR 122 exact
b7c60446cfdf3f67565690a0b1ffaa3c7f8048be on the separate branch
codex/perf-same-frame-readiness. PR 121 remains immutable at81c90614cd;
PR 122 remains immutable for its separate build.

The existing fixture marked a batch consumed at its first callback, then
cancelled an unready ticket. The new private later modes instead leave a
Queued/Running ticket unconsumed. Each following real callback makes one
nonblocking take observation. Ready data transfers exclusively once; Failed/
Expired work is terminal. Every early miss still invokes the original direct
motion update immediately. Frame finish cancels any still-unconsumed ticket,
using the existing bounded generation/acknowledgement rules.

New private modes pair cached event first-consumption with cached event/polling
later-consumption. Existing direct/owned/released controls remain. Width and
algorithm are selected by the real production constructor; the job still owns
only its immutable requests and observation data. Callback entry/controller/ID/
time remain owner-only, and every application uses independent production
current-context validation. Retention stays bounded within the fixed Scene.

Metrics now count readiness checks, pending misses, failed batches, first ready
callback and sampled-but-unused joints. Complete owner/worker windows, current
resource bounds and final cleanup remain. No production source, settings,
readiness policy or animation dispatch changes.

The initial expanded native check forced readiness before normal correctness
callbacks. It can validate ready application, stale data, busy/closed fallback
and cleanup, but could not prove the new early-miss/later-ready transition.
That evidence alone did not authorize timing or publication as completed.

The first native run stopped at the generic unexercised-ready/stale assertion;
its executable/response/log remain preserved as event-sampling14u-initial.
A diagnostic-only build then passed110,001 checks without reproducing the
failure. Do not claim that passing rerun explains the original failure.
Source review found an independent control assumption: tryPost can legitimately
reject transient lock contention, while the oracle demands an admitted result.
Correctness runs now use the real blocking post adapter only to establish
admission in their controlled queue. Performance runs keep the original tryPost
adapter and rejection accounting. Busy slot/closed queue controls remain. Rich
case/mode/frame/admission diagnostics are kept for any further failure. Exact
initial failure attribution remains uncertain; deterministic transition controls
and this corrected admission oracle are still required before measurement.
The corrected-admission build passed110,001 basic controller checks. That was
partial validation; deterministic transitions and performance observations were
still missing at that point.
Source confirmation: workqueue.cpp's tryPost delegates to tryPush; in
llthreadsafequeue.h, tryPush delegates to tryLock, whose failed try_lock returns
false. The normal post path uses pushIfOpen. This proves the admission assumption
was invalid, without conclusively attributing the original unlabelled failure.

The next implemented step added deterministic private delivery controls: owned gate data only, release
at a specified callback, with waits restricted to correctness controls. Verify
early direct callbacks followed by later applications, first-consumption control
discarding late work, no readiness, failure/rejection, staleness between callback
points and final cancellation/cleanup. Character/ID/time and byte-exact pose/hand
oracles must match. Reuse the broader context/executor suites as distinct evidence.
Only after those pass freeze the small paired/reversed matrix and measure with
full-host/private-builder idle. No synthetic gap or live viewer deadline is a
substitute for this transition proof. General cache/deprecated/reentrant lifetime
and Q4/Q7/world acceptance are still separate gates.

## Completed control and cost evidence

Implementation6c5012aadc adds52 deterministic transition cases covering callback
2/middle/last readiness, first-only discard, no readiness, rejection, unsupported
numeric-context failure, between-callback frame/skeleton/asset/permission changes
and final cancellation/cleanup. Gates own only promise/future data; callbacks
and Scene mutations remain on the owner. Both native variants pass123,711 checks
and normal/Tracy syntax passes. Startup correctness controls now use post, while
all timed requests retain nonblocking tryPost with no delivery hook/gate/wait.
The exact initial failure is still not retrospectively attributed.

The frozen24-case matrix is now complete and independently revalidated from
retained data. Read viewer-performance-14u-readiness-results.md and provenance/
summary.json in results/same-frame14u. No whole-batch viewer integration is
supported by these costs. The next14v plan is written before implementation.
