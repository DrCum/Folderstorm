# November 1 upstream integration ledger

Bounded input: [recommendations.json](recommendations.json), completed by GitHub
Manager. Every one of its 41 source IDs is tracked in [ledger.json](ledger.json).
This branch contains reporting only. Implementation batches are independent
main-based draft PRs unless an entry explicitly declares a prerequisite.
Nothing is authorized for merging or release.

Review base: `e462dd1d1f7444b3922989ec30677814bd63c3cd`.
Firestorm: `6d88ec304f493180d25e36b90872d388e3b4d20b`.
SL: `373addb1cbdb6ae505c8e797c33729cb1ad75dba`.

Completed: [#163](https://github.com/DrCum/Folderstorm/pull/163), native memory
safety (3 sources). Next: UI-lifetime fixes, then OpenSim/cache correctness.
All other candidates remain under applicability/dependency review, not omitted.

#32 (`9e9a8a5cbb`) and #66 (`6eef8a47ae`) are ancestors of current main. The
225 post-fork non-merge commits were checked by stable patch ID; none of the 41
candidate patch IDs match exactly. Content-equivalence and newer-API exclusions
are checked individually rather than interpreting that as proof all are missing.

Held #160 (theme), #161 (performance) and #162 (MCP) retain their own branches,
status and unmerged histories. No experimental performance branch is imported.
Daily upstream review automation belongs to the separate coordination task.
No installer publication, paid viewer CI, CLA restoration, DM sharing or IME
changes are included.

Progress and unresolved decisions will be recorded here as batches finish.
Native Windows/full-viewer/runtime checks are not implied by focused Linux tests
or conflict-free trial merges.
