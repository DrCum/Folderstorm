# MCP gestures and posing

E5: gesture queries/playback and own posing. E10: shared posing after the reviewed native consent service is available. Depends on E0; plain gesture chat also depends on E2 send policy. No navigation, building or animesh controls.

## Gesture tools

Add `gesture_list/status`, then explicit `gesture_play/stop`. Resolve exact inventory item and asset revision; list capabilities, active/playing state and declared effects. Preserve gesture enable preferences and RlvActions.canPlayGestures. Gesture activation/download guards added in #47 do not yet guard timed playback.

Playback must retain a per-playback operation/task through asset loading, mPlaying, animation/sound requests, waits, future runStep calls and completion. Recheck before every delayed effect. New manual replay/supersession gets its own generation; an old cancellation cannot stop a newer manual play. Stop/cleanup may release only assistant-owned animation/sound contributions. Shared animation IDs require ownership/reference tracking; do not stop a manual or unrelated gesture using the same animation ID. If ownership-safe cleanup cannot be established, keep that effect unsupported rather than expose unsafe playback.

Inspect and freeze the actual gesture steps before approval. Native runStep sends chat through cmd_line_chat, which can invoke configurable teleport/touch/sit commands. Therefore do not simply expose raw startGesture or use a slash-prefix denylist. First playback slice supports audited animation/sound/wait-only gestures; reject chat-bearing gestures with a clear unsupported reason. The next dependent slice supports plain Chat only via an audited sender that never executes viewer commands, requires Nearby send plus gesture permission, shows exact content/effects and obeys native restrictions. It must not silently delete chat steps and claim full playback.

List/read access is separately opted in; play/stop is Ask-capable and cannot grant Nearby-send authority. No implicit activation, voice ownership, background unmute or camera changes. Initially require the targeted character active for playback mutations unless the native background path has been proven safe; reject RequiresActive without switching it. Read state remains background-safe. Any local-only animation preview versus simulator-visible playback is explicitly labelled.

## Own posing

Expose a typed adapter over FSPoserAnimator: `pose_status`, `pose_joints`, `pose_start`, bounded `pose_update`, `pose_undo/reset/stop`, plus exact saved-preset listing/loading/saving where the native format supports it. Use preset IDs confined to the native pose store, not arbitrary paths. Saving needs a separate permission and collision/revision handling. Native loadPoseFromXml is not safe for the rotation-only adapter: it applies position/scale even in restricted modes, and newer presets can restore wider motion state. Freeze preset bytes/revision and use a bounded audited parser that applies only permitted rotation fields; reject broader state/root/position/scale fields until their separate implementation is reviewed. Test newer preset versions preserve unrelated manual motion state.

Explicitly enforce target == current account avatar before invoking animator methods. The animator's null/dead/same-region check is not consent. Freeze joint identities, reference frame, bounded rotations and pose revision. Preserve native undo and restoration semantics; no agent/world translation, movement, sit/stand, animesh or camera changes. Joint position/scale support is a later substep within this plan only after per-joint bounds, native parity and restoration tests; root/avatar-world translation remains excluded. Start with active-only mutations, no automatic promotion.

Pose transactions detect manual Poser edits and stop stale MCP edits without overwriting newer native work. Permission revocation, target replacement, region/login change and disconnect stop future effects; cleanup restores only state still owned by that pose transaction. Report incomplete restoration explicitly.

## Shared posing prerequisite

This branch's havePermissionToAnimateOtherAvatar returns false; no shared service is present here. Verify PR #35's actual reviewed service/head and coordinate with its owner. Do not bypass the floater's denial by directly calling FSPoserAnimator on another avatar, and do not merge the dependency automatically.

When available, add shared-session status, accepted invitation/grant listing and permitted-joint read/edit tools. Require both MCP permission and target-owner native consent, exact target/grid/native sharing-session generation, joint reservations and expected revisions. MCP cannot approve its own invitation, silently enable publishing/receiving, obtain broader joints or override another editor. Use native revocation/region/disconnect cleanup and target-visible activity. Optional publishing needs its own explicit native approval; local pose state is not proof of remote synchronization.

## Checks and execution

Root implements gesture playback ownership and Poser extraction sequentially because both cross native lifetime/state machines. A Sol 6.1 High agent may audit native APIs; no parallel edits to the same manager. Finished code receives Extra High review; Max is appropriate for a complex shared-consent/cleanup review if needed, never Ultra.

Test cold/cached gesture assets, wait/revoke/re-enable, duplicate callbacks, superseded manual play, shared animation IDs, gesture-disabled/RLV refusal and command-bearing gestures (including configurable commands). Test private versus public effects and transport receipts. For posing: self-target enforcement, bounded joints, manual conflicts, undo/reset, revoked reservations and stale target/session.

Windows: ordinary native gestures/Poser, no stuck animation or unwanted chat, muted/background behavior, self-pose restoration after interruptions; shared posing requires two real viewers, explicit owner grants, competing editors, revocation and disconnect. Do not mark E10 complete until its native dependency and acceptance path exist.
