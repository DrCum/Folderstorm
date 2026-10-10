/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_SETS_H
#define FS_SESSION_SETS_H
#include "fssessionstorecodec.h"
#include "fssessionregistry.h"
#include "fssessionmonitormodel.h"
namespace fs_session
{
struct SetProfile
{
    unsigned int slot=0; Mode standby=Mode::Warm; AccountKey expected;
    bool valid() const {return slot<MaxCharacters && (standby==Mode::Warm || standby==Mode::Economy) && ((expected.account.empty() && expected.grid.empty()) || expected.valid());}
};
struct SetMonitor
{
    unsigned int slot=0,size=0,rate=1; bool chat=false; AccountKey expected; ShellRect rect;
    bool valid() const {return slot<MaxCharacters && size<=2 && rate<=3 && expected.valid() && (rect.width==0 || rect.valid());}
};
struct SessionSet
{
    std::string name;
    bool captureProfiles=true,captureLayout=true,captureMonitors=false;
    std::vector<SetProfile> profiles;
    std::vector<SetMonitor> monitors;
    std::map<unsigned int,ShellRect> rectangles;
    unsigned int chrome=1; bool hosted=false,chat=true,chatDetached=false,controlsDetached=false;
    std::array<unsigned int,MaxCharacters> order{{0,1,2,3,4}};
    bool valid() const
    {
        if(name.empty() || name.size()>64 || !validUtf8(name) || name.find('\0')!=std::string::npos || name.find_first_of("\r\n\t")!=std::string::npos || profiles.size()>MaxCharacters || monitors.size()>MaxMonitors || rectangles.size()>3 || chrome>2)return false;
        std::set<unsigned int> slots;for(const auto& p:profiles)if(!p.valid() || !slots.insert(p.slot).second)return false;
        if(!captureProfiles && !profiles.empty())return false;
        std::set<unsigned int> monitorSlots;for(const auto& m:monitors)if(!m.valid() || !monitorSlots.insert(m.slot).second || (captureProfiles && !slots.count(m.slot)))return false;
        if(!captureMonitors && !monitors.empty())return false;
        for(const auto& r:rectangles)if(r.first>2 || !r.second.valid())return false;
        slots.clear();for(auto i:order)if(i>=MaxCharacters || !slots.insert(i).second)return false;
        return true;
    }
};
class SessionSetStore
{
public:
    static constexpr std::size_t MaxBytes=65536,Capacity=32;
    std::map<std::string,SessionSet> sets;
    bool put(const SessionSet& value,bool replace)
    {
        if(!value.valid() || (!replace && sets.count(value.name)) || (!sets.count(value.name) && sets.size()>=Capacity))return false;
        auto next=*this;next.sets[value.name]=value;if(next.encode().empty())return false;*this=std::move(next);return true;
    }
    std::string encode() const
    {
        if(sets.size()>Capacity) { return {}; }
        StoreWriter w("FSS1");w.number(static_cast<std::uint32_t>(sets.size()));
        for(const auto& entry:sets)
        {
            const auto& v=entry.second;if(entry.first!=v.name || !v.valid())return {};w.text(v.name);
            for(bool b:{v.captureProfiles,v.captureLayout,v.captureMonitors,v.hosted,v.chat,v.chatDetached,v.controlsDetached})w.number(b);
            w.number(v.chrome);for(auto n:v.order)w.number(n);w.number(static_cast<std::uint32_t>(v.profiles.size()));
            for(const auto& p:v.profiles){w.number(p.slot);w.number(static_cast<unsigned int>(p.standby));w.text(p.expected.account);w.text(p.expected.grid);}
            w.number(static_cast<std::uint32_t>(v.monitors.size()));for(const auto& m:v.monitors){w.number(m.slot);w.number(m.size);w.number(m.rate);w.number(m.chat);w.text(m.expected.account);w.text(m.expected.grid);w.rect(m.rect);}
            w.number(static_cast<std::uint32_t>(v.rectangles.size()));for(const auto& r:v.rectangles){w.number(r.first);w.rect(r.second);}
        }
        return w.data.size()<=MaxBytes?w.data:std::string{};
    }
    static bool decode(const std::string& data,SessionSetStore& output)
    {
        StoreReader r(data,"FSS1",MaxBytes);SessionSetStore store;const auto count=r.number();if(count>Capacity)return false;
        const auto boolean=[&r](bool& b){const auto n=r.number();if(n>1)r.ok=false;b=n!=0;};
        for(unsigned int i=0;i<count;++i)
        {
            SessionSet v;v.name=r.text(64);boolean(v.captureProfiles);boolean(v.captureLayout);boolean(v.captureMonitors);boolean(v.hosted);boolean(v.chat);boolean(v.chatDetached);boolean(v.controlsDetached);v.chrome=r.number();for(auto& n:v.order)n=r.number();
            auto n=r.number();if(n>MaxCharacters)return false;while(n--){SetProfile p;p.slot=r.number();p.standby=static_cast<Mode>(r.number());p.expected.account=r.text(64);p.expected.grid=r.text(127);v.profiles.push_back(p);}
            n=r.number();if(n>MaxMonitors)return false;while(n--){SetMonitor m;m.slot=r.number();m.size=r.number();m.rate=r.number();boolean(m.chat);m.expected.account=r.text(64);m.expected.grid=r.text(127);m.rect=r.rect();v.monitors.push_back(m);}
            n=r.number();if(n>3)return false;while(n--){const auto id=r.number();if(v.rectangles.count(id))return false;v.rectangles[id]=r.rect();}
            if(!v.valid() || !store.sets.emplace(v.name,std::move(v)).second)return false;
        }
        if(!r.done()) { return false; }
        output=std::move(store);return true;
    }
};
// One explicit opening intent; invalidating the token revokes every late follow-up.
class SessionSetOpen
{
public:
    std::uint64_t token=0,deadline=0;bool active=false,layoutApplied=false;
    SessionSet selected;
    std::array<bool,MaxCharacters> attempted{},finished{};
    std::array<WorkerId,MaxCharacters> workers{};
    std::array<std::uint64_t,MaxCharacters> generations{};
    std::array<bool,MaxMonitors> monitorDone{};
    std::vector<std::string> skipped;
    void cancel(){++token;active=false;}
    std::uint64_t begin(const SessionSet& set,std::uint64_t now){cancel();selected=set;deadline=now+300000;active=true;layoutApplied=false;attempted.fill(false);finished.fill(false);workers={};generations.fill(0);monitorDone.fill(false);skipped.clear();return token;}
    bool owns(std::uint64_t id,std::uint64_t now)const{return active && token==id && now<deadline;}
    bool accept(const SetProfile& p,const Message& live)const
    {return workers[p.slot]==live.worker && (!generations[p.slot] || generations[p.slot]==live.generation) && (p.expected.account.empty() || p.expected==AccountKey::from(live));}
};
}
#endif
