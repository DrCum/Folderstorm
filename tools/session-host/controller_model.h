/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_CONTROLLER_MODEL_H
#define FS_HOST_CONTROLLER_MODEL_H
#include "fssessionstorecodec.h"
#include "fssessionregistry.h"
#include <map>
#include <set>
#include <vector>

// Controller authority never comes from a viewer's Local Assistant grant.
namespace fs_controller {
using fs_session::StoreReader;
using fs_session::StoreWriter;
constexpr std::size_t RequestBytes=4096, ResponseBytes=65536, ReceiptCapacity=128;
enum class Level : unsigned int { Deny, Ask, Allow };
enum class Permission : unsigned int { Metadata, Activate, Background, Monitors, OpenLogin, Restart, Logout, Detach, Count };
enum class Op : unsigned int { List=1, Status, Receipt, Activate, Background, OpenLogin, Restart, Logout, Detach,
    MonitorList, MonitorOpen, MonitorClose, MonitorOpenAll, MonitorArrange, CycleConfigure, CyclePause, CycleNext };
inline bool validOp(Op op){return op>=Op::List && op<=Op::CycleNext;}
inline bool readOp(Op op){return op==Op::List || op==Op::Status || op==Op::Receipt || op==Op::MonitorList;}
inline Permission permission(Op op)
{
    switch(op){case Op::Activate:return Permission::Activate;case Op::Background:return Permission::Background;
    case Op::OpenLogin:return Permission::OpenLogin;case Op::Restart:return Permission::Restart;
    case Op::Logout:return Permission::Logout;case Op::Detach:return Permission::Detach;
    case Op::MonitorOpen:case Op::MonitorClose:case Op::MonitorOpenAll:case Op::MonitorArrange:
    case Op::CycleConfigure:case Op::CyclePause:case Op::CycleNext:return Permission::Monitors;
    default:return Permission::Metadata;}
}
inline const char* opName(Op op)
{
    static constexpr const char* names[]={"invalid","sessions_list","session_status","controller_operation_status",
        "session_activate","session_background_mode","session_open_login","session_restart","session_logout","session_detach",
        "monitor_list","monitor_open","monitor_close","monitors_open_all","monitors_arrange","monitor_cycle_configure","monitor_cycle_pause","monitor_cycle_next"};
    return validOp(op)?names[static_cast<unsigned int>(op)]:names[0];
}
inline bool hex(const std::string& value,std::size_t size)
{return value.size()==size && std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
inline bool constantEqual(const std::string& a,const std::string& b)
{if(a.size()!=b.size())return false;unsigned int different=0;for(std::size_t i=0;i<a.size();++i)different|=static_cast<unsigned char>(a[i])^static_cast<unsigned char>(b[i]);return different==0;}
inline void u64(StoreWriter& w,std::uint64_t n){w.number(static_cast<std::uint32_t>(n));w.number(static_cast<std::uint32_t>(n>>32));}
inline std::uint64_t u64(StoreReader& r){const auto lo=r.number();const auto hi=r.number();return lo|(static_cast<std::uint64_t>(hi)<<32);}
struct Profile {std::string id;std::uint64_t revision=1;bool enabled=false;};
struct Config
{
    bool enabled=false;
    std::uint64_t revision=1;
    std::array<Level,static_cast<unsigned int>(Permission::Count)> levels{{Level::Deny,Level::Ask,Level::Ask,Level::Ask,Level::Ask,Level::Ask,Level::Ask,Level::Ask}};
    std::array<Profile,fs_session::MaxCharacters> profiles{};
    Level level(Op op) const{return levels[static_cast<unsigned int>(permission(op))];}
    std::string encode() const
    {
        if(!revision)return {};
        StoreWriter w("FCT1");w.number(enabled?1:0);u64(w,revision);
        for(auto level:levels){if(level>Level::Allow)return {};w.number(static_cast<unsigned int>(level));}
        if(levels[0]==Level::Ask)return {};
        std::set<std::string> ids;
        for(const auto& p:profiles){if(!hex(p.id,32)||!p.revision||!ids.insert(p.id).second)return {};w.text(p.id);u64(w,p.revision);w.number(p.enabled?1:0);}
        return w.data;
    }
    static bool decode(const std::string& data,Config& output)
    {
        StoreReader r(data,"FCT1",8192);Config c;const auto enabled=r.number();c.enabled=enabled==1;c.revision=u64(r);
        for(auto& level:c.levels)level=static_cast<Level>(r.number());
        for(auto& p:c.profiles){p.id=r.text(32);p.revision=u64(r);const auto e=r.number();if(e>1)return false;p.enabled=e==1;}
        if(enabled>1||!r.done()||c.encode().empty())return false;
        output=std::move(c);return true;
    }
};
struct Source
{
    std::string profile,worker,viewerSession,account,grid;
    std::uint64_t revision=0,generation=0;
    bool running=false;
    bool operator==(const Source& other) const
    {return profile==other.profile&&revision==other.revision&&running==other.running&&worker==other.worker&&generation==other.generation&&viewerSession==other.viewerSession&&account==other.account&&grid==other.grid;}
};
struct Request
{
    std::string instance,token,id,profile,worker,viewerSession;
    std::uint64_t revision=0,generation=0;
    Op op=Op::List;unsigned int mode=0,monitor=0,seconds=10;bool row=false,playing=false;
    std::string encode(bool authentication=true) const
    {
        StoreWriter w("FSC9");w.number(1);w.text(instance);w.text(authentication?token:std::string());w.text(id);w.number(static_cast<unsigned int>(op));
        w.text(profile);u64(w,revision);w.text(worker);u64(w,generation);w.text(viewerSession);
        w.number(mode);w.number(monitor);w.number(seconds);w.number(row?1:0);w.number(playing?1:0);return w.data;
    }
    static bool decode(const std::string& data,Request& output)
    {
        StoreReader r(data,"FSC9",RequestBytes-4);Request q;const auto version=r.number();q.instance=r.text(32);q.token=r.text(64);q.id=r.text(64);q.op=static_cast<Op>(r.number());
        q.profile=r.text(32);q.revision=u64(r);q.worker=r.text(32);q.generation=u64(r);q.viewerSession=r.text(128);
        q.mode=r.number();q.monitor=r.number();q.seconds=r.number();const auto row=r.number(),playing=r.number();q.row=row==1;q.playing=playing==1;
        if(!r.done()||version!=1||!hex(q.instance,32)||!hex(q.token,64)||!validOp(q.op)||q.mode>1||q.monitor>=5||q.seconds<2||q.seconds>3600||row>1||playing>1)return false;
        if(!q.profile.empty()&&!hex(q.profile,32))return false;
        if(!q.worker.empty()&&!hex(q.worker,32))return false;
        output=std::move(q);return true;
    }
};
// Timestamp is part of the immutable request ID, so an evicted old request
// cannot be reinterpreted as a fresh mutation. Retention exceeds admission age.
inline std::uint64_t requestTime(const std::string& id)
{
    const auto dash=id.find('-');if(dash<10||dash>16||!hex(id.substr(dash+1),32))return 0;
    std::uint64_t n=0;for(std::size_t i=0;i<dash;++i){if(id[i]<'0'||id[i]>'9'||n>999999999999999ULL)return 0;n=n*10+static_cast<unsigned int>(id[i]-'0');}return n;
}
inline bool freshRequest(const std::string& id,std::uint64_t now)
{const auto issued=requestTime(id);return issued&&issued<=now+5000&&now<=issued+60000;}
struct Receipt
{
    Request request;std::vector<Source> sources;std::uint64_t policy=0,created=0,deadline=0;
    std::string state="pending_approval",error,nativeSnapshot,selectedTarget;bool submitted=false;
    bool terminal() const{return state=="completed"||state=="submitted"||state=="refused"||state=="cancelled"||state=="uncertain";}
};
class Operations
{
public:
    std::map<std::string,Receipt> receipts;
    Receipt* find(const std::string& id){const auto it=receipts.find(id);return it==receipts.end()?nullptr:&it->second;}
    // Duplicate lookup precedes fresh admission; never replay a submitted ID.
    Receipt* admit(const Request& request,const std::vector<Source>& sources,std::uint64_t policy,std::uint64_t now,std::string& error)
    {
        if(auto* old=find(request.id)){if(old->request.encode(false)!=request.encode(false)){error="request_id_conflict";return nullptr;}return old;}
        if(!freshRequest(request.id,now)){error="request_expired";return nullptr;}
        for(auto it=receipts.begin();it!=receipts.end();)if(it->second.terminal()&&now>it->second.created+600000)it=receipts.erase(it);else ++it;
        if(receipts.size()>=ReceiptCapacity){error="receipt_capacity";return nullptr;}
        for(const auto& entry:receipts)if(!entry.second.terminal())
        {
            if(request.op==Op::Activate&&entry.second.request.op==Op::Activate){error="busy";return nullptr;}
            for(const auto& source:sources)for(const auto& other:entry.second.sources)if(source.profile==other.profile)
            {if(request.op==Op::Detach&&entry.second.request.op==Op::Restart)continue;error="busy";return nullptr;}
        }
        Receipt next;next.request=request;next.request.token.clear();next.sources=sources;next.policy=policy;next.created=now;next.deadline=now+70000;
        return &receipts.emplace(request.id,std::move(next)).first->second;
    }
    template<class SourceAt> bool allowed(Receipt& receipt,const Config& config,std::uint64_t now,SourceAt sourceAt,bool allowExited=false)
    {
        if(receipt.terminal())return false;
        std::string error;
        if(!config.enabled||receipt.policy!=config.revision||config.level(receipt.request.op)==Level::Deny)error="permission_changed";
        else if(now>=receipt.deadline)error="deadline_expired";
        for(const auto& source:receipt.sources)
        {
            const auto current=sourceAt(source.profile);bool opted=false;
            for(const auto& p:config.profiles)if(p.id==source.profile&&p.enabled&&p.revision==source.revision)opted=true;
            if(!opted)error="profile_changed";
            auto expected=source;
            if(allowExited&&!current.running&&expected.running){expected.running=false;}
            if(!(expected==current))error="session_changed";
        }
        if(error.empty())return true;
        receipt.error=error;receipt.state=receipt.submitted?"uncertain":"cancelled";return false;
    }
    void revoke(const std::string& reason)
    {for(auto& entry:receipts)if(!entry.second.terminal()){entry.second.error=reason;entry.second.state=entry.second.submitted?"uncertain":"cancelled";}}
};
inline std::string quote(const std::string& text)
{
    std::string out="\"";const char* digits="0123456789abcdef";
    for(unsigned char c:text){if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}else if(c<32){out+="\\u00";out+=digits[c>>4];out+=digits[c&15];}else out+=static_cast<char>(c);}return out+'"';
}
inline std::string errorJSON(const std::string& code){return "{\"error_code\":"+quote(code)+",\"error\":"+quote(code)+"}";}
inline std::string receiptJSON(const Receipt& r,const std::string& instance)
{return "{\"controller_instance\":"+quote(instance)+",\"request_id\":"+quote(r.request.id)+",\"profile_id\":"+quote(r.request.profile)+",\"operation\":"+quote(opName(r.request.op))+",\"state\":"+quote(r.state)+",\"submitted\":"+(r.submitted?"true":"false")+",\"error_code\":"+quote(r.error)+"}";}
}
#endif
