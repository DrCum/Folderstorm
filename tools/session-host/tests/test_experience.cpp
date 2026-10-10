#include "fssessionexperience.h"
#include "fssessionattentionnav.h"
#include <cassert>
int main()
{
    using namespace fs_session;HostExperience e,out;e.theme=1;e.pinned[3]=true;e.quick={240,201};e.keys[0]={112,6};
    const unsigned int stored=0x111016;const auto adjusted=visibleAccent(stored,0x111016,0xf5f3fa);assert(colorContrast(adjusted,0x111016)>=3. && stored==0x111016);
    assert(colorContrast(visibleAccent(0xffffff,0xf7f5fb,0x211a2b),0xf7f5fb)>=3.);assert(visibleAccent(0xbfa4fc,0x111016,0xf5f3fa)==0xbfa4fc);
    assert(HostExperience::decode(e.encode(),out));assert(out.theme==1 && out.pinned[3] && out.quick==e.quick && out.keys[0]==e.keys[0]);
    auto menus=e;menus.quick={SelectedActions,ViewActions,SettingsActions,SetActions,RecoverShell};assert(HostExperience::decode(menus.encode(),out) && out.quick==menus.quick);
    auto bytes=e.encode();assert(!HostExperience::decode(bytes+"x",out));assert(out.theme==1);assert(!HostExperience::decode(bytes.substr(0,17),out));
    e.quick.push_back(240);assert(e.encode().empty());e.quick={9999};assert(!e.valid());e.quick.clear();e.order[4]=0;assert(!e.valid());
    e=HostExperience{};e.keys[0]={49,5};assert(bindingsConflict(e,PresentationStore{}));
    Message a,b;a.worker[0]=1;a.generation=1;a.account="11111111-1111-1111-1111-111111111111";a.grid="grid";a.conversation="first";a.eventType=EventType::Conversation;a.eventAt=10;b=a;b.conversation="second";b.eventType=EventType::Attention;b.title="Teleport offer";b.eventAt=20;
    AttentionNavigator nav;nav.update({a,b,a},false);assert(nav.rows.size()==2 && nav.step(1)->conversation=="second");assert(nav.step(1)->conversation=="first");assert(nav.step(1)->conversation=="second");nav.update({a},false);assert(nav.step(1)->conversation=="first");nav.update({},false);assert(!nav.step(1));nav.update({b,a},true);assert(nav.rows.front().conversation=="first");
}
