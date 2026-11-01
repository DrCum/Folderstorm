# Native RLVa correctness

Base: main `e462dd1d1f`; no other upstream batch required.

Plan: preserve native restrictions while fixing four incorrect boundaries:

- `b92a4d01e5e441fea731d79c83a074270aff2be3`: enforce Show Location both
  in profile Picks controls and at the action/callback that captures location.
- `31b1866463ce3a09f1a735726f87ec7b053e54e2`: distinguish the 1.5m default
  restricted local-teleport radius from the 256m maximum/local classification.
- `c3b01a9ef7ee88ca61a6712a9caad9123680ff4d`: measure restricted local
  teleport distance in XYZ, including vertical moves. Depends on the preceding
  source within this PR, not on another Folderstorm PR.
- `c3d918d23a30be1561546e0e6dede8880b266eb8`: compare touch distance in
  agent space on both sides of a region border.

All sources are Firestorm. Native stand/sit restrictions, command-object
exceptions, explicit radius modifiers, ordinary paste/item restrictions and
MCP authority remain enforced. No credential/configuration changes are made.
MCP navigation still calls the same native teleport actions at preparation and
live dispatch; object tools retain edit/interact/show-location restrictions.

Three compiled production-method/snippet checks pass for radius boundaries,
vertical bypass, modifier caps/native exceptions, action-time location blocking,
button state and neighbor-region touch coordinates. All three changed .cpp
files pass real-header Linux syntax. Trial merges with #160/#161/#162 and the
rendering batch are clean. Native simulator/Windows/UI acceptance remains unrun.

Native acceptance: apply/remove showloc with Picks open and region changes;
tplocal without/with modifiers, vertical moves, sit/stand and native command
exceptions; selection across region borders under fartouch; MCP navigation and
object interactions continue refusing native-restricted actions. Rebuild and
test using a disposable restriction setup; no user session settings were changed.
