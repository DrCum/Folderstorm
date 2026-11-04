# Avatar and world initialization batch

Base: main `e462dd1d1f`. Independent of the other upstream batches.

Plan: initialize native avatar typing/animesh flags and OpenSim world limits,
and call the virtual icon-cell setter without an invalid text-cell downcast.
Do not import animation/scheduler changes from held #161 or alter pose authority.

Firestorm sources: `6d88ec304f493180d25e36b90872d388e3b4d20b`,
`08a4fb5c44aa6ef90e0cee16cd0e24bfdf32f82d`,
`524b32142f73e9af4995e8f8b51b6d329da3d5b3`, and
`168ac4d5903f2e3097aef7c85ae4635ce9e6dfe3`.
The typing patch preserves Folderstorm's constructor initialization of
`mVisibleChat`; only the missing typing member gets an in-class default.
Main ancestry, patch IDs and prior #32/#66 provenance show these are distinct.

Trial merges with held #160/#161/#162 and the preceding safety/UI batches are
clean. #161 overlaps llvoavatar.cpp/.h, but its scheduler/render optimizations
do not replace these constructor/member defaults. A clean merge is not runtime
validation of that experimental branch.

Validation: real-header Linux syntax checks for llworld.cpp and llvoavatar.cpp;
23 existing pose native-boundary source-contract checks pass. The poser file
requires the normal viewer PCH in the standalone syntax harness. No new model
tests are added for simple initialization and removal of the two invalid casts.

Native acceptance: fresh login before region-limit capabilities arrive;
typing/name-tag transitions, avatar/animesh impostor exclusion, and poser avatar/
joint icon refresh with local and shared poses. Full viewer/Windows/GL execution
remains unrun; existing native consent and MCP ownership rules are preserved.
