# MCP building and inventory permissions

Status: implemented; native Windows acceptance pending.
Base: PR #75; preserve the existing MCP and host stack. Publish independently from navigation.

## Tools and boundaries

- `build_object_get`: inspect one loaded root volume, transform and up to 64 faces; issue a short-lived, login/region-bound revision.
- `build_object_transform`: edit position, rotation and size of an owned, nonphysical, unattached root using that exact revision. Move/rotate linked roots together; refuse linked-set resizing until its child transforms can be reviewed together.
- `build_object_surface`: edit one face's RGBA, glow, fullbright and UV transform, plus an optional inventory-backed diffuse texture on legacy faces. `build_object_material` assigns a PBR material separately; PBR parameter overrides remain in native Build. Asset assignment requires owned, full-permission inventory to avoid weakening native permission propagation. No arbitrary asset UUID assignment or uploads.
- `build_rez_primitive`: create a cube, sphere, cylinder or cone near the avatar on the current buildable agent parcel. Explicit transform; no native selection takeover.
- `build_rez_inventory`: rez one owned, copyable inventory object on that parcel, retaining the source item. No-copy rezzing, purchases, deletion, linking and scripted building remain outside this checkpoint.
- `inventory_permissions_get` / `inventory_next_owner_set`: inspect and set next-owner Copy/Modify/Transfer on a finished, owned agent-inventory item. Match native Properties eligibility and object slam flags. Preserve all other permission masks, sale information and identity. Reject no-copy/no-transfer and unavailable base rights explicitly.

## Authority and native behavior

Require an explicit viewer_session for every tool, versioned `building_tools` capability, bounded inputs, and native viewer dispatch. New Ask-capable classes: `build_edit`, `build_rez`, `inventory_permissions`. Reads retain existing inventory Read / opt-in World read. World writes additionally require World read and the active managed character. Inventory permission changes need not acquire foreground input.

Freeze exact source identity, target, native baseline and material arguments before Ask; recheck before dispatch. Expiring bounded revision/preparation stores clear on identity changes. Respect RLVa edit/rez/location restrictions, ownership, native scale limits, destination parcel build permissions and region boundaries. Check authority at every transport/async boundary. Do not change global selection or replay uncertain submissions. Report submitted versus simulator-confirmed behavior honestly.

## Validation and acceptance

Focused portable tests for finite transforms, quaternion normalization, next-owner rules, revision expiry/source binding and one-shot submission; source contracts for actual native gates/packing and delayed callbacks; affected MCP Go tests for schemas, exact source, permissions and forwarding. No broad viewer build or packaging.

Windows: matching viewer/sidecar, Ask allow/deny/revoke; owned versus foreign/locked/physical/attached objects; linked-root movement and refused scale; per-face surface/texture/material; parcel refusal; primitive/copyable rez without selection changes; next-owner changes reflected in native Properties and surviving inventory refresh. Native compilation/runtime remain pending until tested on Windows.

## Checkpoint validation

Passed: five affected portable C++ targets (including ticket/revision model, permission defaults, feature manifest, async authority and session guards), four native boundary source contracts, affected Go building/schema/permission tests. Strict C++17 warnings and Release assertions retained; XML and diff checks pass. An attempted native syntax-only check stopped at missing Boost headers, before compiling the listener. No native viewer build, installer or live simulator mutation was performed.
