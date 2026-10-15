/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsposelistener.h"
#include "fsassistantfeature.h"
#include "fsposepolicy.h"
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
 bool active=false;std::string stopped;
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
  // and releases only its own ROT channel at the next native update.
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
 if(sSession.active && !sAnimator.assistantPoseActive(gAgentAvatarp,sSession.motion))
 {sSession.active=false;sSession.stopped="Owned rotation motion stopped or its authority changed";sSession.task.reset();}
}
LLSD state()
{
 reconcile();return llsd::map("target",gAgentID,"pose_session",sSession.id,"revision",static_cast<S32>(sSession.transaction.revision()),
  "active",sSession.active,"native_poser_active",isAgentAvatarValid() && sAnimator.isPosingAvatar(gAgentAvatarp),
  "self_shared",FSSharedPoseManager::instance().isShared(gAgentID),"rotation_only",true,"reference_frame","local",
  "visibility","local_viewer","can_undo",sSession.transaction.canUndo(),"owned_joints",rotationValue(sSession.transaction.rotations()),"stop_reason",sSession.stopped);
}
std::string pathString(const std::filesystem::path& path)
{const auto bytes=path.u8string();return std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
std::filesystem::path poseDirectory(){return std::filesystem::u8path(gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS,"poses"));}
std::string presetKey(const std::string& name){return "preset-"+hash(name);}
bool readPreset(const std::string& id,std::string& bytes,std::string& error)
{
 std::error_code ec;const auto dir=poseDirectory();
 if(!std::filesystem::is_directory(dir,ec) || std::filesystem::is_symlink(dir,ec)){error="Native pose directory is unavailable";return false;}
 unsigned seen=0;
 for(std::filesystem::directory_iterator it(dir,ec),end;!ec && it!=end && seen<256;it.increment(ec))
 {
  const auto name=pathString(it->path().filename());if(it->path().extension()!=".xml")continue;++seen;
  if(presetKey(name)!=id)continue;
  if(it->is_symlink(ec) || !it->is_regular_file(ec) || it->file_size(ec)>presetLimit || ec){error="Preset is not a bounded regular native pose file";return false;}
  llifstream input(pathString(it->path()),std::ios::binary);if(!input){error="Preset could not be opened";return false;}
  bytes.resize(presetLimit+1);input.read(&bytes[0],presetLimit+1);bytes.resize(static_cast<std::size_t>(input.gcount()));
  if(input.bad()){error="Preset read failed";return false;}
  if(bytes.size()>presetLimit){error="Preset exceeds the 65536 byte limit";return false;}return true;
 }
 error="Exact saved preset ID was not found in the native pose store";return false;
}
bool parsePreset(const std::string& bytes,Rotations& result,std::string& error)
{
 if(bytes.empty() || bytes.size()>presetLimit || bytes.find("<!")!=std::string::npos){error="Preset XML is empty, oversized or contains unsupported declarations";return false;}
 LLSD pose;std::istringstream input(bytes);LLPointer<LLSDXMLParser> parser=new LLSDXMLParser(false);
 if(parser->parse(input,pose,presetLimit,8)==LLSDParser::PARSE_FAILURE || !pose.isMap() || pose.size()>static_cast<S32>(jointLimit+2))
 {error="Preset is not a bounded native rotation-only pose document";return false;}
 input>>std::ws;
 if(!input.eof() || !pose["version"]["value"].isInteger() || pose["version"].size()!=1 ||
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
  if((joint.has("enabled") && !joint["enabled"].asBoolean()) || !joint["jointBaseRotationIsZero"].asBoolean() || !joint["rotation"].isArray() || joint["rotation"].size()!=3)
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
 {if(!presetID(request["name"].asString()) || request["expected_preset_revision"].asString()!="absent" || !valid(sSession.transaction.rotations())){error="Saving requires a new confined preset name, expected_preset_revision=absent and owned rotations";return LLSD();}std::error_code ec;if(std::filesystem::exists(poseDirectory()/(request["name"].asString()+".xml"),ec)){error="Preset name collision; replacement is not supported";return LLSD();}value["preset_bytes"]=saveBytes(sSession.transaction.rotations());}
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
class FSPoseListener : public LLEventAPI,public FSAssistantFeature
{
public:
 FSPoseListener():LLEventAPI("FSPose","Bounded local rotation transactions and native shared consent")
 {for(const char* op:{"status","joints","presets","start","update","undo","reset","stop","load","save"})add(op,"Pose tools",&FSPoseListener::dispatch);}
 const char* assistantCapability() const override{return "pose_tools";}
 LLSD assistantLimits() const override{return llsd::map("rotation_joints",64,"preset_bytes",65536,"presets",256,"history",20,"reference_frame","local","rotation_only",true,"visibility","local_viewer","overwrite_presets",false,"requires_live_operation_authority",true);}
 bool prepareAssistant(FSAssistantPreparedOperation& operation,std::string& error) const override
 {
  operation.validation=evidence(operation.request,error);if(!error.empty())return false;
  const auto op=operation.request["op"].asString();if(op=="status" || op=="joints" || op=="presets")return true;
  if(std::find(operation.requiredClasses.begin(),operation.requiredClasses.end(),"pose_read")==operation.requiredClasses.end())operation.requiredClasses.push_back("pose_read");
  if(op=="save" && std::find(operation.requiredClasses.begin(),operation.requiredClasses.end(),"pose_save")==operation.requiredClasses.end())operation.requiredClasses.push_back("pose_save");
  operation.summary=llsd::map("op",op,"subject_count",1,"subjects",LLSD::emptyArray(),"consequences",LLSD::emptyArray(),"visibility","local_viewer","reference_frame","local","rotations",operation.validation["effect_rotations"]);
  operation.summary["subjects"].append(llsd::map("id",gAgentID,"name","Your avatar","after_name",op=="save"?"Save a new rotation-only native preset":"Change only this source's private joint rotations"));
  operation.summary["consequences"].append("pose_local_rotations");return true;
 }
 bool validateAssistant(const FSAssistantPreparedOperation& operation,std::string& error) const override{return validate(operation,error);}
 void dispatch(const LLSD& request)
 {
  Response response(LLSD(),request);
  if(!fs_assistant_request_allowed(request))return response.error("Assistant request/session/permission changed");
  fs_assistant::AsyncScope scope(fs_assistant_async_context(request));
  if(!fs_assistant::AsyncScope::current())return response.error("Pose tools require captured durable authority");
  std::string error;const auto prepared=evidence(request,error);if(!error.empty())return response.error(error);
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
   LLSD result;result["presets"]=LLSD::emptyArray();std::error_code ec;const auto dir=poseDirectory();
   if(std::filesystem::is_directory(dir,ec) && !std::filesystem::is_symlink(dir,ec))
    for(std::filesystem::directory_iterator it(dir,ec),end;!ec && it!=end && result["presets"].size()<256;it.increment(ec))
    {if(it->path().extension()!=".xml" || it->is_symlink(ec) || !it->is_regular_file(ec))continue;const auto name=pathString(it->path().filename());const auto id=presetKey(name);std::string bytes,reason;Rotations rotations;const bool readable=readPreset(id,bytes,reason);const bool supported=readable && parsePreset(bytes,rotations,reason);result["presets"].append(llsd::map("id",id,"name",pathString(it->path().stem()),"revision",readable?hash(bytes):std::string(),"supported",supported,"reason",reason));}
   response.setResponse(result);return;
  }
  if(sReceipts.size()>=64){auto old=sReceipts.begin();while(old!=sReceipts.end() && old->first==sSession.operation)++old;if(old!=sReceipts.end())sReceipts.erase(old);}
  if(!fs_assistant::asyncSubmit())return response.error("Pose authority was revoked before submission");
  auto& receipt=sReceipts[request["reqid"].asUUID()];receipt={identity(request),request,LLSD(),signature(request)};
  auto task=fs_assistant::asyncTask();
  if(op=="start")
  {
   sSession.id=LLUUID::generateNewID();sSession.authority=identity(request);sSession.transaction=Transaction();sSession.active=true;sSession.stopped.clear();
   receipt.result["pose_session"]=sSession.id;
   const auto session=sSession.id;
   sSession.motion=sAnimator.startAssistantPose(gAgentAvatarp,timedAuthority(request,task),[session]()
   {if(sSession.id==session){sSession.active=false;sSession.task.reset();sSession.stopped="Owned rotation authority ended or native ownership changed";}});
   if(sSession.motion.isNull()){sSession.active=false;receipt.result=state();return response.error("Private rotation motion could not start");}
   sSession.task=task;sSession.operation=request["reqid"].asUUID();
  }
  else if(op=="save")
  {
   const auto dir=poseDirectory();std::error_code ec;
   if(std::filesystem::is_symlink(dir,ec) || (!std::filesystem::is_directory(dir,ec) && !std::filesystem::create_directory(dir,ec)))return response.error("Native pose directory could not be created safely");
   const auto path=dir/(request["name"].asString()+".xml");const auto bytes=prepared["preset_bytes"].asString();
   LLFILE* file=LLFile::fopen(pathString(path),"wx");if(!file)return response.error("Preset collision or native file creation failed");
   const bool saved=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();const bool closed=std::fclose(file)==0;
   receipt.result=state();receipt.result["saved"]=saved && closed;receipt.result["preset_id"]=presetKey(pathString(path.filename()));receipt.result["preset_revision"]=hash(bytes);
   if(!saved || !closed)receipt.result["uncertain"]=true;
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
