# MCP script-save boundaries

Follow-up to the source-resolution change in #162. An ordinary script does not
need an Experience. Task script saves query native GetMetadata to preserve any
existing LSL Experience association; silently clearing an unknown association
would change the script's behavior.

The request now uses native LLSD UUIDs for object/item identities, matching
LLExperienceCache. A successful response with an absent, empty, undefined or nil
UUID association means no Experience, matching the native lookup and editor.
Any supplied valid UUID is preserved; malformed supplied metadata and HTTP
failures still stop before upload. Receipts distinguish `metadata_http_error`
and `metadata_invalid_experience` and include numeric `metadata_http_type` /
`metadata_http_status`. In this library, type 200 with status 0 is HTTP success.
No capability URL, source text or Experience identity is added to those
diagnostics. The reconnect report confirmed the successful missing-field case;
real simulator save/readback still needs acceptance testing.

Existing source-update capabilities may omit `upload_price`. Reading that field
must not insert it before testing presence. Asset imports still require a quote;
an explicit quote must be an integer within the approved spending ceiling,
including zero. Identical content-save retries also use their exact execution's
upload lease instead of adopting an older operation with the same arguments.

## Focused checks

The standalone CMake/CTest target `script_save_native` compiles extracted
production metadata request/response handling, task-save continuation and native
upload body, native Experience lookup and editor result handling, the upload-price
gate, inventory-save lease validation, and the bridge's frozen execution binding.
It uses production async guards and upload policies. LLSD, HTTP, inventory/editor
state, Experience-cache data delivery, hashing and selected admission boundaries
are fixtures; this is not a simulator or native Windows UI test.

Cases include typed request IDs; absent/explicit no-association and real-association
responses; failed HTTP and malformed supplied metadata; source/permission revocation
while metadata is pending; revocation before capability submission; optional
prices, required quotes, zero-cost quotes and increased/malformed prices; and
identical failed-save retries alongside unrelated pending uploads. Existing
source-resolution, object lifecycle and upload-boundary checks remain relevant.

## Windows / Second Life acceptance

Use a matching rebuilt viewer/MCP and disposable, owned QA scripts. Do not change
existing project scripts without authorization.

- Read a fresh inventory script, preview, then save/compile a small edit. Verify
  source readback and native compiler diagnostics. Repeat an explicitly requested
  save after a failed attempt with identical material; it must not cancel itself
  at prepare or claim a missing optional price increased.
- Save a task script with no Experience, requesting a stopped result. Verify
  `experience_preserved`, `experience_metadata=known_none`, source readback and
  compilation, including when a successful GetMetadata response omits the
  Experience field. Opening the native editor first must not be required.
- On an authorized QA script with an existing Experience, save without changing
  its association and verify it is retained in the native editor. Repeat with
  `running_mode=preserve`; check running state separately from compilation.
- If metadata still fails, retain the receipt's numeric HTTP fields and
  `experience_metadata`. A failure must leave source and running state unchanged
  and must not submit source bytes. Do not substitute an empty association.
- Revoke Assistant access, change/delete the item, or log out while metadata or
  an upload is pending. No later source submission may proceed. An open native
  editor or unrelated pending native upload must still block conflicting saves.
- Check an existing notecard update and a zero-cost asset upload. Required asset
  quotes and any higher-than-approved server price must remain enforced.

Real metadata transport, native Windows compilation, paid uploads and in-world
save/readback remain manual acceptance steps.
