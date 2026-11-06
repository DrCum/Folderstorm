# Complete controller cost evidence

- `matrix-plan.json`: fixed72 timing/12 allocation cases, written before collection.
- `raw/`:169 normalized-LF files, including the original unrun build-busy precheck.
- `raw-manifest.json`: raw normalized-LF SHA256 values.
- `summary.json`:84 validated cases, source/program identities, counts and costs.
- `progress.json`: selected current native programs and source/dependency identities.
- `progress-before14s1.json`: initial proof before the result-lifetime correction;
  none of its programs produced an accepted observation.
- `parent-build-receipt120.json`: exact55f66ec851 full build7.2.5.82290 receipt,
  verified installer hash,39 focused checks and packaged default-OFF policy.
- `supplemental-oracle.json`: same64-query-frame direct comparisons and identities.

Recheck retained data from the repository, writing output into a scratch folder:

```
python indra/llcharacter/tests/analyze_event_sampling_cost.py --raw doc/plans/results/event-sampling14s/raw --plan doc/plans/results/event-sampling14s/matrix-plan.json --provenance doc/plans/results/event-sampling14s/progress.json --output <scratch-output.json>
```

There is no timing pass threshold. Synthetic gaps and the generated four-asset
cohort do not establish real deadlines or FPS. CPU windows include explicit busy
gaps, bookkeeping and final drain; per-job cycles are attribution within them.
Allocation splits use the first owner boundary, without a separate drained warm-up.
Read the14s results document for the rejection of first-consumption whole-batch
integration and the next bounded-capture experiment.
