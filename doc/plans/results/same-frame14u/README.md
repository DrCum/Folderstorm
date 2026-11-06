# Same-frame readiness evidence

`matrix-plan.json` predates all accepted observations. The 16 timing cases use
1,024 frames, and eight separate allocation cases use 64. `raw/` preserves all
48 normalized-LF observation files; `summary.json` includes exact frame/admission/
readiness/useful-waste accounting and p50/p95/p99/max costs. `provenance.json`
binds the selected native programs, source, dependencies and matrix identities.

The 52 deterministic delivery controls run only in correctness mode; timed
requests have no gate, callback hook, forced readiness or artificial gap. Complete
owner/worker cycle windows include bookkeeping and final drain/cleanup. Per-job
cycles are attribution inside those windows and must not be added again.

Recheck from the repository, writing a new result into a scratch location:

```
python indra/llcharacter/tests/analyze_same_frame_readiness.py --raw doc/plans/results/same-frame14u/raw --plan doc/plans/results/same-frame14u/matrix-plan.json --provenance doc/plans/results/same-frame14u/provenance.json --output <scratch-output.json>
```

Initial native failures and diagnostic programs remain under their original
names. `progress-before-transition-controls.json` preserves that earlier partial
proof, including the unavailable initial case/source hash. No observation from
those programs was accepted as performance data. Current results reject this
whole-batch design for viewer integration; read the 14u results and next 14v plan.
Parent receipts verify separately retained PR121/122 builds, not live acceptance.
