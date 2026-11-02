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

Task upload completion invalidates running queries for both native and assistant
uploads, including failure/cancellation. A query sent during compilation cannot
become evidence for the completed execution when its reply arrives late.

An explicitly `running_mode=stopped` save can advance execution even with a lost
or ambiguous running reply. The old wire query and its original execution remain
fenced; nothing infers running state from timeout. Preserve mode, start/stop/reset
and fresh running observations still require unambiguous replies. A timed-out
query is refused promptly with recovery guidance instead of being joined forever.

Contents insertion no longer treats two undisclosed asset UUIDs as a match.
For eligible COPY/MODIFY scripts, native item-bound downloads resolve hidden
source and destination asset identities without changing their cached metadata.
Confirmation waits for all potentially matching new entries; multiple matches
stay ambiguous. A captured server snapshot (at most 64 new candidates) prevents
optimistic local deletion or asset changes from altering that conclusion. Native
refreshes can temporarily clear Contents; successful lookup proof waits privately
for matching fresh Contents instead of being lost. Each operation probes at most
eight new scripts and retains its receipt while callbacks are outstanding.
Changed source/account/permissions or
private-read epoch invalidate the proof. Scripts whose source cannot be read,
and unsupported undisclosed asset types, can still be copied where permitted,
but their receipt cannot claim a verified destination identity.

Primitive creation has no existing object ID. Building approval summaries now
read optional request fields through const LLSD, preserving the frozen material
instead of inserting an undefined `id` and cancelling with
`operation_source_changed` before dispatch.

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

`contents_put_native` compiles the production insertion sender, native-readable
eligibility, verification callbacks, fresh-Contents observer and receipt settlement
against bounded inventory/transport fixtures and real async guards. It covers
known/hidden source and destination assets, nested objects, wrong/ambiguous
assets, inline callbacks, revoked/stale permissions and login, callback retention,
and the eight-probe limit. The running-query adapter also covers assistant upload
completion and explicitly stopped recovery while a late reply remains fenced.
Refresh/cache clearing and local removal/restoration cases exercise the same
verification continuations. Terminal save receipt validation is tested separately
from active transport, retaining source/signature and permission revocation checks.

`building_native` compiles the production building preparation, material freeze,
ticket and validation functions plus bridge guard registration. Native admission,
scene state and summary serialization are fixtures. It covers all four primitive
shapes with/without a tracking ID, inventory rez, changed arguments, live native
denial, changed login/state, expired tickets and revocation before submission.

## Windows / Second Life acceptance

Use a matching rebuilt viewer/MCP and disposable, owned QA scripts. Do not change
existing project scripts without authorization.

- Create one QA cube on a buildable parcel with explicit position, scale and
  rotation, both with and without a tracking request UUID. It should pass the
  unchanged-material guard. Changed native permission, account, parcel or request
  arguments must still stop submission. A tracked rez remains unconfirmed until
  its candidate is explicitly associated; never replay an uncertain creation.

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
- After a running-state timeout, make an explicitly stopped QA save using fresh
  source evidence. It should compile without a relog. An old delayed reply must
  not make a new running assertion pass. Preserve mode must not infer a state.
- Save an inventory QA script, then put it into a disposable cube. Even when
  fresh task inventory hides its asset UUID, the receipt should identify the
  installed task item after native verification. Repeat with the source asset
  UUID undisclosed too, and with two competing new entries; ambiguous outcomes
  must remain unconfirmed. Revoke read access while verification is pending.
- Take a copy into a QA inventory folder and insert that exact inventory object
  into a second cube; confirm the item ID without replaying either operation.
- Check an existing notecard update and a zero-cost asset upload. Required asset
  quotes and any higher-than-approved server price must remain enforced.

Real metadata transport, native Windows compilation, paid uploads and in-world
save/readback remain manual acceptance steps.
