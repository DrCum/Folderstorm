# Review required: progressive inventory-cache integration

Source: SL `c8c22bbfed91ff1ddb70a3b30ec44c234b47fd20`.
Branch `risk/20261101-progressive-inventory`, base main `e462dd1d1f`.
**Evaluation only: candidate production code is not applied.** The exact source
is [full-upstream.patch](full-upstream.patch); dry-run diagnostics are retained
in [applicability.json](applicability.json). No source or cache files are modified
by that check.

Existing coverage is partial, not a duplicate: #64/#66 already parse disk caches
on a worker and fence login generations. This candidate additionally loads
priority categories, indexes items/categories in maps, defers remaining folders
through background fetch and raises cache schema version 5 to 6. It changes
seven files, including a new shared LLCacheLoadResult header and CMake entry.
The patch fails dry-run checks against current AIS, model and background-fetch code.

Risks and conflicts:

- Main has a nested LLCacheLoadResult and ownership/generation/temporary-file
  handling that must be reconciled with the new shared map-based structure.
- Retain #66's getActualType/getActualName parser isolation; do not replace
  complete functions with upstream bodies and lose that independent fix.
- Deferred cache entries must not override newer simulator/AIS updates, revive
  deleted entries, or invoke old-account callbacks after logout/grid switching.
- Folderstorm's inventory/source snapshots, pending uploads, task Contents and
  MCP source/permission checks rely on truthful readiness and revision evidence.
  Deferred completeness must not make unavailable entries look absent or editable.
- Library visibility, favorites/outfit sorting and per-character cache ownership
  need acceptance. Held #160/#161/#162 remain separate; source integration is not
  established merely because this reporting-only branch merges cleanly.

Proposed staged plan, pending Marty review:

1. Separate map/result/schema changes from deferred application. Keep source-bound
   generation and parser isolation tests passing before altering startup behavior.
2. Add deterministic native adapter tests for priority/link chains, missing/cyclic
   parents, inventory mutations during deferral, callback ownership and multiple
   character/account/grid lifetimes. Expose pending versus authoritative-empty.
3. Implement deferred main-thread batches with bounded work and preserved native
   AIS/cache precedence. Test cancellation and restart with schema 5/6 fixtures.
4. Native acceptance on large real inventories, Library/outfits/favorites, uploads,
   source reads/edits and rapid host character logout/relogin. Measure startup and
   main-thread stalls before considering adoption.

Validation here: baseline #66 parser/folder-upload adapter checks are run only
to establish safeguards that must survive future integration; they do not test
the unapplied candidate. No candidate compile, full viewer, native Windows,
simulator, cache migration or performance test ran.

Unresolved decision: approve this larger follow-up and its native validation
budget, or retain the current background parser. Recommend retaining current
main until the above invariants can be demonstrated. No automatic cache/schema
migration, updater change, installer or release is included in this branch.
