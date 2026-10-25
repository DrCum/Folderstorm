# 14w owner-phase audit

Source audit begun from published PR124 exact
f1db2eab06b1db8c46535fb16979c18bfc147882 on
codex/perf-owner-phase-crossover. This records source findings and inferences,
not observed viewer timing or a completed live ownership design.

## Source path and boundaries

| Point in owner flow | Source anchor | Consequence for early capture |
| --- | --- | --- |
| Main idle updates frame time/count | `indra/newview/llappviewer.cpp:5985`; `indra/llcommon/llframetimer.h:46,95` | Controller timers use the frame timer's sampled time, rather than advancing wall time at each avatar. Inference: within that frame, elapsed work alone need not invalidate a predicted clock; resets, pause and activation still can. |
| Gestures/agent update, then object list | `indra/newview/llappviewer.cpp:6270,6281` | Capturing before agent/gesture work would precede changes relevant to animation. |
| Copy active objects, then call idleUpdate in existing order | `indra/newview/llviewerobjectlist.cpp:1082,1131,1142` | The list includes nonavatars, and idle updates may kill/change objects. A raw active-list copy does not supply a safe captured motion/character lifetime or a prepared avatar cohort. Cleanup follows in `llappviewer.cpp:6295`. |
| Avatar idle render/friend/dead exits, object update/fidget/pending attachments | `indra/newview/llvoavatar.cpp:3113,3233,3252,3258` | Not every listed avatar will reach a motion controller. Work before updateCharacter can affect its state. Preserve this order. |
| Determine visibility/cadence; appearance and sitting transitions; root update | `indra/newview/llvoavatar.cpp:5704,5746,5754,5806` | updateOverallAppearance can reset skeleton/animations; sitting transitions can change animation state. Capturing a whole cohort before these per-avatar mutations is advisory only. |
| Choose hidden/forced/normal update | `indra/newview/llvoavatar.cpp:5806,5810,5815`; `indra/llcharacter/llcharacter.cpp:188` | Hidden uses updateMotionsMinimal, not sampled keyframe callbacks. Normal/force may unpause immediately before controller entry. |
| Controller timing, purge and loading before motion traversal | `indra/llcharacter/llmotioncontroller.cpp:902,918,962,978` | purge can delete deprecated/inactive instances; loading can activate motions. Prediction must not promise identical callback membership/order or let owner pointers escape into jobs. |
| Actual motion callback | `indra/llcharacter/llmotioncontroller.cpp:233,643,745,777,801,824,829` | Current callback identity/time/mask and numeric context remain authoritative. Immediate direct fallback preserves this point. Activation also calls it at `1063`, outside the usual regular traversal. |

The frame clock implementation is `llframetimer.cpp:43`; an instance's elapsed
time is derived from static frame time and its own start state. The existing
predictSamplingClock at `llmotioncontroller.cpp:168` models pause/force/time-step
state, and predictSamplingTime at202 excludes inactive/stopped/late motions.
predictSamplingMask at212 is explicitly advisory for ordinary normal motions.
The audit does not add a new timer read or change controller arithmetic.

## Eligibility and special paths

| Path | Existing behavior and source | Pilot implication |
| --- | --- | --- |
| Ordinary remote avatar | `llvoavatar.h:262` permits owned sampling | Candidate class only, subject to actual callback eligibility and safe assets. |
| Self | Same permission rejects self; `llvoavatarself.cpp:991,1015` adds screen/HUD/idle work | Exclude. Its animation reliability and lifetime are not covered by the private ordinary cohort. |
| UI preview | Permission rejects UI; `lluiavatar.cpp:40` marks dummy/UI | Exclude, including force-update preview paths. |
| Puppet/animesh control | Permission rejects control avatars; `llcontrolavatar.cpp:362,382,409` has death and attached-parent cadence logic | Exclude. Do not count it as an ordinary eligible character. |
| Dummy character | LLCharacter defaults permission false at `llcharacter.h:302`; private fixture explicitly opts in | Synthetic permission is not evidence for arbitrary viewer dummy subclasses. |
| Hidden/not due | `llvoavatar.cpp:5746,5806`; `llmotioncontroller.cpp:998` minimal update | No ordinary row dispatch. Preserve stopped/loading cleanup. |
| Impostor/slow/muted | `llvoavatar.cpp:5218,5656` computes period and need dynamically | Count actual due eligible callbacks, not raw nearby-avatar totals; keep existing quality/cadence. |
| Paused | `llcharacter.cpp:197` can unpause when requests drop; controller entry tests pause/force | Final pause/force decision must match the actual owner path. |
| Loading/deprecated/reentrant | `llmotioncontroller.cpp:269,483,854,1063` | No generic retained live-pointer resolver has been proved. Instance/generation validation must accompany any later owner lookup and cleanup design. |

## Asset and lifetime gate

captureOwnedSamplingIdentity (`llkeyframemotion.cpp:662`) records instance,
lifecycle, frame/count/time, skeleton, asset revision, stop and appearance state.
prepareOwnedSampling at701 owns an asset reference and query values. Application
at738 validates an independently captured current identity, and updateWithSample
at1100 changes joints/hand only on the owner callback or runs direct.

That is stale-output validation and lifetime retention, not an immutability proof.
getKeyframeSamplingLease at530 aliases the same asset also exposed through mutable
mJointMotionList. setLoopIn/Out at2784/2814 can write loop keys read by sampling;
setPriority, setEaseIn/Out, setLoop and other mutators also invalidate then change
the shared object. A revision comparison after sampling cannot make a concurrent
read/write safe. Before a pilot, either prove and enforce an asset freeze for the
entire job lifetime or use separately immutable versioned sampling storage with
complete construction/retention costs measured. A const shared_ptr alone does not
remove those other aliases. No such production mechanism is installed here.

14v's two groups and ticket acknowledgement prove owned query/result buffer
lifetime only. Its Scene-scoped bindings do not prove a live deprecated/reentrant
motion resolver. Main idle can kill objects before cleanup. Any future owner map
must avoid retaining raw controller/joint pointers across an unproved lifetime;
workers continue to receive no such pointers. Q4/Q7 captures may inform readiness
and phase duration, but cannot replace this source ownership requirement.

## Decision before crossover collection

There is no established equivalent of the fixture's whole-cohort pre-controller
snapshot after all avatars have completed their mutating setup. A point near an
individual controller entry preserves local order but supplies less lead time.
Splitting preparation and controller traversal across avatars would require a
separate behavior/lifetime design; a synthetic speedup does not supply that evidence.
Do not add a frame, change callback order or introduce waits to manufacture lead.

The existing bounded CLI already accepts any cohort1..32, so intermediate
measurements need no native implementation change or rebuild. They can locate
the private computational crossover with the exact14v programs. They cannot
establish a live dispatch threshold, real mutation-free overlap, General-pool
contention tolerance or viewer FPS. The packaged General event trial remains
default OFF; the private pool is a separate otherwise idle queue.

Next collect predeclared2/4/8/16/32 direct/whole-event/event-row paired/reversed
cases, with separate allocations and the same strict idle/identity/window guards.
Keep the phase/asset/lifetime gates open regardless of the numerical crossover.
