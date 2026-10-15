# Assistant pose validation

Portable validation covers `test_pose_policy.cpp`, `test_pose_xml.cpp`,
`test_pose_file.cpp`, `test_pose_shared.cpp`, the existing
`test_fssharedpose.cpp`, `test_fspose_native_contract.py`, and the Go
`TestPose*` tests in `tools/fs-mcp/internal/mcptools`.

These checks do not replace a viewer build, Windows file-handle acceptance, or
live multi-viewer acceptance. Those remain pending. The following runtime
checks must be completed on the supported native viewer before claiming native
acceptance:

1. Change an otherwise unanimated Bento joint, then undo its first change,
   reset, stop, revoke permission, pause, switch profile, and teleport. Its
   unchanged assistant rotation must restore; external/manual rotation changes
   must survive with `restoration_complete=false` when cleanup cannot restore.
2. Start native Poser while the private motion is fully masked. Close native
   Poser and confirm the private motion never resumes. Verify a local stop sends
   no simulator animation STOP packet for the random private motion UUID.
3. List/load an existing UTF-8 native preset and save a new ASCII-named preset.
   Try directory/file symlink or Windows
   junction/reparse substitution while opening/saving. The assistant must never
   follow the substitution or overwrite an existing file.
4. With two updated viewers and native sharing explicitly enabled, publish an
   already active native Poser. Decline its normal owner consent request and
   verify MCP Ask cannot create an editing grant. Explicitly accept the native
   request and verify typed editing becomes available.
5. Have two editors reserve the same and disjoint joints. Conflicting leases or
   stale revisions must fail; disjoint rotation edits must preserve position,
   scale, priority, enable, mirror/lock flags, and each editor's history scope.
6. Revoke the native grant, replace the native session/bridge URL, pause or
   switch source while acquire, renew or final edit packets are queued. No
   remaining source packet may POST after the corresponding guard fails.
   Toggle either native sharing or Use LSL Bridge Off/On before an idle tick,
   including a manually published owner with no local assistant work. Old
   grants/leases and queued source work must stay invalid; native Poser remains,
   and publishing/editing consent requires a new explicit native flow. Also
   round-trip a manually published owner A→B→A before idle while its remote
   editor stays in A: old owner grants/leases must remain invalid on return.
7. Duplicate a locked reply, lose or delay a final POST/acknowledgement, and
   deliver an acknowledgement late. There is one final sequence, no uncertain
   resend, and `owner_accepted` appears only for the exact owner acknowledgement.
8. Cancel an unsubmitted operation: only its own lease may be released or
   allowed to expire. Cancel after submission: report uncertainty, retain native
   manual work, and perform no global refresh/undo/stop.
9. Continue native owner and granted-peer edits after publication; they must
   preserve that publication and its grants. Initial Ask still freezes exact
   native pose bytes. End publishing or revoke its original source: only its
   exact publishing/consent session ends and the native Poser remains. An
   explicit native Share takeover/restart detaches old MCP ownership, so later
   cleanup cannot end the newer native publication. Remote synchronization is
   unconfirmed until the peer receives the stop or times out.

The typed protocol extends the existing native consent/reservation model. Both
native peers must advertise typed-rotation support. Ordinary native UI packet
and undo behavior remains on its existing path. Automatic consent requests,
notification acceptance, preference enabling, and private-motion publication are
not implemented by the assistant adapter.
