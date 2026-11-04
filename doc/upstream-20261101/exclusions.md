# Verified equivalents and exclusions

Pinned baseline for all content checks:
`e462dd1d1f7444b3922989ec30677814bd63c3cd`. Lack of matching ancestry or
stable patch ID was not treated as proof a fix was missing.

| Source | Disposition | Content/provenance evidence |
| --- | --- | --- |
| SL `f47a45343092c90c549875657b1857aee95b124a` | Already equivalent; no import | `indra/llxml/llxmlnode.cpp` lines 1546, 1554, 1562, 1570 already use strncmp for inf, -inf, 1.#INF and -1.#INF. All four operations match the source fix. |
| SL `373addb1cbdb6ae505c8e797c33729cb1ad75dba` | Pointer portion already equivalent; remaining guards in #169 | LLFace and LLDrawInfo already hold LLConstPointer<LLMeshSkinInfo>. Retain those stronger const native references; do not replace them with upstream's LLPointer. Only missing null/palette identity guards are imported, covering native overloads. |
| Firestorm `898bdf73670125180510286782476bc75287bc13` | Not applicable | `indra/llcommon/llprocess.cpp` uses synchronous apr_file_write (line 201), not the newer startAsyncWrite/asio::async_write implementation whose buffer ownership is fixed. No pipe transport upgrade is included. |
| Firestorm `06eed6230ac60500ec51ba73f0af3a8362b0ba9f` | Not applicable | `handle_selected_script_action` in llviewermenu.cpp uses bool mono=false then mono=true for Compile Mono, passed to the older compile queue. It has no shadowed string target from the source fix. |
| Firestorm `83c687e21d8afd0eb82b1ba73231a847a7e6d4ba` | Not applicable as a standalone fix | Current editor lacks LLScriptEdCore::updateTabs(bool is_lua) and getIsLuauLanguage(), using the older LSL/preprocessor path. Porting those hunks alone would require language/API changes rather than fix an equivalent defect. |

#32 (`9e9a8a5cbb2a0110e3b1cc4a01c874a7c2bd9537`) and #66
(`6eef8a47aee4610330fda803a0c2b5677c56400b`) are ancestors of main.
The post-fork main patch-ID inventory covers 225 non-merge commits. None of the
41 candidate patch IDs match those exactly. The table above records the content
checks that prevent that result from causing duplicate/inapplicable imports.
Prior #66 parser isolation also remains in the cache/inventory plans and checks.

Scope exclusions remain: previously rejected DM-sharing and IME changes, paid
viewer workflows, CLA restoration, release/installer publication and security/
credential changes. No competing daily automation is created. Supplied target
configuration is OpenAL/OpenSim (FMOD disabled); no FMOD-only work is added.
