# MCP navigation

Status: planned; implement after building as a separate stacked PR.

## Tools

- `navigation_status`: exact login, current position/flying, assistant-owned movement and last outcome; no native session credentials.
- `navigation_move_to`: walk or fly to a finite nearby global destination, within the current region, with explicit stop distance and timeout (at most 30 seconds, 64 meters). Native autopilot provides collision handling; no reachability guarantee.
- `navigation_flying_set`: explicit takeoff/landing using native flight eligibility; no keyboard injection.
- `navigation_teleport`: one explicit global-coordinate teleport via native APIs; report submitted, never replay uncertain sends.
- `navigation_stop`: stop only this service's current movement; never stop a newer native pilot/follow operation.

## Ownership and cancellation

New Ask-capable `navigation_move` and `navigation_teleport`; opt-in World read for location/status. Exact viewer_session and versioned navigation capability. Managed navigation requires the active ready character. Freeze identity, region, start position, movement options and destination before approval; refuse stale approvals. Respect RLVa stand/fly/teleport and native flight/teleport restrictions, sitting, existing native autopilot/follow/teleport and disconnects.

Retain the execution guard through native asynchronous work. Recheck authority at each delayed send and while assistant movement runs. Use a unique behavior token so access pause/revocation, timeout, account/region change or loss of active ownership cancels only the owned autopilot. Manual cancellation wins; never restart it. Clear only assistant-owned movement controls. Return an operation ID and pollable state rather than claiming arrival at submission. Teleport cancellation after simulator submission is best effort and cannot undo a completed teleport.

## Validation and acceptance

Focused portable ownership/state tests, finite/range and source/revision checks, source contracts for delayed teleport/native transport guarding, and affected MCP schema/forwarding tests. Review both PRs with one senior Sol 6.1 Extra High reviewer and address confirmed findings. No broad builds, packaging or unrelated tests.

Windows: Ask refusal/approval/revocation, native manual movement and follow ownership, walking/flying obstacles/timeout, active-character switch, login/region changes, teleport delay/cancel, destination boundaries, RLVa restrictions and foreign/stale viewer sessions. Publish independently; native acceptance remains explicitly pending.
