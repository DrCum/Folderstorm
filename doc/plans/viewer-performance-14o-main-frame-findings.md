# Main-frame audit: source findings and next decision

Source-level audit under plan 14o, based on published PR116 head
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
The pool constructor calls `getConfiguredWidth`, which consults the named entry
in `ThreadPoolSizes`; a configured integer overrides the requested width. Thus
three is the General call site's default, not a verified runtime count. On
Windows, [ThreadPoolBase::run](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/llcommon/threadpool.cpp#L142)
installs `sleepy_robin`: `suspend_until` calls `Sleep(1)` and `notify` does
nothing. Other platforms do not install that helper in this entry point.

Follow-up source review in 14p2 also reads packaged settings: `ThreadPoolSizes`
contains `General: 1`. With ordinary initialized settings lookup this overrides
the constructor's three. The running user's override remains unknown. Packaged
ImageDecode 9 is replaced by initThreads' computed entry before construction.
See `viewer-performance-14p2-general-boundaries.md` for actual General consumers;
the startup comment mentioning LLPerfStats is not a dispatch proof. Current
StatsRecorder::send aggregates synchronously. The private matrix covers widths
1/3/8 without changing the viewer's selection.

[Image decode](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/llimage/llimageworker.cpp#L74)
already owns requests with retained image/responder handles and runs them on
the ImageDecode pool. The [mesh repository](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llmeshrepository.cpp#L118)
has repo/decomposition/HTTP/upload roles; asset unpacking is staged off the main
thread, then loaded-mesh notifications return to viewer objects. Texture cache,
fetch and LFS also have their existing infrastructure. Do not duplicate those
queues to increase a nominal thread count.

[ImageGL](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/llrender/llimagegl.cpp#L2630)
conditionally creates a shared graphics context and makes it current on its
worker. This is an explicit context/cleanup contract, not permission to send
arbitrary drawable or render calls to General. Actual enabled pools, widths,
busy CPU time and the number of viewer processes remain runtime evidence gaps.

## Candidate decision table

This ranks the next investigation, not real-scene CPU hotspots.

| Candidate | Current owner/dependencies | Decision |
| --- | --- | --- |
| Windows pool wait | Queue delivery and Boost.Fiber scheduling; no scene data needed for a private-pool probe. | First independent experiment. Existing actual-pool measurements show a large wake penalty; sustained idle CPU, timed work, fairness and shutdown need stronger evidence before replacing the historical workaround. |
| Owned animation sampling | Owned curves/results exist, but current controller input/lifetime validation and same-frame consumption remain owner work. Appearance, sit/root and hidden/preview paths can invalidate preparation. | Keep runtime dispatch disabled pending Q7 coverage/window and visual acceptance. A faster queue alone does not establish a useful live deadline. |
| Visibility/detail admission | [computeNeedsUpdate](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llvoavatar.cpp#L5656), update period, impostor/appearance state, global frame/LOD budgets and root mesh LOD changes. | Keep rates/quality unchanged. Scalar subexpressions are not demonstrated hotspots; split value calculations only if real attribution supports their complete cost. |
| Geometry preparation | [updateGeom](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/pipeline.cpp#L3001) owns dirty build queues; [volume rebuild](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llvovolume.cpp#L5927) changes groups/draw maps/face data and buffer budgets. Existing dirty/cached paths avoid complete rebuilds. | Whole methods are unsuitable worker jobs. A measured CPU buffer kernel would need owned inputs/results, versions and owner-side publication separate from drawable/GL mutation. No measured new kernel selected here. |
| Procedural avatar physics | [onUpdate](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llphysicsmotion.cpp#L486) reads current joints, params and pixel/role state; substeps write drivers and select visual updates. Parameter pointers already have a cache. | Profile before extraction. Preserve substep order, driven-param side effects, local/remote behavior and visible morphs. Do not add another unmeasured cache or remove visual calls based only on unchanged scalar values. |
| Flexible objects | [updateClass/doFlexibleUpdate](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llflexibleobject.cpp#L429) uses shared instance traversal, drawable/volume paths, section state and current region wind. | Requires a state/time/wind snapshot and ordered owner publication before dependent geometry. Current scene/callback methods are not an owned worker payload. Crowded-frame benefit is unmeasured. |
| Particles | [updateSimulation](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llviewerpartsim.cpp#L717) uses global RNG, source/group mutation, quotas, muted/complexity flags and drawable rebuilds; dead entries are removed during traversal. | Preserve stochastic order, admission and lifetimes. Parallelizing the loop directly would change shared behavior. Extract a measured particle kernel only with owned group state and explicit allocation/publication ordering. |
| Terrain decoding/geometry | [unpackData](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llvlmanager.cpp#L105) is called in idle; [decompressDCTPatch](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llsurface.cpp#L868) writes patch heights and neighbor edges. [IDCT](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/llmessage/patch_idct.cpp#L33) uses shared header/table/size state. | A future decode job needs a private decompressor context, owned packet/output and region/neighbor versions. Main-thread height consumers and drawable publication set deadlines. Arrival/travel relevance differs from warmed crowded FPS; defer without attribution. |

## Role and deadline checks

`LLVOAvatar::allowsOwnedKeyframeSampling` explicitly excludes self, UI and
control avatars. Motion factory permission is a separate gate. Animesh/control
and preview processing therefore cannot inherit ordinary remote job authority.
Unbuilt avatars return before preparation; hidden paths use minimal motion
updates, while previews can force updates. Normal-root work resolves current
ground/region state, can match control-volume transforms, and updates root
position/rotation before motions. These paths require distinct validation;
uniformly dispatching an avatar loop would discard their contracts.

[Hidden/standby display](https://github.com/DrCum/Folderstorm/blob/836f1eed3850dcb55e57ba8e9138a2a21917e7fe/indra/newview/llviewerdisplay.cpp#L532)
still performs maintenance, teleport progress and texture work under its gates.
Session rendering policy does not imply all background CPU or queues are idle.
The main reply time budget also does not preempt a long callback; it is not a
hard animation deadline. Per-job numeric context and independently current owner
validation from 14e/14f remain necessary even with a faster worker wake.

## Existing measurements and next decision

Plan 12's actual private one-worker tests compare current Windows polling with
the bundled stock `round_robin`. Under an accepted 1 ms timer request, retained
medians were about 1.94/1.99 ms versus 12/13 us. Those two-second idle samples
were quantized and cannot dismiss the 2022 busy-wait rationale. Plan 13's
complete-cost tests show immediate consumption largely misses the current
pool's deadline; synthetic overlap can help larger batches but proves no real
window. The later scope/admission work still has no real crowd capture.

The next useful independent step is [plan 14p](viewer-performance-14p-worker-wait-acceptance.md):
test the already bundled stock scheduler for longer idle CPU, timed work,
notification races, coroutine progress, load/drain and shutdown. It already has
a measured latency advantage in the limited probe. Resolve the historical CPU
and correctness concerns before adding a new Win32 wait primitive or proposing
a production scheduler change. This experiment keeps the viewer unchanged.

## Open runtime acceptance

## Remaining work

- Record configured pool widths, busy/idle load and process count on the target;
  source defaults and hardware concurrency do not establish spare CPU capacity.
- Reconcile measured complete sampling costs with preparation/queue/wake costs
  and real Q4/Q7 evidence when available. Do not infer an FPS improvement or
  change rates/quality to create one.
- Keep PR116 stable for its separate build/review. PR115 was retained as
  `7.2.5.82269` at exact `2df5f5bdaf`, with installer hash and 33 passed focused
  checks verified. Rendered/lifecycle/crowd acceptance remains pending.
