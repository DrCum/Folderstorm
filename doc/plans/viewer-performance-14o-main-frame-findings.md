# Main-frame audit: initial source findings

Partial audit under plan 14o, based on published PR116 head
`836f1eed3850dcb55e57ba8e9138a2a21917e7fe`. This records source ordering and
ownership constraints, not runtime hotspot attribution or a dispatch proposal.

## Frame and avatar path

| Phase | Source at the frozen head | Observation and consequence |
| --- | --- | --- |
| Frame entry and events | [doFrame](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llappviewer.cpp#L1611) | Session-worker ticking, event bridge and main-loop/input callbacks precede simulation. These are mutation/callback boundaries that any early snapshot must account for. |
| Worker replies | [idle reply queue](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llappviewer.cpp#L5994) | `gMainloopWork.runFor` services replies with a configured time budget before later object updates. Posting a reply does not itself establish a same-frame animation consumption deadline. |
| Agent and object update | [object update and cleanup](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llappviewer.cpp#L6250) | Gestures/agent positioning precede `gObjectList.update`; GLTF scene update follows it, then dead objects/drawables are cleaned. The code explicitly allows idle updates to kill objects before cleanup. |
| Active-object traversal | [object-list update](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llviewerobjectlist.cpp#L1013) | A reusable list of raw pointers is copied from active-object handles because idle callbacks can change the list. Objects are updated serially; flexible and texture animation class updates follow in the non-freeze path. The list copy alone is not a worker lifetime contract or an avatar preparation barrier. |
| Avatar update | [avatar idle tail](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llvoavatar.cpp#L3249) | Pending attachment work and saved root position precede `updateCharacter`. Voice, appearance, wind/water and name-tag work follow. Parallelizing this entire callback would cross scene/attachment/UI state. |
| Motion deadline | [updateCharacter](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llvoavatar.cpp#L5704) | Shadow preparation occurs per built avatar, before update admission, appearance/sit/root work and motion callbacks. Hidden, preview/force and normal paths differ. Root/head/footstep work also surrounds motion evaluation; the existing observer covers this local opportunity, not a new global avatar phase. |
| Rendering | [display call](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llappviewer.cpp#L1818) | Display follows idle and is gated by exit/headless/window state. Session-camera frame handling wraps it, and render-dependent side work has a further session render gate. Hidden/session maintenance needs its own path audit. |

The sampled path does not establish a global collect-all-avatars phase followed
by a motion-consumption barrier. Adding one would require explicit lifetime,
mutation, callback-order and deadline analysis. Existing owned curve inputs
remain useful, but neither this source ordering nor synthetic gaps proves a
beneficial live worker window. Do not send raw idle-list objects to a worker.

## Existing worker budget: first observations

[Thread startup](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llappviewer.cpp#L2663)
requests a three-thread General pool and starts separate image decode, texture
cache/fetch and LFS infrastructure. Image-decode configuration derives from
reported hardware concurrency, with emulation/explicit setting overrides and
`clamp(cores - 4, 2, 8)`. If 12 hardware threads are reported and those overrides
are disabled, that formula selects eight decode threads. This is a conditional
source calculation, not a reading of the user's settings or active CPU load.
Pool constructor behavior, mesh/network workers, per-process multiplication and
idle/busy scheduling remain to be audited before any additional worker budget.

## Remaining work

- Finish startup, hidden/preview/local/remote and session-role paths, and confirm
  the actual consumer/deadline and current-state checks for each numerical seam.
- Review visibility/detail, geometry, procedural/flexible/particle and terrain
  candidates with their existing workers/caches/invalidation. No candidate is
  ranked as a runtime hotspot from this first source pass.
- Reconcile measured complete sampling costs with preparation/queue/wake costs
  and real Q4/Q7 evidence when available. Do not infer an FPS improvement or
  change rates/quality to create one.
- Keep PR116 stable for its separate build/review. PR115 was retained as
  `7.2.5.82269` at exact `2df5f5bdaf`, with installer hash and 33 passed focused
  checks verified. Rendered/lifecycle/crowd acceptance remains pending.
