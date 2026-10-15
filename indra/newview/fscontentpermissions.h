/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_CONTENT_PERMISSIONS_H
#define FS_CONTENT_PERMISSIONS_H
#include "llagent.h"
#include "llinventorymodel.h"
#include "llviewerinventory.h"
#include "llviewernetwork.h"
#include "llsdutil.h"
#include <openssl/sha.h>
#include <string>
namespace fs_content {
inline constexpr std::size_t textLimit=65536, assetLimit=262144;
inline std::string hash(const std::string& value)
{
    unsigned char bytes[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(value.data()),value.size(),bytes);
    static const char digits[]="0123456789abcdef";
    std::string result;result.reserve(64);
    for(auto byte:bytes){result+=digits[byte>>4];result+=digits[byte&15];}
    return result;
}
inline LLSD identity()
{
    return llsd::map("account",gAgent.getID(),"session",gAgent.getSessionID(),
                     "grid",LLGridManager::instance().getGrid());
}
inline bool library(const LLUUID& id)
{return gInventory.getLibraryRootFolderID().notNull() && gInventory.isObjectDescendentOf(id,gInventory.getLibraryRootFolderID());}
inline bool agentInventory(const LLUUID& id)
{return gInventory.getRootFolderID().notNull() && gInventory.isObjectDescendentOf(id,gInventory.getRootFolderID());}
inline bool readable(const LLViewerInventoryItem* item,bool notecard)
{
    if(!item || !item->isFinished() || item->getIsLinkType() || item->getAssetUUID().isNull() ||
       gAgent.getID().isNull() || gAgent.getSessionID().isNull() ||
       (!agentInventory(item->getUUID()) && !library(item->getUUID())) ||
       item->getType()!=(notecard?LLAssetType::AT_NOTECARD:LLAssetType::AT_LSL_TEXT))return false;
    const auto& permissions=item->getPermissions();
    // Match native COPY / COPY+MODIFY, including its Library exception.
    // Assistant access never broadens this through a godlike override.
    return gAgent.allowOperation(PERM_COPY,permissions,GP_OBJECT_MANIPULATE) &&
        (notecard || library(item->getUUID()) || gAgent.allowOperation(PERM_MODIFY,permissions,GP_OBJECT_MANIPULATE));
}
inline bool writable(const LLViewerInventoryItem* item,bool notecard)
{return readable(item,notecard) && agentInventory(item->getUUID()) && !library(item->getUUID()) &&
    item->getPermissions().getOwner()==gAgent.getID() &&
    gAgent.allowOperation(PERM_MODIFY,item->getPermissions(),GP_OBJECT_MANIPULATE);}
inline LLSD evidence(const LLViewerInventoryItem* item)
{
    LLSD result=identity();
    if(item){result["item_id"]=item->getUUID();result["asset_id"]=item->getAssetUUID();
        result["owner"]=item->getPermissions().getOwner();result["parent_id"]=item->getParentUUID();
        result["type"]=static_cast<S32>(item->getType());result["permissions"]=item->getPermissions().getMaskOwner();
        result["group_permissions"]=item->getPermissions().getMaskGroup();result["everyone_permissions"]=item->getPermissions().getMaskEveryone();}
    return result;
}
}
#endif
