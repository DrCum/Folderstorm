/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsassistantfeatures.h"
#include "fsassistantpermissions.h"
#include "fsassistantasync.h"
#include "fsassistantsession.h"
#include <cassert>
#include <set>
#include <string>
int main()
{
 using namespace fs_assistant;
 std::set<std::string> keys;
 for(const auto& op:featureOperations)
 {
  assert(keys.insert(std::string(op.api)+"/"+op.op).second);
  assert(featureOperation(op.api,op.op)==&op);
  assert(requiresAsyncAuthority(op.api,op.op,op.readOnly));
  if(*op.permission)
  {
   bool known=false;for(const auto& def:permissionClasses)if(std::string(op.permission)==def.id)known=true;
   assert(known);
  }
 }
 for(const char* api:{"LLAgent","LLViewerControl","UI","LLWindow"})assert(!featureAPI(api));
 for(const char* op:{"requestTeleport","startAutoPilot","requestSit","requestTouch","set","toggle","execute"})assert(!featureOperation("FSWorld",op));
 assert(std::string(featureOperation("FSChat","draftGet")->permission)=="chat_read");
 assert(std::string(featureOperation("FSPose","sharedEdit")->capability)=="shared_pose_tools");
 assert(needsActiveCharacter("FSGestures","play") && needsActiveCharacter("FSPose","update"));
 assert(!needsActiveCharacter("FSWorld","location") && !needsActiveCharacter("FSChat","sendIM"));
}
