/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_ASSISTANT_SOCIAL_MODEL_H
#define FS_ASSISTANT_SOCIAL_MODEL_H
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace fs_assistant_social
{
// Only the bridge's opaque reference belongs on the external wire. Native
// simulator login/session credentials are deliberately absent from this API.
inline std::map<std::string,std::string> publicSourceFields(const std::string& account,
    const std::string& viewerSession,const std::string& grid,std::uint64_t privacyEpoch)
{return {{"account_id",account},{"viewer_session",viewerSession},{"grid",grid},{"privacy_epoch",std::to_string(privacyEpoch)}};}
// Pure bounded storage used by the native adapter, not a second host cursor.
template<class T> class Stream
{
public:
    struct Entry { std::uint64_t id; T value; std::size_t bytes; };
    static constexpr std::size_t Capacity=512, ByteCapacity=512*1024;
    std::uint64_t append(T value,std::size_t bytes)
    {
        const auto id=++mLatest;
        if(bytes>ByteCapacity){clear();return id;}
        mEntries.push_back({id,std::move(value),bytes});mBytes+=bytes;
        while(mEntries.size()>Capacity || mBytes>ByteCapacity){mBytes-=mEntries.front().bytes;mEntries.pop_front();}
        return id;
    }
    void clear(){mEntries.clear();mBytes=0;} // privacy flush never rewinds IDs
    std::uint64_t latest()const{return mLatest;}
    std::uint64_t first()const{return mEntries.empty()?mLatest+1:mEntries.front().id;}
    bool gap(std::uint64_t after)const{return after<first()-1;}
    const std::deque<Entry>& entries()const{return mEntries;}
private:
    std::deque<Entry> mEntries;std::uint64_t mLatest=0;std::size_t mBytes=0;
};
struct Draft { std::string text;std::uint64_t revision=0; };
inline bool setDraft(Draft& draft,std::uint64_t expected,const std::string& text)
{if(expected!=draft.revision)return false;draft.text=text;++draft.revision;return true;}
inline bool liveMessage(bool hasHistory,int history,bool currentLogin)
{return currentLogin && hasHistory && history==0;}
inline bool reopens(bool incoming,std::uint64_t ingress,std::uint64_t closedAt)
{return incoming && ingress>closedAt;}
// Frozen native inputs include exact text and participants. Map comparison in
// the adapter additionally binds login, grid, native session type and readiness.
inline bool validText(const std::string& text)
{return !text.empty() && text.size()<=1023 && text.find('\0')==std::string::npos;}
struct Reader
{
    std::string binding;std::uint64_t epoch=0;std::set<std::string> topics;
    std::map<std::string,std::uint64_t> positions;double expires=0;
};
class Cursors
{
public:
    static constexpr std::size_t Capacity=128;
    void clear(){mReaders.clear();}
    void save(const std::string& opaque,Reader reader,double now)
    {
        for(auto it=mReaders.begin();it!=mReaders.end();)if(it->second.expires<=now)it=mReaders.erase(it);else ++it;
        if(mReaders.size()>=Capacity)
        {auto oldest=std::min_element(mReaders.begin(),mReaders.end(),[](const auto& a,const auto& b){return a.second.expires<b.second.expires;});mReaders.erase(oldest);}
        reader.expires=now+300;mReaders[opaque]=std::move(reader);
    }
    bool read(const std::string& opaque,const std::string& binding,std::uint64_t epoch,const std::set<std::string>& topics,double now,Reader& output)const
    {
        const auto found=mReaders.find(opaque);
        if(found==mReaders.end() || found->second.expires<=now || found->second.binding!=binding || found->second.epoch!=epoch || found->second.topics!=topics)return false;
        output=found->second;return true;
    }
private:std::map<std::string,Reader> mReaders;
};
}
#endif
