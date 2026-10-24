# Private bounded owner capture evidence

The fixed matrix contains 12 timing cases of 1,024 frames and six separate
allocation cases of 64 frames. `raw/` preserves all 36 normalized-LF observation
files. `summary.json` contains complete validated costs and cleanup counts;
`provenance.json` binds exact native programs to source/dependency identities.
The manifest hashes retained files without relying on checkout line endings.

From the repository, write a fresh verification result to a scratch location:

```
python indra/llcharacter/tests/analyze_owner_capture_cost.py --raw doc/plans/results/owner-capture14t/raw --plan doc/plans/results/owner-capture14t/matrix-plan.json --provenance doc/plans/results/owner-capture14t/provenance.json --output <scratch-output.json>
```

All modes are serial in this matrix. Per-kernel cycle counters are attribution
inside the complete owner window, not extra worker CPU. Bounds and final cleanup
are included in that window. The fixed cohort holds at most one binding vector
per character, at most 16,384 bytes, with zero binding bytes after cleanup.
No general viewer cache/lifetime policy, actual deadline or FPS gain is proved.
Read the 14t results document and separate 14u plan before proposing integration.
