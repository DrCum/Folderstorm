# Memory safety batch

Base: `main` at `e462dd1d1f7444b3922989ec30677814bd63c3cd`. Independent
of the theme, performance and MCP PRs; no other upstream batch is required.

Plan: import only the Windows thread-name argument count, two texture-animation
removal rebuilds, and malformed Collada attribute-array guards. Retain the
existing native rendering, upload permissions and Folderstorm callbacks.

Sources (original authors and provenance retained in individual commits):

- SL `fa8967bb61cfd7787a0a01c0bc3dac11903b7282`: adapt its count fix to
  Folderstorm's older `set_thread_name(DWORD, const char*)` signature.
- SL `d67326e6208cb6ac65561d44ea70d3c395e40474`: unchanged rebuild behavior.
- SL `2200e164f48b03a3de2c5c4fc50df963aadd1855`: retain Folderstorm's earlier
  required-position validation as well as the new optional-array guards.

All three are absent by ancestry and stable patch ID in the 225 post-fork main
commits. PR #32's callback/skinning/object-parameter fixes and #66's inventory
parser isolation remain present and are distinct. No workflow is added.

Validation: three compiled production-snippet checks pass for the Windows ABI
argument count, malformed/valid DOM arrays, and both rebuild paths with/without
a spatial group. Linux syntax checks pass for all three production files using
real viewer headers. These do not exercise Windows SEH, real Collada imports,
rendering/GL, or a full viewer build.

Native acceptance: build Windows with warnings-as-errors; exercise worker thread
names under the debugger, texture animation stopping/restarting under object
updates, and valid/malformed Collada imports. Verify no stale draw batches/crash
when animated textures stop. Upload authority and charging flows are unchanged.
