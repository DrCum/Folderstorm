/** SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "llinventorymodel.h"
#include "llviewerinventory.h"
#include "llviewercontrol.h"

namespace fs_inventory_archive
{
inline constexpr char setting[] = "FSArchivedInventoryFolders";

inline bool eligible(const LLUUID& id)
{
    const auto* cat = gInventory.getCategory(id);
    return cat && (cat->getPreferredType() == LLFolderType::FT_NONE || cat->getPreferredType() == LLFolderType::FT_OUTFIT)
        && gInventory.isObjectDescendentOf(id, gInventory.getRootFolderID())
        && !gInventory.isObjectDescendentOf(id, gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH));
}

inline bool marked(const LLUUID& id)
{
    return gSavedPerAccountSettings.getLLSD(setting).has(id.asString());
}

inline void set(const LLUUID& id, bool archived)
{
    if (!eligible(id)) return;
    LLSD folders = gSavedPerAccountSettings.getLLSD(setting);
    if (archived) folders[id.asString()] = true;
    else folders.erase(id.asString());
    gSavedPerAccountSettings.setLLSD(setting, folders);
}
}
