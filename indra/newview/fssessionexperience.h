/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_EXPERIENCE_H
#define FS_SESSION_EXPERIENCE_H
#include <cmath>
#include "fssessionstorecodec.h"
#include "fssessionregistry.h"
namespace fs_session
{
inline double colorLuminance(unsigned int rgb)
{
    const auto linear=[](unsigned int n){const double c=static_cast<double>(n)/255.;return c<=0.04045?c/12.92:std::pow((c+0.055)/1.055,2.4);};
    return 0.2126*linear((rgb>>16)&255)+0.7152*linear((rgb>>8)&255)+0.0722*linear(rgb&255);
}
inline double colorContrast(unsigned int a,unsigned int b)
{const double x=colorLuminance(a),y=colorLuminance(b);return ((std::max)(x,y)+0.05)/((std::min)(x,y)+0.05);}
inline unsigned int visibleAccent(unsigned int stored,unsigned int background,unsigned int foreground)
{
    if(colorContrast(stored,background)>=3.)return stored;
    for(unsigned int step=1;step<=10;++step)
    {
        unsigned int rgb=0;for(unsigned int shift:{0u,8u,16u})rgb|=((((stored>>shift)&255)*(10-step)+((foreground>>shift)&255)*step)/10)<<shift;
        if(colorContrast(rgb,background)>=3.)return rgb;
    }
    return foreground;
}
enum ShellCommand : unsigned int { RestoreShell=140, RecoverShell=141, MonitorSelected=150, NextAttention=151, PreviousAttention=152, Details=153, AppearanceSettings=154, QuickSettings=155, SelectedActions=156, ViewActions=157, SettingsActions=158, SetActions=159, AttentionSettings=160 };
struct ShellCommandInfo { unsigned int id; const wchar_t* label; const wchar_t* tip; const wchar_t* icon; };
inline const std::vector<ShellCommandInfo>& shellCommands()
{
    static const std::vector<ShellCommandInfo> commands{
        {RestoreShell,L"Show controls",L"Restore condensed host controls without changing characters",L"▣"},
        {MonitorSelected,L"Monitor",L"Open a read-only monitor for the selected inactive character",L"▤"},
        {240,L"Restart",L"Restart the selected character through normal logout and login",L"↻"},
        {201,L"Workspaces",L"Open the selected character's workspace controls",L"▦"},
        {204,L"Voice",L"Toggle voice following the active character",L"♪"},
        {205,L"Mute",L"Toggle background world and media sound muting",L"♫"},
        {208,L"Skip",L"Skip the current visual transition",L"»"},
        {Details,L"Details",L"Show resource measurements and connection diagnostics",L"≡"},
        {NextAttention,L"Next",L"Go to the next eligible conversation or attention item",L"›"},
        {PreviousAttention,L"Previous",L"Go to the previous eligible attention item",L"‹"},
        {274,L"Attention",L"Open the attention inbox",L"!"},
        {206,L"Save choices",L"Persist host appearance and presentation choices",L"✓"},
        {270,L"Pop-out Chat",L"Dock or detach shared Chat",L"↗"},
        {271,L"Pop-out controls",L"Dock or detach character controls",L"↗"}};
    return commands;
}
inline const ShellCommandInfo* shellCommand(unsigned int id)
{ for(const auto& c:shellCommands()) if(c.id==id) return &c; return nullptr; }
struct HostExperience
{
    static constexpr std::size_t MaxBytes=65536;
    unsigned int theme=0,density=0,labels=0,attentionMask=29,accountMask=31;
    bool cards=true,diagnostics=false,oldestFirst=false,viewerShellKeys=false;
    std::array<bool,8> pinned{};
    std::array<unsigned int,MaxCharacters> order{{0,1,2,3,4}};
    std::vector<unsigned int> quick;
    std::array<ShortcutChord,3> keys{}; // Host-local recovery/next/previous; no defaults.
    ShellRect attentionRect;
    bool valid() const
    {
        if(theme>2 || density>1 || labels>2 || attentionMask>31 || accountMask>31 || pinned[0] || quick.size()>12) return false;
        std::set<unsigned int> seen;
        for(auto id:quick) if(!shellCommand(id) || !seen.insert(id).second) return false;
        seen.clear(); for(auto id:order) if(id>=MaxCharacters || !seen.insert(id).second) return false;
        for(std::size_t i=0;i<keys.size();++i)
        { if(!keys[i].valid()) return false; for(std::size_t j=0;j<i;++j) if(keys[i].key && keys[i]==keys[j]) return false; }
        return attentionRect.width==0 ? attentionRect.height==0 && attentionRect.x==0 && attentionRect.y==0 && attentionRect.dpi==96 : attentionRect.valid();
    }
    std::string encode() const
    {
        if(!valid()) { return {}; }
        StoreWriter w("FSX1");
        for(auto n:{theme,density,labels,attentionMask,accountMask}) w.number(n);
        w.number(cards); w.number(diagnostics); w.number(oldestFirst);w.number(viewerShellKeys);
        for(bool b:pinned) w.number(b);
        for(auto n:order) w.number(n);
        w.number(static_cast<std::uint32_t>(quick.size())); for(auto n:quick) w.number(n);
        for(const auto& k:keys) { w.number(k.key); w.number(k.modifiers); }
        w.rect(attentionRect); return w.data;
    }
    static bool decode(const std::string& data,HostExperience& output)
    {
        StoreReader r(data,"FSX1",MaxBytes); HostExperience v;
        v.theme=r.number();v.density=r.number();v.labels=r.number();v.attentionMask=r.number();v.accountMask=r.number();
        const auto boolean=[&r](bool& b){const auto n=r.number();if(n>1)r.ok=false;b=n!=0;};
        boolean(v.cards);boolean(v.diagnostics);boolean(v.oldestFirst);boolean(v.viewerShellKeys);for(auto& b:v.pinned)boolean(b);
        for(auto& n:v.order)n=r.number();
        auto count=r.number();if(count>12)return false;v.quick.clear();while(count--)v.quick.push_back(r.number());
        for(auto& k:v.keys){k.key=r.number();k.modifiers=r.number();}v.attentionRect=r.rect();
        if(!r.done() || !v.valid()) { return false; }
        output=std::move(v);return true;
    }
};
inline bool bindingsConflict(const HostExperience& e,const PresentationStore& p)
{ for(const auto& k:e.keys)if(k.key)for(const auto& other:p.bindings)if(k==other)return true;return false; }
}
#endif
