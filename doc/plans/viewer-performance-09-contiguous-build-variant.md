# Checkpoint 9: isolated contiguous-animation build variant

Plan written before implementation. Parent: checkpoint 8 refined experiment.

## Objective and decision

Make the tested candidate available to the local building agent while retaining
original map storage in normal builds. Use a compile-time CMake option named
`FS_CONTIGUOUS_ANIMATION_KEYS`, default OFF, with consistent public definitions
for every consumer of the affected curve classes. There is no runtime toggle or
automatic enabling. The option changes type layout and requires a complete
consistent rebuild of dependent translation units.

The candidate improves synthetic longer-curve sampling and retained key bytes,
but adds loading cost and temporary memory; short rotations still have a small
sampling penalty. Keep these limits in the PR and acceptance handoff. No FPS
claim is justified without matched real-scene samples.

## Work sequence

1. Audit deserialization allocation/failure paths and cache publication. Ensure
   incomplete staged curves cannot become visible as valid assets. Check legacy
   and quantized formats, empty/duplicate/invalid keys, and edit/reload cleanup.
2. Introduce a small curve-storage helper/alias selected by the build option.
   Normal builds retain std::map. Experimental builds use bundled flat_map and
   a scoped temporary map while decoding. Finalize once per curve with an
   ordered unique range and release scratch nodes before the next curve loads.
   Avoid a second permanent representation or untrusted-count reservation.
3. Update the three curve aliases and one-key candidate paths only under the
   option. Preserve upstream code/comments. Keep arithmetic, query time,
   loop/hand-pose behavior, serialization ordering and `mNumKeys` diagnostics.
4. Exercise the actual helper with malformed/partial-load exits, duplicates,
   empty/long/reversed keys, signed timestamps and exact output checks. Regenerate
   the production sampling suite for both compile configurations. Measure helper
   allocations/peak behavior if implementation differs from the prototype.
5. Compile-check complete animation and pose callers with the option off/on and
   Tracy off/on as appropriate. Confirm the option propagates to all class-layout
   consumers. Keep native focused targets and full viewer acceptance distinct.
6. Publish a runtime-checkpoint PR with explicit OFF/ON build variants, exact
   comparison heads and separately retained installers/packages. Default builds
   continue using maps until real-scene acceptance supports a later decision.

## Build and runtime acceptance handoff

Use x64 ReleaseFS_open_AVX2, Nightly, NSIS. The building agent owns the shared
cache and may build only one viewer at a time. Request experimental configuration
explicitly with `-DFS_CONTIGUOUS_ANIMATION_KEYS=ON` through autobuild's CMake
arguments; give the control OFF explicitly when reusing a configured cache.
Retain both completed variants under distinct archive names. Do not run full
viewer builds or configure the shared cache from the source-work agent.

Test animation arrival/rez and retained/peak memory as well as steady crowded
frame times, poses, loop edits, gestures, animesh, preview close/reload, outfit
changes, teleport/logout and exit. Record exact settings and changing population.
Check solo and short-animation controls for regressions. Disabled-by-default
status is retained until these results are available.

## Next worker boundary

The candidate remains main-thread owned. Separate design work must give workers
owned immutable asset snapshots, versioned inputs/results and bounded queues,
and preserve same-frame application before constraints. Do not move raw-pointer
motion sampling or live joint-state writes to workers merely because sampling
is const or storage is contiguous.

## Results

Implemented with `FS_CONTIGUOUS_ANIMATION_KEYS` default OFF. `llcharacter` exposes
its 0/1 definition publicly so linked layout consumers, including the viewer's
target-based PCH, use the same setting. `fsanimationkeys.h` supplies the selected
map alias and a scoped builder. Normal builds forward writes to std::map without
scratch storage; experimental builds stage validated keys in an ordinary map,
bulk-convert once, then reset scratch before decoding the next curve. RAII
handles parse exits and conversion exceptions. Builders are single-use and
intended for the fresh empty curves allocated by deserialization.

The experimental one-key paths retain existing values and position finiteness
assertions. Serialization, all validation/decoding branches, duplicate-count
diagnostics, loop/hand-pose timing and final cache publication stay in place.
Nothing samples on worker threads in this checkpoint.

- `deserialize` owns a newly created `JointMotionList` with `unique_ptr` and adds
  each newly allocated joint motion to that list immediately. Its destructor
  deletes the joint motions and constraints on a failed parse.
- Only after keys and constraints finish validation does the function release the
  list to `mJointMotionList`, insert it into the cache and mark the asset loaded.
  Staged key data must be committed before this existing publication boundary.
- `mJointStates` is cleared/reserved and populated earlier in deserialization;
  the current function is not transactional over every character field. Preserve
  its existing failure handling and do not infer an asynchronous safe boundary.
- Legacy rotation times are checked for finiteness. Quantized time decoding and
  existing time/value validity checks must stay in their current order. Both
  rotation and position loops write validated keys by timestamp, with duplicate
  diagnostics afterward. No parallel decoding is proposed.
- Use a scoped builder per curve. In the ON configuration it stages into an
  ordinary map, constructs the ordered unique flat range at commit, then releases
  scratch nodes before the next curve. Its destructor must release scratch data
  on early returns. In the OFF configuration it forwards writes to the existing
  map and adds no scratch representation. Test the actual helper rather than
  assuming the earlier prototype establishes its behavior.

### Verification completed

- The actual helper passes 3,576 ordered key/value comparisons in map mode and
  3,579 in contiguous mode. Payload lifetime checks cover scoped partial/truncated
  exits, non-finite time rejection in a fixture parser, empty/long/reverse/shuffled
  loads, duplicate replacement, signed-zero timestamp identity, scratch release
  at commit and an injected conversion-copy exception. These are actual-builder
  tests, not full malformed-asset/LLDataPacker integration tests.
- Both build-selected aliases with the actual six production sampler methods
  pass 56,232 byte-exact actual-math comparisons each, plus state-immutability and
  const-signature checks. The position shortcut retains its debug assertion.
- A CMake fixture evaluates the actual option declaration and llcharacter target
  with placeholder unrelated libraries. Default/OFF resolve to 0 and ON to 1 in
  a linked consumer's generated compile definitions. It checks propagation, not
  full viewer configuration or linking.
- Complete `llkeyframemotion.cpp` and `fsposingmotion.cpp` native syntax checks
  pass in all four storage OFF/ON and Tracy OFF/ON combinations. The isolated pose
  check supplies `<chrono>` for the existing `fsjointpose.h` dependency, as in
  checkpoint 6. MSVC 19.51.36244, x64 AVX2; shared build data read only.
- The previous refined storage experiment still passes its three sampling
  suites and 293,004 cross-container checks. It explicitly selects reference
  storage and excludes the new build-conditional shortcut so its original
  map/flat control remains reproducible.

No full viewer build, installer, ARM/macOS execution or live scene acceptance has
been performed here. The builder uses the same staged-map/ordered-range algorithm
as the measured prototype; the prototype's timing/byte figures are not whole
viewer measurements of this helper. Actual memory behavior during asset arrival
and sustained crowds remains part of the build handoff.

### Exact build variants for the local building agent

After the normal repository bootstrap, configure the map control explicitly:

```text
autobuild configure -A 64 -c ReleaseFS_open_AVX2 -- --package --chan Nightly -DFS_CONTIGUOUS_ANIMATION_KEYS:BOOL=OFF
autobuild build -A 64 -c ReleaseFS_open_AVX2 --no-configure
```

Retain its installer and runnable package, then configure/build the experimental
variant serially with the same source/flags and the option ON:

```text
autobuild configure -A 64 -c ReleaseFS_open_AVX2 -- --package --chan Nightly -DFS_CONTIGUOUS_ANIMATION_KEYS:BOOL=ON
autobuild build -A 64 -c ReleaseFS_open_AVX2 --no-configure
```

The configure script passes these CMake arguments through. Reconfigure explicitly
when switching modes; do not reuse object/PCH output compiled with the opposite
definition. Build normally through CMake so its dependencies rebuild. Use distinct
archive variant suffixes such as `animation-map` and `animation-contiguous` under
the project's retained PR build directory. Do not overwrite completed archives.
The source-work agent has not executed these viewer build commands.

Plan 10 audits canonical asset ownership and adds owned leases before any worker
dispatch. Loop metadata may remain mutable on the main thread; it must never be
read opportunistically by a job that is only entitled to immutable key data.
