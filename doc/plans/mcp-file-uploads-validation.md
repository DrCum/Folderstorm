# MCP file uploads — validation and native acceptance

Implementation is above #78 with additive capability `file_uploads: 1`. It adds
five typed tools, an Ask-by-default Upload permission, native automatic
texture/sound/animation uploads, directory hierarchy, source-bound receipts and
native mesh review. Mesh review explicitly requires resident completion; it is
not an automatic mesh uploader. Host IPC/version is unchanged.

## Completed focused checks

- Strict C++17 portable upload policy/progress tests: free and paid approval,
  missing/negative/increased server quotes, bounded names/paths/hashes, terminal
  ordering and revocation. Assertions remain enabled in the standalone CMake
  test target `file_upload`.
- Python compiles and runs the actual production `advance`, `stop` and `live`
  functions with native transport stand-ins and the real durable authority
  helpers. Covers queued revocation, lost capability response, late confirmed
  IDs/charges, cancelled folder creation, completed final-file ordering,
  fail-stop batches, and moved parent folders before native queued submission.
- At the 32-plan limit, repeated status lookups retain cancelled uploads and
  folder operations through late confirmation and callback-owner cleanup.
  Outstanding native work blocks capacity eviction; only admitting a new plan
  can replace terminal history after its native work settles.
- Registered Go MCP tools: manifest hierarchy/hashes, count/depth limits,
  unsupported/duplicate/linked/oversized sources, explicit session targeting,
  stale/denied/unsupported viewers, and absence of agent-supplied price bypasses.
- Focused Linux C++20 syntax checks of `fsfileuploadlistener.cpp`,
  `llviewerassetupload.cpp` and `fseventapibridge.cpp` against actual viewer and
  dependency headers. This is not a viewer build or link test.
- Go Windows amd64 test-binary cross-compilation and affected-package vet.
- Sol 6.1 Extra High review and re-review: all reported findings fixed; no
  remaining blocker found in this scope.

## Windows acceptance still required

1. Build matching viewer/MCP. Verify Upload appears in native Preferences,
   presets and saved choices, including OK/Cancel and session-only choices.
2. Upload one of each supported file with native permissions set to Allow.
   Verify dimensions, names, inventory destinations, returned IDs, and default
   upload permissions. No asset should play, wear, rez or execute.
3. Test Premium free sound/animation uploads and a free OpenSim grid where
   available. With all required permissions Allow, L$0 must not prompt for price.
   With Upload Ask, verify ordinary permission authorization still appears.
4. Verify a paid texture/sound/animation batch shows exact per-file and total
   price before spending. Cancel it; no upload should start. Verify server quote
   mismatch/refusal stops before bytes, never silently increases the ceiling.
5. Import a nested directory, then test a missing/invalid file, full destination,
   failed folder creation, insufficient balance and dropped response. Inspect
   partial/uncertain receipts; repeated Execute must not duplicate work.
6. Revoke/pause access or disconnect during folder queue, capability handshake
   and binary transfer. Confirm unsent work stops; same-login accepted folders
   and late asset confirmations remain reported. Moving/renaming a created
   folder during queued work must stop the plan, including before its next AIS
   POST. Reconnect under another account and verify old plans are unavailable.
7. Open DAE mesh review; verify native permission, LOD/physics/rigging choices,
   server price calculation and explicit native completion. Close and reopen
   review; never replace an already occupied importer. No upload is implied by
   `native_review`. Try normal viewer and hosted active-character operation.

No native Windows runtime, real upload charge, Premium account, or mesh/GPU
acceptance was performed in the Linux workspace. No installer or release was
built or published.
