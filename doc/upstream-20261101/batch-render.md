# Rendering and animation correctness

Base: main `e462dd1d1f`; independent from the other safety/cache batches.

Plan and sources:

- SL `bcb90c8dce7006f861b8252bd401fa379c0c54d5`: advance the animation
  clock during minimal/culled updates while preserving paused behavior and
  processing non-looping/easing-out motion lifetime.
- SL `ccfd47cc8a8bf12e93b3a487d4f184bd41132e3e`: let the bound FXAA
  destination determine its viewport, including scaled and offset world views.
- SL `5f111ff91c567e9b4e56d42e60d4a7f1bfd565cc`: pick culled terrain under
  opaque water while still refusing disabled render types and culled objects.
- SL `373addb1cbdb6ae505c8e797c33729cb1ad75dba`: retain Folderstorm's
  existing LLConstPointer skin lifetimes, port only missing identity-matrix and
  null-info guards. Apply the null guard to all three native upload overloads.

The earlier pointer/lifetime portion of 373addb1cb is content-equivalent in
main. #32's terminal palette bounds remain intact. No experimental #161 code,
assistant authority change or new renderer mode is imported.

Validation: four compiled production-method/snippet checks pass with bounded
clock, GL, scene and matrix fixtures. Cases cover minimal/pause/resume clocks,
FXAA destinations at 960x540 and 320x180 while the world view is offset,
fallback copying, terrain-only visibility exceptions and short palette identity
initialization. Header/source checks preserve native retained skin pointers and
all three upload guards. All five changed .cpp files pass real-header Linux
syntax checks. Fixtures do not run actual GL, animation assets or GPU picking.

Trial merges with #160/#161/#162 and preceding memory/avatar batches are clean.
#161 shares llmotioncontroller.cpp and llskinningutil.cpp. Its predictions read
the controller clock; the source fix changes minimal-update clock progression,
not the experimental prediction/shadow implementation. If #161 is pursued,
re-run its animation equivalence checks on the combined native build.

Native acceptance: couples animations when avatars leave/reenter view; pause,
non-looping completion and local/shared poses; FXAA plus resolution divisor in
Standard/custom viewports, host previews and snapshots; opaque-water alt-click;
missing/short rigged meshes. Full viewer/Windows/GL runtime remains unrun.
