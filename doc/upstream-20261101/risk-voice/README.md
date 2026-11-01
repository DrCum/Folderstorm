# Review required: WebRTC shutdown and microphone capture

Branch `risk/20261101-webrtc-capture`, base main `e462dd1d1f`.
**Evaluation only; neither candidate is applied to production.**

Exact sources:

- SL `d632b85899e76529c9b42affc4fe12df75c1af5e`: termination guard and
  signaling-thread drain to prevent device work from requeuing after shutdown.
  See [disconnect-upstream.patch](disconnect-upstream.patch).
- Firestorm `1bb852e5a40d9bd31dcfeed9e1c4e87f2141a2d5`: optional
  stop-capture-on-mute behavior, WebRTC interface change, dependency update and
  viewer preference. See [capture-upstream.patch](capture-upstream.patch).

They are grouped for related device/shutdown ownership review, not silently
bundled into an ordinary batch. Dry-run diagnostics for each independently
against main are in [applicability.json](applicability.json). Both fail; that is
not evidence the bugs are absent, only that a direct cherry-pick is insufficient.

Risks/evidence:

- Current code moves worker/signaling/device ownership into a shutdown thread
  and can detach it on timeout. The disconnect patch adds mTerminating checks,
  drains signaling work and changes device repost paths. Correctly integrating
  that requires examining all task posting and observer/lifetime paths, not
  accepting only the hunks that happen to apply.
- The capture option adds a virtual setStopCaptureOnMute() method and updates
  the third-party WebRTC package from m144.7559.06.20 to .22 in its source
  context. Matching interfaces/binaries and actual Windows/macOS/Linux audio
  device behavior require a dedicated native build. The source also changes
  settings/XML; no such setting or package changes are applied here.
- Folderstorm's LLVoiceClient gates enable, mute, PTT and state queries through
  FSSessionWorker::voiceAllowed(). Its worker requires permitVoice, readiness,
  input grant, active mode and no promotion. Any capture restart must preserve
  those gates, including character switches and revocation while tasks queue.
- Stopping capture discards the warm AEC stream and may cause unmute hiss,
  Bluetooth mode changes, tuning/device-switch races or unwanted restarts.
  OpenAL/OpenSim is the supplied build configuration; these WebRTC changes are
  not FMOD-only fixes and are not excluded on that basis.

Plan/prerequisites for approval:

1. Integrate shutdown/observer/device-task ownership first, retaining existing
   main lifecycle fixes. Test delayed worker/signaling work, disconnect,
   termination timeout, observer removal and reinitialization with native mocks.
2. Separately pin and compile matching third-party interfaces/libraries before
   implementing the capture preference. Confirm upstream platform fixes and
   retain Folderstorm's build/update/signing setup; do not publish installers.
3. Add explicit native tests for mute/unmute/PTT, no active connection, voice
   disabled, device changes/tuning, Bluetooth headsets, host active-character
   microphone grant/swap, promotion failure and access revocation while queued.
4. Validate native Windows full linking and actual audio hardware before adoption.

Validation performed: source/provenance/API inspection and non-mutating dry-run
applicability checks only. No candidate compilation, native threading/device
execution, package upgrade, settings change or full viewer test was run.

Decision for Marty: prioritize shutdown stabilization separately; then authorize
the SDK/capture work and headset acceptance, or retain current capture policy.
Recommend that ordering. Keep this draft isolated until its ownership and binary
compatibility prerequisites are demonstrated. Held #160/#161/#162 remain untouched.
