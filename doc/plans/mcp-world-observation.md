# MCP read-only world observations

E4, depends on E0. Use a new exact read adapter; never allowlist all of LLAgent, which also contains excluded navigation/touch operations.

## Tools and truthfulness

- `world_location`: current region identity, permitted region name, local/global coordinates and orientation from native agent state. No camera focus, movement or selection changes.
- `world_parcel`: current agent parcel identity/name/description/owner and an explicit small read-only flag allowlist from LLViewerParcelMgr.getAgentParcel/LLParcel. Do not select a parcel or request edit powers to implement a read.
- `world_nearby_avatars`: bounded loaded-avatar observations, requested radius, actual distance where permitted, display/username when known. Exclude dead/control avatars and distinguish unresolved names.
- `world_nearby_objects`: loaded root-volume observations, with stable IDs and positions; report names/owners only when actually available. Any property fetch is a separate bounded read path with guarded results. Do not inspect task inventory, contents or scripts.
- `world_audio_sources`: observed sound history/current sources from gSoundHistory, including source/owner/asset IDs where permitted, observed position/timestamp, loop/trigger flags and stale/current state. Owner is not the emitter; old position is not current position. A matching sound asset or nearest object does not prove attribution.
- `world_media_sources`: distinguish parcel configured music/media, currently playing stream and loaded object/face media associations. Report availability/provenance. Redact URL credentials/query secrets; full sensitive URLs require a separately reviewed opt-in and are not in the initial field set. Do not navigate or control media, replay/stop sounds or change volume.

Require opt-in world observation permission, with name/location/audio-media privacy scopes defined explicitly in E0. Respect RlvActions.canShowNearbyAgents, shownames and showloc at request and result delivery. Do not call raw legacy reads that omit these checks. Restricted data must not leak through alternative IDs, distances, owner/name resolution, totals or events. Prefer a clear Restricted result when safe masking is not established.

Every result identifies actual account/login, observation time, region generation, loaded-state coverage, limit/truncated flag and unavailable reason. Nearby means what this viewer has loaded, not a simulator-wide census. Missing data is unknown, not proof of absence or silence. Region changes invalidate paginated snapshots and pending property callbacks.

No tool activates the character, starts background rendering, upgrades Economy, changes microphone/audio ownership or downloads media merely to obtain a result. Unsupported data stays unavailable. Background restrictions and observation staleness are visible in results.

## Implementation and checks

Build native read-only service files plus Go wrappers after E0. A Sol 6.1 High implementation agent may own these files/tests; root owns bridge registration and permissions. Existing LLAgent read implementations, FSRadar/LLAgentUI restriction checks, Sound Explorer and native media getters are references, not whole APIs to expose.

Test wrong login/region callbacks, name/location restrictions, loaded versus unavailable/empty results, attachments/control avatars, range/result bounds, deterministic pagination, unresolved names, sound source versus owner, stale sources and redacted media URLs. Negative tests prove teleport/autopilot/sit/stand/touch/object edits remain denied.

Windows acceptance: busy/sparse regions, region crossing, Warm/Economy, sound disabled/media not started, parcel stream versus object media, unloaded emitter and native RLV restrictions. Compare observable sources with native Sound Explorer; never claim complete acoustic attribution. Independent Sol 6.1 Extra High review is mandatory before publication completion.

Implemented checkpoint: FSWorld and six MCP read tools with world_read opt-in. Independent Sol6.1 ExtraHigh review at46faa23b55 found no unresolved actionable findings after cached-parcel/current-region/pending-teleport containment and unknown music-association fixes. Focused strict portable observation tests and Go world/schema tests pass. Native compilation/runtime remains pending. Sound history's generation tracks transitions observed during requests; an unobserved A→B→A excursion may retain intervening historical observations. Positions/playback/audibility are explicitly historical/unknown, and empty results never establish silence.
