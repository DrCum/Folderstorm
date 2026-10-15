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

The own-pose checkpoint deliberately incorporates PR #35's verified native dependency tip `071403d63e7964935bd8d633ff0b6b11881f510e`, preserving its published history. This provides the native sharing/consent service; it does not itself advertise a shared MCP provider. A separate adapter checkpoint must enforce that service's grants and reservations. Do not bypass native consent by directly calling FSPoserAnimator on another avatar.

When available, add shared-session status, accepted invitation/grant listing and permitted-joint read/edit tools. Require both MCP permission and target-owner native consent, exact target/grid/native sharing-session generation, joint reservations and expected revisions. MCP cannot approve its own invitation, silently enable publishing/receiving, obtain broader joints or override another editor. Use native revocation/region/disconnect cleanup and target-visible activity. Optional publishing needs its own explicit native approval; local pose state is not proof of remote synchronization.

## Checks and execution

Root implements gesture playback ownership and Poser extraction sequentially because both cross native lifetime/state machines. A Sol 6.1 High agent may audit native APIs; no parallel edits to the same manager. Finished code receives Extra High review; Max is appropriate for a complex shared-consent/cleanup review if needed, never Ultra.

Test cold/cached gesture assets, wait/revoke/re-enable, duplicate callbacks, superseded manual play, shared animation IDs, gesture-disabled/RLV refusal and command-bearing gestures (including configurable commands). Test private versus public effects and transport receipts. For posing: self-target enforcement, bounded joints, manual conflicts, undo/reset, revoked reservations and stale target/session.

Windows: ordinary native gestures/Poser, no stuck animation or unwanted chat, muted/background behavior, self-pose restoration after interruptions; shared posing requires two real viewers, explicit owner grants, competing editors, revocation and disconnect. Do not mark E10 complete until its native dependency and acceptance path exist.

## Delivered gesture checkpoint

`gesture_list`, `gesture_status`, `gesture_play` and `gesture_stop` are registered behind `gesture_tools=1`. Reads require opt-in Gesture read; playback additionally requires Gesture play, active-character ownership and native approval. Literal gesture Chat also requires Nearby send. The sender bypasses command interpretation; configurable viewer commands are never executed.

Inspection freezes the exact COPY-permitted inventory item, asset UUID, SHA256 bytes, decoded effects and referenced animation bytes. Bounds are 16 KiB/64 gesture steps, 16 referenced animations, 1 MiB per animation and 4 MiB total. Native built-in animation states, nonempty embedded emotes, incompatible cached motion data and unsupported formats are refused. At most one assistant playback runs per login, for at most 120 seconds; its original bridge deadline may cancel it sooner.

Timed effects retain source/permission authority. Asset reads retain it through queued HTTP/UDP submission, callback delivery and cache publication, including region-generation changes. Native duplicate consumers are preserved. Manual animation ownership is never assumed: ambiguous ownership preserves the animation and reports incomplete cleanup. Submitted one-shot sound and Chat cannot be recalled. Native gesture activation is not changed.

Portable gesture/asset-authority checks, registered Go tool tests and native-boundary source checks pass. Independent Extra High review is recorded on the checkpoint PR. Native Windows compilation and runtime acceptance remain pending; the available environment has no CMake executable, so direct strict portable compilation was used.

Windows acceptance:

- Inspect cold/cached exact items, then play the returned revision; reject changed bytes, unsupported states and restricted/non-COPY items.
- Exercise waits while pausing access, revoking permissions, changing regions and switching characters. No later effect or stale asset publication should occur.
- Repeat native manual playback/local preview during assistant playback, including the same animation ID. Cleanup must preserve newer or uncertain native ownership.
- Verify literal Chat does not invoke viewer commands; denied Nearby send refuses the entire Chat-bearing gesture.
- Check native muted/background audio behavior, explicit stop, receipt outcomes and incomplete-cleanup reporting. Do not replay uncertain sends.

Own and shared posing remain separately reviewed checkpoints described above.

## Delivered own-pose checkpoint

Ten typed tools provide status/joints, start/update/undo/reset/stop and preset list/load/save under `pose_tools=1`. Opt-in Pose read, Ask-capable Pose edit and separate Ask-capable Save pose preset permissions remain distinct. Mutations require this account's active self-avatar and exact pose session/revision. Another native Poser or shared self-pose owner refuses acquisition. No agent translation, position/scale, world-lock, animesh or camera controls are exposed.

Private poses use a separate random local motion and at most 64 named joint rotations. Revocation checks run independently of animation-controller culling; native Poser takeover invalidates the old transaction before capture. Cancellation uses local motion removal, without a simulator animation STOP. Undo/reset/stop restore acquisition rotations only while the current joint still matches the assistant's last applied contribution; external/mixed state is preserved and incomplete restoration reported.

Presets are exact, hashed IDs under the native `poses` directory, bounded to 64 KiB. Loading accepts supported native version-8 absolute local-rotation XML only, after strict complete-document/depth/shape validation; wider or relative state fails explicitly. Saving creates a new confined filename and never overwrites an existing preset. Pinned non-link directory/file handles prevent pathname replacement from redirecting I/O. Symlink/reparse/UNC settings paths intentionally refuse; UTF-8 filenames remain supported without deprecated C++20 path APIs.

Independent Max source review is clean at `0e19856243`; final parent integration retains the separately reviewed gesture/content/Preferences changes. Strict portable pose/XML/file-link/race checks, registered Go pose tools and six native-boundary source checks pass. CMake registration is source-reviewed only because CMake is absent. Native Windows compilation, real animation blending and runtime acceptance remain pending.

Windows acceptance:

- Start an active self-pose, apply bounded rotations and exercise undo/reset/stop on animated and otherwise unanimated Bento joints. Check restoration outcomes.
- Open native Poser during a private pose, then close it; the old assistant pose must not resume. Revoke access while the private motion is fully covered by another motion.
- Change source/region/login or disconnect. Future effects must stop without simulator requests for the private UUID; ambiguous newer state must survive.
- List/load/save Unicode presets, collisions, unsupported wider-state XML, trailing documents and changed file revisions. New files must stay in the native pose directory.
- Verify default permission presets and native approval distinguish runtime posing from saving files. No operation may control another avatar or implicitly enable sharing.

Shared MCP posing remains a separate checkpoint with two-viewer native consent acceptance.
