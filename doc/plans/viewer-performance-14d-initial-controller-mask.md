# Checkpoint 14d: initialize the controller's first activation mask

Plan written before implementation. Discovered while comparing checkpoint 14c
with the real motion controller, before any live worker dispatch.

## Problem and scope

The controller constructor does not initialize `mJointSignature`. Its first
`activateMotionInstance` calls a motion's virtual `onUpdate(0, signature[1])`
before `updateMotions` necessarily resets the arrays. Constraints use this mask
to decide whether to run, so initial behavior can depend on prior stack/heap
contents. Checkpoint 14c's controller comparisons explicitly perform the normal
first controller update to establish defined masks before activation; they do
not establish correctness of activation on a never-updated controller.

## Work sequence

1. Reproduce deterministically using poisoned storage for a freshly constructed
   actual controller/character and a test-only motion that captures its first
   activation mask. No source extraction, mock controller or account operation.
   Confirm the original constructor leaves the poison observable at onUpdate.
2. Value-initialize the two existing signature arrays in the controller
   constructor. Keep later per-update reset logic, motion callbacks, timing,
   ordering, blending and constraints unchanged. Do not fold this behavior fix
   into the phase-contract PR.
3. Verify the first mask is zero for multiple prior storage patterns, initial
   activation and empty normal/hidden updates. Confirm active-motion masks and
   subsequent update ordering still match the existing controller behavior.
4. Run checkpoint 14c's real-controller and frozen-update tests in both key
   storage modes, and independent source checks as needed. Keep native scratch
   output outside Git; do not configure or build in the shared viewer cache.
5. Publish a separate exact-head PR, record first-activation constraint acceptance
   for local viewer testing, and write the next plan before further work.

## Following work

Before live sampling, continue the current-context/eligibility audit and establish
per-job floating-point context capture/restoration for shared workers. Do not
turn a protected base phase API into automatic eligibility for walk, stand,
fall, poser or arbitrary virtual subclasses. Real same-frame overlap and matched
crowd frame-time/quality evidence remain required.

## Results

Not implemented yet. Constructor omission and inconsistent first-activation
constraint output were observed during 14c; deterministic poison reproduction
is the first task here.
