/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_ASSISTANT_ASSET_FETCH_H
#define FS_ASSISTANT_ASSET_FETCH_H
#include "fsassistantasync.h"
#include <cstdint>
namespace fs_assistant {
struct AssetReadSource {
    std::string account,login,grid,region;
    std::uint64_t regionGeneration=0;
    bool connected=false;
    bool operator==(const AssetReadSource& other)const
    {return connected && other.connected && account==other.account && login==other.login && grid==other.grid &&
        region==other.region && regionGeneration==other.regionGeneration;}
};
class AssetReadAuthority {
public:
    AssetReadAuthority(AssetReadSource source,AsyncTaskPtr task):mSource(std::move(source)),mTask(std::move(task)){}
    bool allowed(const AssetReadSource& current)const
    {
        if(!(mSource==current))
        {if(mTask && mTask->guard())mTask->guard()->cancel("asset_source_changed");return false;}
        return asyncTaskAllowed(mTask);
    }
    const AsyncTaskPtr& task()const{return mTask;}
private:AssetReadSource mSource;AsyncTaskPtr mTask;
};
}
#endif
