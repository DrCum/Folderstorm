/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_OBJECT_TOOLS_MODEL_H
#define FS_OBJECT_TOOLS_MODEL_H
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs_object_tools {
struct Candidate {std::string id,asset;int type=0;};
struct Insertion {std::string id;bool ambiguous=false;};
inline Insertion correlateInsertion(const std::set<std::string>& before,const std::vector<Candidate>& after,const std::string& asset,int type)
{
    Insertion result;
    for(const auto& item:after)
    {
        if(before.count(item.id) || item.asset!=asset || item.type!=type)continue;
        if(!result.id.empty()){result.id.clear();result.ambiguous=true;return result;}
        result.id=item.id;
    }
    return result;
}
// Main-thread bounded storage. Reads never allocate mutation preparation slots.
template<class Payload> class Journal
{
public:
    struct Entry { std::uint64_t sequence; std::size_t bytes; Payload value; };
    explicit Journal(std::size_t entries=2048,std::size_t bytes=2*1024*1024)
        :mMaxEntries(entries),mMaxBytes(bytes){}
    void append(Payload value,std::size_t bytes)
    {
        ++mLast;
        if(bytes>mMaxBytes){mEntries.clear();mBytes=0;return;}
        while(!mEntries.empty() && (mEntries.size()>=mMaxEntries || mBytes+bytes>mMaxBytes))
        {mBytes-=mEntries.front().bytes;mEntries.pop_front();}
        mEntries.push_back({mLast,bytes,std::move(value)});mBytes+=bytes;
    }
    std::uint64_t latest()const{return mLast;}
    bool gap(std::uint64_t cursor)const{return cursor<(mEntries.empty()?mLast:mEntries.front().sequence-1);}
    const std::deque<Entry>& entries()const{return mEntries;}
    void clear(){mEntries.clear();mBytes=0;mLast=0;}
private:
    std::deque<Entry> mEntries;std::size_t mBytes=0,mMaxEntries,mMaxBytes;std::uint64_t mLast=0;
};

template<class Payload> class Receipts
{
public:
    struct Entry {std::string signature;Payload value;unsigned pending=0;bool terminal=false;};
    explicit Receipts(std::size_t limit=32):mLimit(limit){}
    Entry* find(const std::string& key)
    {const auto it=mEntries.find(key);return it==mEntries.end()?nullptr:&it->second;}
    const Entry* find(const std::string& key)const
    {const auto it=mEntries.find(key);return it==mEntries.end()?nullptr:&it->second;}
    Entry* admit(const std::string& key,const std::string& signature,Payload value)
    {
        if(auto* entry=find(key))return entry->signature==signature?entry:nullptr;
        // A retired receipt is not permission to replay an old mutation.
        if(mSeen.count(key) || mSeen.size()>=4096)return nullptr;
        for(auto it=mEntries.begin();it!=mEntries.end() && mEntries.size()>=mLimit;)
            if(it->second.terminal && !it->second.pending)it=mEntries.erase(it);else ++it;
        if(mEntries.size()>=mLimit)return nullptr;
        mSeen.insert(key);
        return &mEntries.emplace(key,Entry{signature,std::move(value),0,false}).first->second;
    }
    const std::map<std::string,Entry>& entries()const{return mEntries;}
    void clear(){mEntries.clear();mSeen.clear();}
private:
    std::size_t mLimit;std::map<std::string,Entry> mEntries;std::set<std::string> mSeen;
};
}
#endif
