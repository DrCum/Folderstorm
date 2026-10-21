/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fscontentreadpolicy.h"
#include "fsassistantasync.h"
#include <cassert>
#include <iostream>
int main()
{
 using namespace fs_content;using namespace fs_assistant;
 for(bool card:{false,true})for(bool library:{false,true})for(bool copy:{false,true})for(bool modify:{false,true})
 {
  ReadAuthority native{card,!library,library,copy,modify,false,false,false};
  assert(nativeContentReadable(native)==(copy && (card || library || modify)));
  native.restrictionsEnabled=true;native.viewNoteRestricted=card;native.viewScriptRestricted=!card;
  assert(!nativeContentReadable(native));
  native.viewNoteRestricted=!card;native.viewScriptRestricted=card;
  assert(nativeContentReadable(native)==(copy && (card || library || modify)));
 }
 assert(!nativeContentReadable({true,false,false,true,true,false,false,false}));
 for(bool card:{false,true})
 {
  ReadAuthority authority{card,true,false,true,true,true,false,false};
  auto guard=std::make_shared<AsyncOperation>([](){return std::string();});
  guard->checkWith([&](){return nativeContentReadable(authority)?std::string():std::string("native_content_restricted");});
  auto task=std::make_shared<AsyncTask>(guard);assert(asyncTaskAllowed(task)); // fetch
  authority.viewNoteRestricted=card;authority.viewScriptRestricted=!card;
  int privateDeliveries=0;
  asyncDeliver(task,"asset",[&](bool allowed){if(allowed && nativeContentReadable(authority))++privateDeliveries;});
  assert(privateDeliveries==0); // delivery cannot bypass a newly enabled restriction
  authority.viewNoteRestricted=false;authority.viewScriptRestricted=false;
  assert(!asyncTaskAllowed(task)); // stopped operation never regains authority
 }
 std::cout<<"content read permissions passed\n";
}
