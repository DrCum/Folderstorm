/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsposelistener.h"
#include "fsassistantfeature.h"
#include "fsposepolicy.h"
#include "fsposexml.h"
#include "fsposefile.h"
#include "fsposeranimator.h"
#include "fssharedposemanager.h"
#include "fssessionworker.h"
#include "llagent.h"
#include "llapp.h"
#include "llcallbacklist.h"
#include "lldir.h"
#include "llfile.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "llstartup.h"
#include "llstring.h"
#include "llviewerregion.h"
#include "llviewernetwork.h"
#include "llvoavatarself.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <openssl/sha.h>

namespace {
using namespace fs_pose;
FSPoserAnimator sAnimator;
struct Session
{
 LLUUID id=LLUUID::generateNewID(),motion,operation;
 LLSD authority;Transaction transaction;
 fs_assistant::AsyncTaskPtr task;
 bool active=false,restorationComplete=true;std::string stopped;
};
Session sSession;
struct Receipt {LLSD identity,request,result;std::string signature;};
std::map<LLUUID,Receipt> sReceipts;
std::string hash(const std::string& value)
{
 unsigned char bytes[SHA256_DIGEST_LENGTH];SHA256(reinterpret_cast<const unsigned char*>(value.data()),value.size(),bytes);
 static const char hex[]="0123456789abcdef";std::string result;
 for(auto byte:bytes){result+=hex[byte>>4];result+=hex[byte&15];}return result;
}
LLSD identity(const LLSD& request)
{
 return llsd::map("account",gAgentID,"login",gAgent.getSessionID(),"grid",LLGridManager::instance().getGrid(),
  "region",gAgent.getRegion()?gAgent.getRegion()->getRegionID():LLUUID::null,"worker",FSSessionWorker::assistantIdentity(),"epoch",request["__assistant_epoch"]);
}
void syncSession(const LLSD& request)
{
 if(sSession.authority.isDefined() && sSession.authority!=identity(request))
 {
  // Discard private snapshots without performing a mutation during Ask
  // preparation. The old motion's nonce-bound per-frame authority now fails
  // and the independent idle tick releases only its owned rotations.
  sSession=Session();sReceipts.clear();
 }
}
bool liveSelf(const LLSD& request,std::string& error,bool mutation)
{
 if(request["target"].asUUID()!=gAgentID || gAgentID.isNull() || !isAgentAvatarValid() || gAgentAvatarp->getID()!=gAgentID ||
  gAgentAvatarp->isDead() || gAgentAvatarp->isControlAvatar() || !gAgent.getRegion() || gAgentAvatarp->getRegion()!=gAgent.getRegion() ||
  gDisconnected || LLApp::isExiting() || LLStartUp::getStartupState()!=STATE_STARTED)
 {error="An explicit current-account ordinary avatar target in the current region is required";return false;}
 if(mutation && (!FSSessionWorker::assistantActiveAllowed() || FSSessionWorker::assistantPaused() || gAgent.getTeleportState()!=LLAgent::TELEPORT_NONE))
 {error="RequiresActive: finish the character transition or teleport before posing";return false;}
 return true;
}
LLSD rotationValue(const Rotations& rotations)
{
 LLSD result=LLSD::emptyMap();for(const auto& entry:rotations){LLSD q=LLSD::emptyArray();for(auto value:entry.second)q.append(value);result[entry.first]=q;}return result;
}
bool readRotations(const LLSD& value,Rotations& result,std::string& error)
{
 if(!value.isMap() || !value.size() || value.size()>static_cast<S32>(jointLimit)){error="rotations must contain 1 to 64 exact joints";return false;}
 for(auto entry=value.beginMap();entry!=value.endMap();++entry)
 {
  const auto* joint=sAnimator.getPoserJointByName(entry->first);
  if(!joint || !jointName(entry->first) || joint->boneType()==WHOLEAVATAR || joint->boneType()==COL_VOLUMES || !entry->second.isArray() || entry->second.size()!=4)
  {error="Only known non-root local joint quaternion rotations are supported";return false;}
  Rotation q;for(unsigned i=0;i<4;++i){if(!entry->second[i].isReal() && !entry->second[i].isInteger()){error="Rotation components must be finite numbers";return false;}q[i]=entry->second[i].asReal();}
  if(!normalize(q)){error="Rotations must be finite unit quaternions [x,y,z,w]";return false;}result[entry->first]=q;
 }
 return true;
}
Rotations captured()
{
 Rotations result;
 for(const auto& value:sAnimator.captureAssistantPose(gAgentAvatarp,sSession.motion))
 {Rotation q{{value.second.transform[0],value.second.transform[1],value.second.transform[2],value.second.transform[3]}};normalize(q);result[value.first]=q;}
 return result;
}
std::map<std::string,LLQuaternion> nativeRotations(const Rotations& rotations)
{
 std::map<std::string,LLQuaternion> result;
 for(const auto& entry:rotations)result.emplace(entry.first,LLQuaternion(entry.second[0],entry.second[1],entry.second[2],entry.second[3]));return result;
}
void reconcile()
{
 if(sSession.active)sSession.restorationComplete=sAnimator.assistantRestorationComplete(gAgentAvatarp,sSession.motion);
 if(sSession.active && !sAnimator.assistantPoseActive(gAgentAvatarp,sSession.motion))
 {sSession.active=false;sSession.stopped="Owned rotation motion stopped or its authority changed";sSession.task.reset();}
}
LLSD state()
{
 reconcile();return llsd::map("target",gAgentID,"pose_session",sSession.id,"revision",static_cast<S32>(sSession.transaction.revision()),
  "active",sSession.active,"native_poser_active",isAgentAvatarValid() && sAnimator.isPosingAvatar(gAgentAvatarp),
  "self_shared",FSSharedPoseManager::instance().isShared(gAgentID),"rotation_only",true,"reference_frame","local",
  "visibility","local_viewer","restoration_complete",sSession.restorationComplete,"can_undo",sSession.transaction.canUndo(),"owned_joints",rotationValue(sSession.transaction.rotations()),"stop_reason",sSession.stopped);
}
std::string pathString(const std::filesystem::path& path)
{const auto bytes=path.u8string();return std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
std::filesystem::path poseDirectory()
{
 const auto directory=gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS,"poses");
#if LL_WINDOWS
 return std::filesystem::path(ll_convert_string_to_wide(directory,directory.size(),CP_UTF8));
#else
 return std::filesystem::path(directory);
#endif
}
std::string presetKey(const std::string& name){return "preset-"+hash(name);}
bool readPreset(const std::string& id,std::string& bytes,std::string& error)
{
 NativePoseStore store;if(!store.open(poseDirectory().parent_path(),false,error))return false;
 for(const auto& name:store.names())if(presetKey(name)==id)return store.read(name,bytes,error);
 error="Exact saved preset ID was not found in the native pose store";return false;
}
bool parsePreset(const std::string& bytes,Rotations& result,std::string& error)
{
 if(!presetXML(bytes)){error="Preset XML is empty, oversized or contains unsupported declarations";return false;}
 LLSD pose;std::istringstream input(bytes);LLPointer<LLSDXMLParser> parser=new LLSDXMLParser(false);
 if(parser->parse(input,pose,presetLimit,8)==LLSDParser::PARSE_FAILURE || !pose.isMap() || pose.size()>static_cast<S32>(jointLimit+2))
 {error="Preset is not a bounded native rotation-only pose document";return false;}
 if(!pose["version"]["value"].isInteger() || pose["version"].size()!=1 ||
 pose["version"]["value"].asInteger()!=8 || !pose["startFromTeePose"]["value"].isBoolean() ||
 pose["startFromTeePose"].size()!=1 || pose["startFromTeePose"]["value"].asBoolean())
 {error="Only native version 8 local absolute rotation presets without global T-pose reset are supported";return false;}
 for(auto entry=pose.beginMap();entry!=pose.endMap();++entry)
 {
  if(entry->first=="version" || entry->first=="startFromTeePose")continue;
  const auto& joint=entry->second;
  if(!joint.isMap()){error="Unknown or wider native motion state is unsupported";return false;}
  for(auto field=joint.beginMap();field!=joint.endMap();++field)
   if(field->first!="rotation" && field->first!="enabled" && field->first!="jointBaseRotationIsZero" && field->first!="userSetBaseRotationToZero")
   {error="Preset contains position, scale, mirror, lock or wider motion state; rotation-only import refused";return false;}
  if((joint.has("enabled") && (!joint["enabled"].isBoolean() || !joint["enabled"].asBoolean())) ||
     (joint.has("userSetBaseRotationToZero") && (!joint["userSetBaseRotationToZero"].isBoolean() || !joint["userSetBaseRotationToZero"].asBoolean())) ||
     !joint["jointBaseRotationIsZero"].isBoolean() || !joint["jointBaseRotationIsZero"].asBoolean() || !joint["rotation"].isArray() || joint["rotation"].size()!=3)
  {error="Preset requires unsupported joint enable or relative/base state changes";return false;}
  F32 angles[3];for(unsigned i=0;i<3;++i){const auto& component=joint["rotation"][i];const double angle=component.asReal();if((!component.isReal() && !component.isInteger()) || !std::isfinite(angle) || std::abs(angle)>3.141593){error="Preset rotation exceeds local Euler bounds";return false;}angles[i]=angle;}
  LLQuaternion q;q.setEulerAngles(angles[0],angles[1],angles[2]);LLSD rotation=LLSD::emptyArray();for(float component:q.mQ)rotation.append(component);
  LLSD one;one[entry->first]=rotation;if(!readRotations(one,result,error))return false;
 }
 if(!valid(result)){error="Preset has no supported bounded rotations";return false;}return true;
}
std::string saveBytes(const Rotations& rotations)
{
 LLSD pose;pose["version"]["value"]=8;pose["startFromTeePose"]["value"]=false;
 for(const auto& joint:nativeRotations(rotations))
 {
  F32 x,y,z;joint.second.getEulerAngles(&x,&y,&z);LLSD rotation=LLSD::emptyArray();rotation.append(x);rotation.append(y);rotation.append(z);
  auto& value=pose[joint.first];value["rotation"]=rotation;value["enabled"]=true;value["jointBaseRotationIsZero"]=true;value["userSetBaseRotationToZero"]=true;
 }
 std::ostringstream out;LLSDSerialize::toPrettyXML(pose,out);return out.str();
}
std::string signature(const LLSD& request)
{
 LLSD copy;for(const char* field:{"op","target","pose_session","expected_revision","rotations","preset_id","preset_revision","name","expected_preset_revision","__assistant_epoch"})copy[field]=request[field];
 std::ostringstream out;LLSDSerialize::toBinary(copy,out);return hash(out.str());
}
Receipt* knownOperation(const LLSD& request)
{
 const auto key=signature(request);for(auto& entry:sReceipts)if(entry.second.signature==key && entry.second.identity==identity(request))return &entry.second;return nullptr;
}
bool context(const LLSD& request,std::string& error)
{
 if(!liveSelf(request,error,true))return false;
 if(FSSharedPoseManager::instance().isShared(gAgentID) || sAnimator.isPosingAvatar(gAgentAvatarp))
 {error="Native manual/shared Poser ownership conflicts with this private rotation transaction";return false;}
 return true;
}
LLSD evidence(const LLSD& request,std::string& error)
{
 const auto op=request["op"].asString();const bool read=op=="status" || op=="joints" || op=="presets";
 if(!liveSelf(request,error,!read))return LLSD();
 syncSession(request);reconcile();LLSD value=identity(request);value["state"]=state();
 if(read)return value;
 if(!context(request,error))return LLSD();
 if(request["pose_session"].asUUID()!=sSession.id || !request["expected_revision"].isInteger() || request["expected_revision"].asInteger()<0 ||
    static_cast<unsigned>(request["expected_revision"].asInteger())!=sSession.transaction.revision())
 {error="Pose transaction revision changed; obtain a fresh pose_status";return LLSD();}
 if(op=="start")
 {if(sSession.active){error="An assistant pose transaction is already active";return LLSD();}return value;}
 if(!sSession.active || sSession.authority!=identity(request)){error="An active pose transaction owned by this source/login is required";return LLSD();}
 if(op=="update")
 {Rotations changes;if(!readRotations(request["rotations"],changes,error))return LLSD();auto next=sSession.transaction;if(!next.update(sSession.transaction.revision(),changes,captured())){error="Pose joint capacity or snapshot conflict";return LLSD();}value["effect_rotations"]=rotationValue(next.rotations());}
 else if(op=="load")
 {std::string bytes;Rotations changes;if(!readPreset(request["preset_id"].asString(),bytes,error) || hash(bytes)!=request["preset_revision"].asString() || !parsePreset(bytes,changes,error)){if(error.empty())error="Preset changed; list presets again";return LLSD();}auto next=sSession.transaction;if(!next.update(sSession.transaction.revision(),changes,captured())){error="Preset exceeds transaction joint bounds";return LLSD();}value["preset_bytes"]=bytes;value["effect_rotations"]=rotationValue(next.rotations());}
 else if(op=="save")
 {if(!presetID(request["name"].asString()) || request["expected_preset_revision"].asString()!="absent" || !valid(sSession.transaction.rotations())){error="Saving requires a new confined preset name, expected_preset_revision=absent and owned rotations";return LLSD();}NativePoseStore store;if(!store.open(poseDirectory().parent_path(),false,error,true))return LLSD();if(store.exists(request["name"].asString()+".xml")){error="Preset name collision; replacement is not supported";return LLSD();}value["preset_bytes"]=saveBytes(sSession.transaction.rotations());}
 else if(op=="undo")
 {auto next=sSession.transaction;if(!next.undo(sSession.transaction.revision())){error="No owned transaction change can be undone";return LLSD();}value["effect_rotations"]=rotationValue(next.rotations());}
 else if(op=="reset")value["effect_rotations"]=LLSD::emptyMap();
 else if(op!="stop"){error="Unsupported pose operation";return LLSD();}
 return value;
}
bool validate(const FSAssistantPreparedOperation& operation,std::string& error)
{
 if(const auto* own=knownOperation(operation.request))
 {
  if(!context(operation.request,error) || own->identity!=identity(operation.request))return false;
  return sSession.id==own->result["pose_session"].asUUID();
 }
 const auto current=evidence(operation.request,error);if(!error.empty())return false;
 if(current!=operation.validation){error="Avatar, pose revision, native ownership or preset changed; prepare again";return false;}return true;
}
std::function<bool()> timedAuthority(const LLSD& request,const fs_assistant::AsyncTaskPtr& task)
{
 const auto owner=identity(request);const auto session=sSession.id;
 return [owner,session,task,request]()
 {
  std::string error;
  return sSession.id==session && sSession.active && sSession.authority==owner && identity(request)==owner && context(request,error) &&
   fs_assistant::asyncTaskAllowed(task);
 };
}
bool sharedOperation(const std::string& op)
{return op=="sharedSessions" || op=="sharedGrants" || op=="sharedEdit" || op=="startSharing" || op=="stopSharing";}
class FSPoseListener : public LLEventAPI,public FSAssistantFeature
{
public:
 FSPoseListener():LLEventAPI("FSPose","Bounded local rotation transactions and native shared consent")
 {gIdleCallbacks.addFunction([](void*){fs_pose_tick();});for(const char* op:{"status","joints","presets","start","update","undo","reset","stop","load","save","sharedSessions","sharedGrants","sharedEdit","startSharing","stopSharing"})add(op,"Pose tools",&FSPoseListener::dispatch);}
 const char* assistantCapability() const override{return "pose_tools";}
 LLSD assistantCapabilities() const override{return llsd::map("pose_tools",1,"shared_pose_tools",1);}
 LLSD assistantLimits() const override
 {
  auto result=llsd::map("rotation_joints",64,"preset_bytes",65536,"presets",256,"history",20,"reference_frame","local","rotation_only",true,"visibility","local_viewer","overwrite_presets",false,"requires_live_operation_authority",true);
  result["visibility_scope"]="own_pose";
  result["shared_pose"]=llsd::map("visibility","native_shared_pose_peers","native_owner_consent",true,
   "rotation_edit_joints",64,"may_publish_existing_position_scale",true,"uncertain_replay",false);
  return result;
 }
 bool prepareAssistant(FSAssistantPreparedOperation& operation,std::string& error) const override
 {
  if(sharedOperation(operation.request["op"].asString()))return FSSharedPoseManager::instance().prepareAssistantPose(operation,error);
  operation.validation=evidence(operation.request,error);if(!error.empty())return false;
  const auto op=operation.request["op"].asString();if(op=="status" || op=="joints" || op=="presets")return true;
  if(std::find(operation.requiredClasses.begin(),operation.requiredClasses.end(),"pose_read")==operation.requiredClasses.end())operation.requiredClasses.push_back("pose_read");
  if(op=="save" && std::find(operation.requiredClasses.begin(),operation.requiredClasses.end(),"pose_save")==operation.requiredClasses.end())operation.requiredClasses.push_back("pose_save");
  operation.summary=llsd::map("op",std::string("pose_")+op,"subject_count",1,"subjects",LLSD::emptyArray(),"consequences",LLSD::emptyArray(),"visibility","local_viewer","reference_frame","local","rotations",operation.validation["effect_rotations"]);
  operation.summary["subjects"].append(llsd::map("id",gAgentID,"name","Your avatar","after_name",op=="save"?"Save a new rotation-only native preset":"Change only this source's private joint rotations"));
  const auto rotations=op=="save"?rotationValue(sSession.transaction.rotations()):operation.validation["effect_rotations"];
  for(const auto& joint:llsd::inMap(rotations))
  {
   std::ostringstream shown;shown<<"local [";
   for(int axis=0;axis<4;++axis){if(axis)shown<<", ";shown<<joint.second[axis].asReal();}
   shown<<"]";operation.summary["subjects"].append(llsd::map("name",joint.first,"after_name",shown.str()));
  }
  if(op=="save" || op=="load")
   operation.summary["subjects"].append(llsd::map("name","Native pose preset","after_name",op=="save"?operation.request["name"].asString()+".xml (new file only)":operation.request["preset_id"].asString()+" revision "+operation.request["preset_revision"].asString()));
  operation.summary["subject_count"]=operation.summary["subjects"].size();
  operation.summary["consequences"].append("pose_local_rotations");return true;
 }
 bool validateAssistant(const FSAssistantPreparedOperation& operation,std::string& error) const override
 {return sharedOperation(operation.request["op"].asString())?FSSharedPoseManager::instance().validateAssistantPose(operation,error):validate(operation,error);}
 void dispatch(const LLSD& request)
 {
  Response response(LLSD(),request);
  if(!fs_assistant_request_allowed(request))return response.error("Assistant request/session/permission changed");
  fs_assistant::AsyncScope scope(fs_assistant_async_context(request));
  if(!fs_assistant::AsyncScope::current())return response.error("Pose tools require captured durable authority");
  std::string error;
  if(sharedOperation(request["op"].asString()))
  {
   const auto result=FSSharedPoseManager::instance().executeAssistantPose(request,fs_assistant::asyncTask(),error);
   if(!error.empty())return response.error(error);response.setResponse(result);return;
  }
  const auto prepared=evidence(request,error);if(!error.empty())return response.error(error);
  const auto op=request["op"].asString();
  if(op=="status")
  {
   if(request.has("operation_id"))
   {const auto found=sReceipts.find(request["operation_id"].asUUID());if(found==sReceipts.end() || found->second.identity!=identity(request))return response.error("Operation is unavailable for this source/login");response.setResponse(found->second.result);return;}
   response.setResponse(state());return;
  }
  if(op=="joints")
  {
   LLSD result=state();result["joints"]=LLSD::emptyArray();
   for(const auto& joint:sAnimator.PoserJoints)
   {if(!jointName(joint.jointName()) || joint.boneType()==WHOLEAVATAR || joint.boneType()==COL_VOLUMES)continue;auto* native=gAgentAvatarp->getJoint(joint.jointName());if(native)result["joints"].append(llsd::map("name",joint.jointName(),"rotation",native->getRotation().getValue(),"frame","local"));}
   response.setResponse(result);return;
  }
  if(op=="presets")
  {
   LLSD result;result["presets"]=LLSD::emptyArray();NativePoseStore store;
   if(!store.open(poseDirectory().parent_path(),false,error,true))return response.error(error);
   for(const auto& name:store.names())
   {const auto id=presetKey(name);std::string bytes,reason;Rotations rotations;const bool readable=store.read(name,bytes,reason);const bool supported=readable && parsePreset(bytes,rotations,reason);result["presets"].append(llsd::map("id",id,"name",pathString(pathFromUTF8(name).stem()),"revision",readable?hash(bytes):std::string(),"supported",supported,"reason",reason));}
   response.setResponse(result);return;
  }
  if(sReceipts.size()>=64){auto old=sReceipts.begin();while(old!=sReceipts.end() && old->first==sSession.operation)++old;if(old!=sReceipts.end())sReceipts.erase(old);}
  if(!fs_assistant::asyncSubmit())return response.error("Pose authority was revoked before submission");
  auto& receipt=sReceipts[request["reqid"].asUUID()];receipt={identity(request),request,LLSD(),signature(request)};
  auto task=fs_assistant::asyncTask();
  if(op=="start")
  {
   sSession.id=LLUUID::generateNewID();sSession.authority=identity(request);sSession.transaction=Transaction();sSession.active=true;sSession.restorationComplete=true;sSession.stopped.clear();
   receipt.result["pose_session"]=sSession.id;
   const auto session=sSession.id;
   sSession.motion=sAnimator.startAssistantPose(gAgentAvatarp,timedAuthority(request,task),[session](bool complete)
   {if(sSession.id==session){sSession.restorationComplete=complete;sSession.active=false;sSession.task.reset();sSession.stopped="Owned rotation authority ended or native ownership changed";}});
   if(sSession.motion.isNull()){sSession.active=false;receipt.result=state();return response.error("Private rotation motion could not start");}
   sSession.task=task;sSession.operation=request["reqid"].asUUID();
  }
  else if(op=="save")
  {
   NativePoseStore store;if(!store.open(poseDirectory().parent_path(),true,error))return response.error(error);
   const auto name=request["name"].asString()+".xml",bytes=prepared["preset_bytes"].asString();
   const auto saved=store.writeNew(name,bytes,error);if(!saved.created)return response.error(error);
   receipt.result=state();receipt.result["saved"]=saved.saved;receipt.result["preset_id"]=presetKey(name);receipt.result["preset_revision"]=hash(bytes);
   if(!saved.saved){receipt.result["uncertain"]=true;receipt.result["reason"]=error;}
  }
  else if(op=="stop")
  {sAnimator.stopAssistantPose(gAgentAvatarp,sSession.motion);sSession.active=false;sSession.task.reset();sSession.stopped="Stopped by this source";receipt.result=state();}
  else
  {
   auto next=sSession.transaction;
   if(op=="reset")next.reset(next.revision());else if(op=="undo")next.undo(next.revision());
   else{Rotations changes;if(op=="load")parsePreset(prepared["preset_bytes"].asString(),changes,error);else readRotations(request["rotations"],changes,error);if(!error.empty() || !next.update(next.revision(),changes,captured()))return response.error(error.empty()?"Pose revision conflict":error);}
   receipt.result["pose_session"]=sSession.id;
   if(!sAnimator.updateAssistantPose(gAgentAvatarp,sSession.motion,nativeRotations(next.rotations()),timedAuthority(request,task)))return response.error("Pose authority or native ownership changed before write");
   sSession.transaction=std::move(next);sSession.task=task;sSession.operation=request["reqid"].asUUID();
  }
  if(!receipt.result.has("revision"))receipt.result=state();receipt.result["operation_id"]=request["reqid"];receipt.result["ok"]=true;
  response.setResponse(receipt.result);
 }
};
}
void fs_initialize_pose_api(){static FSPoseListener listener;}
void fs_pose_tick(){sAnimator.tickAssistantPoses(gAgentAvatarp);reconcile();}
