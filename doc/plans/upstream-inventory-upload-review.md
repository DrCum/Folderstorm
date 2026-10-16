# Upstream inventory-cache and folder-upload review corrections

This integration preserves #62 and the upstream PR histories at these reviewed
heads. No existing PR is merged on GitHub or rewritten:

| PR | Included head | Purpose |
| --- | --- | --- |
| #62 | `d107ae8ab0c8f40986263279c059b2ca004dfb76` | Existing integrated MCP, UI and shared-pose work |
| #63 | `97ce6e67225d6c5087a9b5efcf27cee806ce63a8` | Rename teardown, PBR offsets and camera fixes |
| #64 | `596b689aeead7f7418785673e28f8e85fff9e40e` | Background inventory-cache loading |
| #65 | `63d11dbc1edf1da0cea49fefada8bfb40aea55e1` | Recursive native folder upload |

## Verified findings and resulting behavior

Cache parsing uses the stored asset type, preserving item and category links for
the main-thread merge without consulting live inventory maps. The null-item-ID
diagnostic also uses the stored name: its previous virtual `getName()` resolved
links just like `getType()`. Generation fencing alone cannot protect either
lookup because the parse happens before that fence.

Folder import prepares all inventory destinations before submitting files to
the existing native bulk uploader. Category requests run one at a time, yielding
on idle between callbacks. Root, child, Materials and Textures failures stop
preparation and report the failed destination plus the number of folders already
created. Those folders remain in inventory; no asset files from that import have
been submitted, and the operation does not retry or delete created folders.
This avoids ambiguous partial asset uploads on folder-creation failure.

Account/login changes, disconnect and shutdown cancel further preparation or
submission silently. Duplicate callback delivery cannot resume a failed import
or submit files twice. Missing destinations and failed background validation
report an error before creating folders. Once preparation succeeds, individual
asset-upload failures, completion and charges remain the native uploader's
responsibility; this change does not claim transactional asset uploads.

Scan completion calls `upload_bulk()` directly. That existing helper handles
initial cost calculation, incompatible files and the confirmation UI. The
redundant preliminary image/glTF pricing pass is removed. Pricing still runs on
the UI thread once initially and when the user changes cost-affecting options;
bounded background pricing is not part of this correction.

## Focused validation

Run from the checkout root:

```sh
python3 -m unittest discover -s indra/newview/tests -p test_fsinventorycache_folderupload.py -v
git diff --check
```

Three checks pass. Two compile actual production parser/accessor and upload
callback bodies against portable boundary stubs using C++17 with
`-Wall -Wextra -Werror -pedantic`. Coverage includes:

- Stored type and name access for valid item/folder links and malformed null-ID
  links with diagnostic logging enabled; unknown types still dirty their parent.
- Folder failures at all four callback paths, exact remaining-folder counts,
  no partial asset submission and no automatic restart.
- Synchronous/asynchronous callback delivery, duplicate completion, source/login
  changes, disconnect, shutdown and cancellation before final submission.
- Image/sound/material destination routing, missing destinations, validation and
  queue failures, and 1,000 synchronous folder callbacks yielding without
  recursive callback growth.
- One initial pricing pass through the existing bulk helper, retaining its
  unsupported-file behavior.

Negative control: the original combined #64 parser at `1144bb6942` fails the
live-inventory-lookup assertion under the same harness (`FS_CACHE_SOURCE_REF`
selects the parser revision). These tests do not model native HTTP transport,
filesystem races, image encoding, GPU behavior or actual upload charges.

## Native acceptance still required

No full viewer/host build, packaging or GitHub build was run for this review.
Build this integration branch directly, rather than reapplying #63–#65 patches.

1. Compile the matching Windows viewer/host; log in with cached inventory that
   includes item and folder links. Verify inventory/library completion, link
   display, cache fallback and logout/relogin during cache loading.
2. On a test grid/account, upload a small nested tree with an image, sound and
   glTF material. Check confirmation cost, created hierarchy and material/texture
   destinations before testing larger imports.
3. Inject category-creation failure at root, child, Materials and Textures. Check
   the named error, remaining-folder count and that no asset uploads/charges
   started for that import. Fix the failure manually; no automatic retry occurs.
4. Disconnect/logout during destination preparation; verify no further folders
   or file batches are submitted by this operation after its source becomes
   invalid. An already submitted native request may still finish.
5. Check incompatible-only/BVH-only folders, changed files after confirmation
   and the 2K pricing toggle. Native bulk-upload warnings should remain intact.
