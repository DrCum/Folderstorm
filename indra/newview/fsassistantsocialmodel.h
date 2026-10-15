/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_ASSISTANT_SOCIAL_MODEL_H
#define FS_ASSISTANT_SOCIAL_MODEL_H
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>
namespace fs_assistant_social
{
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
{return currentLogin && (!hasHistory || history==0);}
// Frozen native inputs include exact text and participants. Map comparison in
// the adapter additionally binds login, grid, native session type and readiness.
inline bool validText(const std::string& text)
{return !text.empty() && text.size()<=1023 && text.find('\0')==std::string::npos;}
}
#endif
