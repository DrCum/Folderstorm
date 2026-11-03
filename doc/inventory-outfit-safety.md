# Inventory outfit safety

Inventory context-menu **Wear** is disabled by default. Enable it at
**Preferences → User Interface → Interface Windows → Inventory options →
Enable "Wear" in inventory menus**. The choice uses normal viewer settings and
Preferences Apply/OK/Cancel. Add, Attach To, double-click preferences and the
dedicated Outfits controls retain their existing behavior.

Folder **Add To Current Outfit** and **Replace Current Outfit** always confirm.
Cancel is the default, Escape cancels, and there is no suppression checkbox.
One prompt retains the selected folder IDs for the originating account, grid
and login session. It does not retain inventory UI objects. Accept rechecks the
whole selection and the native outfit limits before submitting any folder.
Native appearance handling continues enforcing individual wearable/attachment
restrictions and fetching/copying folder contents.

## Focused checks

```sh
python3 indra/newview/tests/test_inventory_outfit_safety.py -v
```

Also registered as `inventory_outfit_safety` in the standalone
`indra/newview/tests/folderstorm` CTest project. The fixtures execute production
confirmation/keyboard code with mocked UI and inventory boundaries. They do
not exercise Windows rendering, live servers or asynchronous outfit loading.

## Windows acceptance (pending)

- [ ] In list and Gallery views, including a second inventory window, Wear is
      visible but disabled for clothing, body parts, attachments and item links.
      Existing native eligibility still applies when the preference is enabled.
- [ ] Toggle the preference: Apply/OK retains it; Cancel restores it; restarting
      preserves an applied choice. Add and double-click behavior still work.
- [ ] For both folder actions, check the named folder, Add/Replace and Cancel
      buttons, default Cancel focus, Enter on the initial focus, Escape and any
      available close control. Only explicit Add/Replace changes the outfit.
- [ ] Multiple eligible folders produce one prompt and act on the captured
      selection. Changing selection or closing inventory before replying does
      not retarget the operation or crash. Mixed item/folder selections do not
      submit a partial operation.
- [ ] Test normal folders, saved outfits and Library folders. Check folder wear
      limits, a change already in progress, trash/deletion while prompting and
      RLVa locks introduced before accepting. Blocked selections do not submit.
- [ ] Check the new preference fits with larger fonts/UI scaling and alternate
      skins. Confirm menus remain keyboard-accessible and the host is unaffected.

This PR is independent of the held MCP and upstream-import branches and is
based directly on `main` (`e462dd1d1f`). No host protocol changes are required.
