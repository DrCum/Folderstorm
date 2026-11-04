# November 1 upstream integration report

All **41 researched candidates are accounted for**. Eight independent draft PRs
implement 31 sources. Four `risk/` drafts hold six sources or remaining portions.
Four sources were verified as already equivalent or inapplicable. Nothing was
merged, deployed, packaged or released.

Start with [the candidate table](candidates.md). [ledger.json](ledger.json)
contains every exact source ID, original subject/repository, ancestry/patch-ID
checks, branch/head, disposition, evidence and tests. The completed input is
preserved in [recommendations.json](recommendations.json).

## Ordinary implementation batches

Each PR is based independently on main
`e462dd1d1f7444b3922989ec30677814bd63c3cd`. None requires merging another
ordinary batch first. Dependencies between source commits remain inside their
own PR (notably the RLVa default/max split before its XYZ correction).

| Draft PR | Sources | Result |
| --- | --- | --- |
| [#163](https://github.com/DrCum/Folderstorm/pull/163) | 3 | Thread-name buffer count, texture-animation draw lifetime, malformed Collada arrays |
| [#165](https://github.com/DrCum/Folderstorm/pull/165) | 4 | IM floater handles, picker listener names, login callback teardown, signed tab enum |
| [#166](https://github.com/DrCum/Folderstorm/pull/166) | 4 | Avatar/world initialization and poser icon casts |
| [#167](https://github.com/DrCum/Folderstorm/pull/167) | 6 | Unicode/Hypergrid URLs, canonical aliases, bounded map requests, missing reconnect endpoint |
| [#168](https://github.com/DrCum/Folderstorm/pull/168) | 2 | Protected/failed/missing cache accounting, UTF-8 native paths and startup thresholds, adapted to Boost |
| [#169](https://github.com/DrCum/Folderstorm/pull/169) | 4 | Culled animation clocks, FXAA target viewport, opaque-water picking and remaining skinning guards |
| [#170](https://github.com/DrCum/Folderstorm/pull/170) | 4 | No-copy link references, live editor colors, queued presence, browser startup |
| [#171](https://github.com/DrCum/Folderstorm/pull/171) | 4 | Native RLVa Picks/local-teleport/touch restriction correctness |

Original authors and full source trailers are retained. Adaptations preserve
Folderstorm's native and MCP permissions, upload/source ownership, per-character
host behavior, existing viewport/background work and earlier parser safeguards.
No experimental performance implementation from #161 is imported.

## Risk review for Marty

Each risk PR also starts at the same main baseline and is **not a prerequisite**
for the ordinary batches. Keep these drafts isolated pending their decisions.

| Draft | Applied code | Decision / proposed disposition |
| --- | --- | --- |
| [#172](https://github.com/DrCum/Folderstorm/pull/172), `risk/20261101-theme-color-depth` | Exact two-commit native color-traversal fix | It rejects valid chains deeper than ten and asserts in debug. Decide whether to adopt that policy or use actual bounded cycle detection before #160. Recommend preserving valid deeper aliases if supported by theme authoring. |
| [#173](https://github.com/DrCum/Folderstorm/pull/173), `risk/20261101-http-shutdown` | Only the HTTPStats/service teardown order | Forced TerminateThread/pthread_cancel plus a short sleep is held. Recommend a separately validated cooperative worker-exit strategy; this is not a complete hung-worker fix. |
| [#174](https://github.com/DrCum/Folderstorm/pull/174), `risk/20261101-progressive-inventory` | None; exact source patch, diagnostics and staged plan | Schema 6, map-based cache results and deferred application need manual integration with #66 and native large-inventory/revision/lifecycle acceptance. Retain current main until those invariants pass. |
| [#175](https://github.com/DrCum/Folderstorm/pull/175), `risk/20261101-webrtc-capture` | None; both exact patches, diagnostics and staged plan | Stabilize device/shutdown ownership first, then decide SDK/interface and optional capture policy. Preserve host microphone grants; native thread, headset and binary acceptance required. |

Their reports name exact sources, evidence, conflicts, prerequisites, tests,
unresolved decisions and proposed disposition. Source patches are deliberately
archived without modifying their original whitespace. Progressive inventory
and WebRTC candidate compilation/runtime were **not performed** because those
patches remain unapplied.

## Deduplication and compatibility

Pinned reviewed tips: Firestorm `6d88ec304f493180d25e36b90872d388e3b4d20b`,
SL `373addb1cbdb6ae505c8e797c33729cb1ad75dba`.

#32 and #66 are main ancestors. All four #32 imported patch IDs match their
originals. The 225 post-fork main non-merge commits were checked by stable patch
ID; no input candidate exactly matches. Content inspection nevertheless found
the XML infinity fix already equivalent and three fixes targeting absent newer
APIs. Skin-info retention was also already equivalent, so #169 imports only its
remaining guards. See [exclusions.md](exclusions.md) and
[prior-imports.json](prior-imports.json).

Held #160 (`491d2bf4f0`), #161 (`91b50ab7db`) and #162 (`1cf4a72d04`)
remain unchanged and unmerged. Applicable per-batch trial merges are text-clean.
Known overlaps are #160/lldir.cpp and #161/avatar, motion and skinning files;
reports explain the distinct changes and remaining native acceptance. Textual
compatibility does not validate #161's experimental runtime behavior.

All eight ordinary branches plus #162 were applied to a temporary detached
working tree for combined checks. Their 60 changed files do not overlap. No
combined branch or merge commit was created or published. Risk implementations
were excluded from that ordinary snapshot. Separately, the three editor checks
also pass with #172's optional hardened getter. See
[combined-validation.json](combined-validation.json) and the observed
[pull-request snapshot](pull-requests.json).

## Validation and limits

On the combined ordinary tree plus #162:

- 24 new upstream Python checks passed (compiled production methods/snippets,
  real temporary files, XML and source-boundary checks).
- 6 existing navigation, 23 pose source-contract and 3 inventory parser/upload
  checks passed: **56 focused Python checks total**.
- 8 focused CTests passed, including native object/content source adapters and
  portable content/lifecycle helpers.
- 8 selected cross-component .cpp files passed real-header Linux syntax checks.
  Each implementation batch also records its own production syntax checks.
- Working-tree/diff checks passed. Exact archived upstream patches are excluded
  from whitespace lint because unified-diff context intentionally retains it.

The directory-picker standalone Linux syntax check is blocked by existing GTK
picker API/configuration gaps in this harness; its changed Windows listener
registration is source-reviewed and still requires native compilation.
The poser syntax check uses the normal viewer PCH.

No full viewer/host build, native Windows/MSVC link, GL/UI/display-scaling,
real simulator/voice device/worker, packaging or installer acceptance ran.
Fixtures and source contracts do not replace those checks. Batch reports carry
Windows/native checklists. No unrelated Go suite or paid viewer CI was run.

## Continuation

All candidate dispositions are complete; there is no pending implementation
silently left in the input ledger. Next actions are review and native acceptance
of the ordinary drafts, then Marty's four risk decisions. There is **no merge
authorization**. Do not build/release a combined installer from the risk drafts.

All implementation/risk worktrees are clean and their branches pushed. The
clearly named `/workspace/Folderstorm-upstream-validation` worktree retains the
intentional staged ordinary+#162 test snapshot; it has no published branch.
Earlier worktrees and their work in progress were preserved.

Daily morning monitoring remains with the separate coordination task. No
competing automation, old October 22 coordination, build/download/help/site,
credential/security configuration, DM-sharing or IME work was started. No paid
viewer workflow or CLA checker was restored.
