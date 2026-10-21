# MCP navigation

Status: implemented above building PR #76; native Windows acceptance pending.

## Tools

- `navigation_status`: exact login, current position/flying, assistant-owned movement and last outcome; no native session credentials.
- `navigation_move_to`: walk or fly to a finite nearby global destination, within the current region, with explicit stop distance and timeout (at most 30 seconds, 64 meters). Native autopilot provides collision handling; no reachability guarantee.
- `navigation_flying_set`: explicit takeoff/landing using native flight eligibility; no keyboard injection.
- `navigation_teleport`: one explicit global-coordinate teleport via native APIs; report submitted, never replay uncertain sends.
- `navigation_stop`: stop only this service's current movement; never stop a newer native pilot/follow operation.

## Ownership and cancellation

New Ask-capable `navigation_move` and `navigation_teleport`; opt-in World read for location/status. Exact-token Stop is cleanup under World read, without a fresh movement approval; its native source/ownership/active checks remain required. Exact viewer_session and versioned navigation capability. Managed navigation requires the active ready character. Freeze identity, region, start position, movement options and destination before approval; refuse stale approvals. Respect RLVa stand/fly/teleport and native flight/teleport restrictions, sitting, existing native autopilot/follow/teleport and disconnects.

Retain the execution guard through native asynchronous work. Recheck authority at each delayed send and while assistant movement runs. Use a unique behavior token so access pause/revocation, timeout, account/region change or loss of active ownership cancels only the owned autopilot. Manual cancellation wins; never restart it. Clear only assistant-owned movement controls. Return an operation ID and pollable state rather than claiming arrival at submission. Teleport cancellation after simulator submission is best effort and cannot undo a completed teleport.

## Validation and acceptance

Focused portable ownership/state tests, finite/range and source/revision checks, source contracts for delayed teleport/native transport guarding, and affected MCP schema/forwarding tests. Review both PRs with one senior Sol 6.1 Extra High reviewer and address confirmed findings. No broad builds, packaging or unrelated tests.

Windows: Ask refusal/approval/revocation, native manual movement and follow ownership, walking/flying obstacles/timeout, active-character switch, login/region changes, teleport delay/cancel, destination boundaries, RLVa restrictions and foreign/stale viewer sessions. Publish independently; native acceptance remains explicitly pending.

## Implementation and validation

Five typed tools use `navigation_tools: 1`, exact sessions and compound World-read/movement authority. Movement is rechecked both during idle maintenance and before native autopilot writes control flags. Flight changes/manual replacement win. Native teleport requests retain captured authority across maturity callbacks, reject a second submission, skip the extra bridge teleport path, and expose a guarded cancel helper matching the original execution. Only teleport operations can retain input authority through their own progress display; all other ownership, transition and Preferences gates remain intact.

Passed focused C++ ownership/bounds tests, native source contracts including a compiled extraction of the actual delayed request submit gate, host integration contracts and affected MCP tools/schema/policy tests. XML and diff checks pass. Native viewer compilation remains blocked by missing Boost headers in this Linux workspace; matching Windows viewer/sidecar and live navigation acceptance remain pending. No broad build or packaging was run.

## Review correction: status polling and Stop capacity

Status reads no longer allocate a 120-second mutation preparation ticket. Their
frozen source and material live only in the existing bridge execution guard;
validation still checks the exact account/login/grid/worker source, readiness
and native restrictions. Dispatch retains the authenticated guard, exact login
epoch and explicit World-read permission checks before returning status. Mutation
records and live async guards retain their existing bounded lifetime and checks.

A regression compiles the actual native preparation function with the production
ticket cache: 63 idle status polls, movement start and Stop one second later now
succeed. Another 1,024 polls leave the live mutation record intact. The same
harness rejects changed account/login/grid/worker sources, non-ready or restricted
reads, and changed read material. Native source contracts verify guarded read
routing precedes mutation-cache lookup. Windows runtime acceptance remains pending.

## Windows listener compiler compatibility

Navigation replies now use the native Response `setResponse()` API. Reviewed
its declaration and destructor: the original request still supplies the reply
pump and request ID, so status, Stop, flight and movement replies keep their
existing structure and guarded delivery. The source exactly matches the user's
retained compiler patch for successful Windows build 7.2.5.82173. The six focused
navigation source/extracted checks pass, including polling capacity and delayed
teleport authority. This does not establish live navigation acceptance.
