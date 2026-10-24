# Checkpoint 14p2: resume private worker wait acceptance

Written after implementing and native-testing 14p1, before continuing the worker
experiment. This refines the remaining work in 14p after its queue prerequisite.

1. Keep the scheduled queue fix independently published and immutable. Merge its
   exact published head into `codex/perf-worker-wait-probe`, retaining the frozen
   ccad probe, both sets of LL_TESTS targets and the original failure evidence.
   Recompile fresh workqueue.cpp in the scratch benchmark wrapper. Do not reuse
   the old cached scheduled queue instantiation or silently remove timed tests.
2. Recheck independent builder state and a full host native compiler/package
   process inventory. Only run measurements while both are idle, with before/
   after records for every case. Preserve interrupted or overlapping records;
   exclude them from controlled comparisons. Do not stop builds or alter their
   caches, source or monitoring to get an idle window.
3. Use private current/stock pools with widths 1/3/8. Keep default and scoped
   requested-1ms timer contexts separate. Measure 30-second empty and finite
   fiber-wait intervals, worker CPU ticks/raw cycles, exact active-work checksum
   and 64 post-to-callback observations per case. Pair and reverse variants.
   The 25%-of-core-per-worker resource cap terminates an unexpectedly busy
   private case; it is not an adoption threshold. Keep watchdogs and every raw
   admission/rejection. Other desktop load/power/timer policy is uncontrolled.
4. Review correctness together with idle CPU, finite waits, active throughput
   and wake distributions. Attribute no power saving to quantized zero CPU and
   no real viewer FPS improvement to a generated fixture. Compare actual bundled
   Boost 1.90.0 behavior; no new Windows wait implementation yet.
5. Retain raw tables, source/executable/dependency identities and before/after
   idle observations. Publish this test-only checkpoint as its own PR when the
   evidence supports a clear result. Its normal installer must retain production
   pool policies. If stock is unsuitable, retain the reason and write the next
   independent proposal. If suitable, plan an explicitly gated production trial
   with startup/loading/media/region/idle/shutdown and CPU/frame-time acceptance.

Q4/Q7 and the user's crowded-scene findings remain deferred. No animation worker
dispatch, frame delay, callback reordering, quality reduction or full viewer
policy change is authorized by the private probe results alone.
