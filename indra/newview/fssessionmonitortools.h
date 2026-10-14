/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_MONITOR_TOOLS_H
#define FS_SESSION_MONITOR_TOOLS_H
#include "fssessionmonitorchat.h"
#include <vector>
namespace fs_session
{
struct CyclingMonitor
{
    enum class Action { Keep, Disable, Advance };
    bool playing=true;
    unsigned int seconds=10;
    std::uint64_t due=0;
    static bool validInterval(unsigned int n){return n>=2 && n<=3600;}
    void pause(){playing=false;due=0;}
    void play(std::uint64_t now){playing=true;due=now+static_cast<std::uint64_t>(seconds)*1000;}
    void enteredComposer(bool requirePause){if(!requirePause)pause();}
    bool chatAllowed(bool requirePause) const{return !playing || !requirePause;}
    template<class Eligible> int next(int current,int direction,const std::array<unsigned int,MaxCharacters>& order,Eligible eligible) const
    {
        auto found=std::find(order.begin(),order.end(),static_cast<unsigned int>(current));
        int at=found==order.end()?(direction<0?0:MaxCharacters-1):static_cast<int>(found-order.begin());
        for(int n=0;n<MaxCharacters;++n){at=(at+(direction<0?MaxCharacters-1:1))%MaxCharacters;const int slot=static_cast<int>(order[at]);if(eligible(slot))return slot;}
        return -1;
    }
    bool elapsed(std::uint64_t now){if(!playing)return false;if(!due){play(now);return false;}if(now<due)return false;play(now);return true;}
    Action action(bool sourceValid,bool visible,std::uint64_t now)
    {
        if(!sourceValid)return playing?Action::Advance:Action::Disable;
        return visible && elapsed(now)?Action::Advance:Action::Keep;
    }
};
inline bool cyclingComposerInputAllowed(const CyclingMonitor& cycle,bool focused,const MonitorChat& chat,
    const MonitorBinding& binding,const Message& source,const ChatView& view)
{
    // A rebind alone cannot authorize queued typing/Enter/IME input. A fresh
    // focus in a paused composer is also required for the displayed destination.
    return !cycle.playing && focused && chat.request(Kind::SendChat,binding,source,view).has_value();
}
struct MonitorTile { int x=0,y=0,width=0,height=0; };
struct MonitorMinimum { int width=0,height=0; };
inline MonitorMinimum monitorMinimum(bool chat,bool cycling,unsigned int effectiveDpi)
{
    if(!effectiveDpi || effectiveDpi>10000)return {};
    const auto scale=[effectiveDpi](unsigned int value)
    {return static_cast<int>((static_cast<std::uint64_t>(value)*effectiveDpi+48)/96);};
    // Match native MulDiv rounding, including the host's larger-text scale.
    return {scale(chat?320u:280u),scale((chat?392u:120u)+(cycling?32u:0u))};
}
inline bool tilesRespectMinimums(const std::vector<MonitorTile>& tiles,const std::vector<MonitorMinimum>& minimums)
{
    if(tiles.empty() || tiles.size()!=minimums.size())return false;
    for(std::size_t i=0;i<tiles.size();++i)
        if(minimums[i].width<1 || minimums[i].height<1 || tiles[i].width<minimums[i].width || tiles[i].height<minimums[i].height)return false;
    return true;
}
inline std::vector<MonitorTile> monitorTiles(MonitorTile work,unsigned int count,bool row,int gap=8)
{
    if(!count || count>MonitorViews || work.width<1 || work.height<1 || gap<0)return {};
    unsigned int columns=count;
    if(!row){columns=1;while(columns*columns<count)++columns;}
    const unsigned int rows=(count+columns-1)/columns;
    const int usableWidth=work.width-gap*static_cast<int>(columns-1),usableHeight=work.height-gap*static_cast<int>(rows-1);
    if(usableWidth<static_cast<int>(columns) || usableHeight<static_cast<int>(rows))return {};
    std::vector<MonitorTile> tiles;
    for(unsigned int i=0;i<count;++i)
    {
        const int col=static_cast<int>(i%columns),r=static_cast<int>(i/columns),cols=static_cast<int>(columns),rs=static_cast<int>(rows);
        const int x=usableWidth*col/cols,y=usableHeight*r/rs;
        tiles.push_back({work.x+x+gap*col,work.y+y+gap*r,usableWidth*(col+1)/cols-x,usableHeight*(r+1)/rs-y});
    }
    return tiles;
}
}
#endif
