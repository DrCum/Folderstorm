# Review required: color traversal policy

Branch: `risk/20261101-theme-color-depth`, based on main `e462dd1d1f`.
This implements only SL `432b726d6798c4053fc24fc314fbe2aefe421798` and
its follow-up `e7fc2fc32d64721b83f12279044e0a1efa5ca72c`. The second
commit depends on the first. Neither held #160 nor its theme authoring code is
imported. No other upstream batch is a prerequisite.

Reason for isolation: the final upstream implementation prevents recursive
stack overflow, but rejects *all* reference chains exceeding ten links, not
only cycles. It returns magenta and calls llassert(false), which has different
debug-build behavior. A legitimate eleven-link acyclic chain reproduces this
fallback under the production getter. That is a user-visible policy decision
for an application adding custom theme authoring.

Evidence: a compiled production-getter test passes direct/mutable aliases,
ten-link chains, eleven-link acyclic fallback, self-cycles and two-color cycles.
Assertions/logging are fixtures that record the branch; actual debug assertion
termination is not simulated. A per-file scan of shipped main colors.xml files
finds maximum depth three and no cycles; this is not proof about merged user
theme tables or imported packages. Linux syntax with real viewer headers passes.

Compatibility: trial merges with held #160/#161/#162 and editor-color #170 are
text-clean. The new getter retains live reference semantics. #160's custom
themes still require native validation against whichever depth policy is chosen.
There is no protocol, settings or package export change in this branch.

Unresolved decision for Marty: accept the upstream ten-link cap/debug assertion,
or replace the depth heuristic with bounded cycle detection that permits valid
deeper aliases. Prefer the latter if deep hand-authored XML must remain valid.
Keep this draft isolated until that policy and native Release/Debug acceptance
are settled. Before #160 merges, test custom imports, malformed cycles, live
editing, Apply/Cancel and exported packages with the selected native guard.
