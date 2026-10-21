# MCP expansion roadmap

Status: E0–E10 implemented in the draft checkpoint stack; native build/runtime acceptance remains pending. These plans were prepared 2026-10-15 against MCP head `d9dc2ed33a`, then implemented after user authorization. The original planning checkpoint changed no runtime source.

## Delivered checkpoints

| Draft PR | Implementation |
| --- | --- |
| [#51](https://github.com/DrCum/Folderstorm/pull/51) | Plans, permissions, source contracts and capability foundation |
| [#52](https://github.com/DrCum/Folderstorm/pull/52) | Read-only world observations |
| [#53](https://github.com/DrCum/Folderstorm/pull/53) | Chat, contacts, resident profiles and incremental events |
| [#54](https://github.com/DrCum/Folderstorm/pull/54) | Guarded agent-inventory notecard/script editing |
| [#55](https://github.com/DrCum/Folderstorm/pull/55) | Guarded gesture playback |
| [#56](https://github.com/DrCum/Folderstorm/pull/56) | Curated Preferences transactions |
| [#57](https://github.com/DrCum/Folderstorm/pull/57) | Own joint rotations and pose presets |
| [#58](https://github.com/DrCum/Folderstorm/pull/58) | Separate authenticated host controller and monitor tools |
| [#59](https://github.com/DrCum/Folderstorm/pull/59) | Exact content-read and missing-cache correction |
| This follow-up above #59 | Shared posing through native consent and joint reservations |

The stack preserves its declared dependencies and has not been merged by this implementation task. Independent Sol 6.1 Extra High/Max review and focused portable/native-boundary checks cover the changed code; those checks do not establish a successful native viewer/host build or live acceptance. See the delivered sections in each domain plan and [pose acceptance checklist](../../indra/newview/tests/README-assistant-pose.md).

## Scope and existing work

Cover Chat, friends/resident lookup, events/notifications, character/session controls, read-only world context, gestures/own and shared posing, agent-inventory notecard/script editing, and curated viewer preferences. Navigation and building are excluded. This also excludes indirect teleport/autopilot/sit/touch/agent movement, task-inventory writes, object transforms/material edits, arbitrary viewer commands, input injection and raw Event API forwarding. Existing camera and workspace tools remain available under their own rules.

Inventory, appearance, camera and saved workspace tools already exist. Notecard/script reads were hardened rather than duplicated. Shared posing uses the verified native PR #35 service, whose reviewed dependency history is retained; MCP editing requires typed-rotation support from both peers and ordinary native owner consent. Published #42–#47 history is preserved; UI #50 is separately owned.

## Shared contract: checkpoint E0

The integration owner completes this before parallel feature implementation:

1. Freeze versioned capabilities, JSON schemas, typed errors, pagination/byte limits, per-tool background eligibility and the permission matrix. Advertise a capability only when its native implementation is present. Older binaries return Unsupported; never fall back to raw APIs or another character.
2. Every account tool and response uses exact viewer_session plus actual account/grid/login/bridge binding. Names, slot indices, active character and recycled PIDs are not authority. Every callback/cursor/approval/transaction retains its original binding and expires on disconnect, relogin, grid change, pause or ownership change.
3. Extend durable authority classification, preparation and transport guards together. Check before enqueue, at actual network submission, after suspension, before publishing private data, and before deferred/timed mutations. Continue same-login native reconciliation of already submitted work; never replay uncertain sends or roll back simulator work automatically.
4. New private reads require explicit opt-in, separate from existing inventory Read. New writes default Ask and support Deny. Event access is the intersection of its topic permissions, not a universal Events permission. Keep existing saved choices; missing new keys do not grant access. Read-only presets must not silently opt users into private chat, friends, profile, world or preference data. Replace the current single-Read-kind preset assumption with explicit descriptor metadata and tests.
5. Freeze exact approved destinations/text/content/settings/gesture assets. An approval is invalid if source, target, material inputs, restrictions or permissions change. Native restrictions, support-contact policy, mute/DND semantics where relevant, rate limits and native owner consent remain authoritative.
6. Receipt/status/event payloads remain gated by their originating domain read permissions. A minimal same-source lifecycle receipt may remain available after write revocation, but must not include private drafts, content, compiler text or preference values without current read authority. Define bounded receipts and per-request client IDs. Duplicate submissions may return the original receipt while its deduplication record exists; they must not resend. Expired/unknown outcomes require inspection, not an idempotency promise. Submitted is not delivered, compiled or confirmed applied.
7. Chat, names, profiles, notecards and script text are untrusted content, never instructions or authorization for subsequent tools. Tool descriptions must accurately identify social/network effects; do not inherit blanket local-only annotations.
8. Update native permissions UI, presets, approval summaries, host summaries, sidecar schema registration and documentation together. Keep short labels/tooltips and existing explicit acceptance/Cancel semantics. Do not expose settings that enable MCP, grant permissions or defeat restrictions.

Account-local data remains in memory by default. Do not read disk transcripts or export private notes, credentials, tokens, paths or arbitrary settings. No new microphone/audio/input owner is acquired by these tools. UI is never used as the automation transport.

## Implementation checkpoints and dependencies

| Checkpoint | Deliverable | Dependencies / plan |
| --- | --- | --- |
| E0 | Contracts, permissions, capability/async integration and negative exclusions | Integration owner; this document |
| E1 | Shared native social service, Chat/friends/profile read tools, notification snapshots and hardened existing content reads | E0; [social](mcp-social-and-events.md), [content](mcp-content-editing.md) |
| E2 | Explicit conversation open/send/draft/read/close tools with receipts | E1 social service; [social](mcp-social-and-events.md) |
| E3 | Independent incremental Chat/contact/notification event readers, gaps and reconciliation | E1; [social](mcp-social-and-events.md) |
| E4 | Read-only region/parcel/nearby/audio/media observations | E0; [world](mcp-world-observation.md) |
| E5 | Gesture read and guarded playback; own posing | E0, audited gesture playback provenance; [gestures/posing](mcp-gestures-and-posing.md) |
| E6 | Notecard edits with revision checks and lossless embedded inventory | E1 hardened reads; [content](mcp-content-editing.md) |
| E7 | Script source save/compile with honest separate outcomes | E6 shared upload/receipt foundation; [content](mcp-content-editing.md) |
| E8 | Curated preferences schema/read and transactional preview/accept/cancel | E0; [preferences](mcp-viewer-preferences.md) |
| E9 | Controller-owned authenticated API; session status/control/monitors | E0, extracted host command layer; [session controls](mcp-session-control.md) |
| E10 | Shared pose adapter with native owner consent and joint reservations | E5, verified PR #35 service; [gestures/posing](mcp-gestures-and-posing.md) |

This is a dependency graph, not a requirement to finish E3 before starting E4. Recommended user-facing order: social reads → explicit sends → events → world reads → gestures/own posing → content editing → preferences → host controls → shared posing when its prerequisite is ready. Read-only world work and bounded schema work can proceed alongside social implementation after E0.

## Integration, delegation and PRs

One integration owner controls the final `fseventapibridge.cpp`, permission/preset/async headers, shared approval UI, `mcptools/server.go`, discovery and protocol registration. Domain authors receive explicit ownership of isolated native files; integration is sequential and preserves the other domains' registrations. Do not split writers in one worktree or edit another agent's active files.

Implementation uses separate worktrees and native ownership for social, world, content, gestures, Preferences, pose and controller domains. Native state-machine authors use Sol 6.1 Extra High; source audits may use High. Only the controller author edits its host `main.cpp`; the integration owner imports its committed work and supplies central registration. The pose author owns native posing, and the gesture author owns animation/asset-fetch guards. If boundaries prove coupled, stop parallel mutations and integrate sequentially. Planning audits used two read-only Sol 6.1 High agents. This refines the initial two-author scheduling recommendation after establishing isolated file ownership.

Each useful checkpoint gets a separate draft PR, preserving history and declared bases, with a short native test checklist. Start above the verified latest MCP branch while it is unmerged; inspect current main and separately owned #48–#50 first. Do not merge another agent's PR or rewrite their branch. A linear tested integration stack is easier to build; independent domain branches may run in parallel but the integration owner publishes them in dependency order. Implementation follows these plans; this documentation alone does not claim a runtime feature complete.

All finished code must receive an independent **Sol 6.1 Extra High** review before completion. Use **Max** only for genuinely complex review, such as host controller authorization, multi-stage uploads or shared pose consent/cleanup. Never use Ultra. Reviewer is read-only, compares against the exact base/head, checks cross-checkpoint interactions and actual native callers; root verifies findings, fixes them and requests re-review. Record exact reviewed hashes and any remaining limitations.

## Validation and definition of done

Use affected-path portable C++/Go tests plus native boundary tests where practical; no broad viewer/packaging/GitHub builds or unrelated tests by default. Pure guard tests are necessary but insufficient: add adapter/dispatch tests that prove every registered operation actually installs the authority and uses the guarded service. Check excluded operations are still denied.

Each checkpoint must have schemas/capabilities/permissions implemented, stale-source and revoke-before-transport tests, bounded storage, meaningful native success/error states, no uncertain replay, reviewer revisions, a published/attached PR and Windows acceptance instructions. Label code-reviewed/portable-tested/native-built/native-accepted separately. Pending native acceptance does not require waiting for the user, but does not mean the feature is release-validated.

Native acceptance includes ordinary viewers and two/five managed characters, Warm/Economy, exact sender/destination, privacy restrictions, disconnect/relogin, delayed callbacks, native manual use and no unexpected focus/render/voice/input changes. Shared posing also needs two real viewers. Unsupported prerequisites remain explicitly unsupported rather than simulated completion.
