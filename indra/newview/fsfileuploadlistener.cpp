/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsfileuploadlistener.h"
#include "fsfileuploadmodel.h"
#include "fsassistantfeature.h"
#include "fscontentpermissions.h"
#include "fseventapibridge.h"
#include "fssessionworker.h"
#include "llagentbenefits.h"
#include "llapp.h"
#include "llbvhloader.h"
#include "llcallbacklist.h"
#include "lldatapacker.h"
#include "lldir.h"
#include "lleventapi.h"
#include "llfile.h"
#include "llfilesystem.h"
#if LL_WINDOWS
#include "llwin32headers.h"
#endif
#include "llfloatermodelpreview.h"
#include "llfloaterperms.h"
#include "llfloaterreg.h"
#include "llimagej2c.h"
#include "llkeyframemotion.h"
#include "llstartup.h"
#include "lltimer.h"
#include "llviewerassetupload.h"
#include "llviewerregion.h"
#include "llviewertexturelist.h"
#include "llvoavatarself.h"
#include "llvorbisencode.h"
#include <filesystem>
#if !LL_WINDOWS
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <limits>
#include <map>
#include <set>

namespace
{
namespace model=fs_file_upload;
double now(){return LLTimer::getTotalSeconds();}
std::string source()
{
    if(gDisconnected || LLApp::isExiting() || LLStartUp::getStartupState()!=STATE_STARTED || !gAgent.getRegion())return {};
    return gAgent.getID().asString()+":"+gAgent.getSessionID().asString()+":"+LLGridManager::instance().getGrid()+":"+
        FSEventAPIBridge::instance().sessionReference()+":"+ll_stream_notation_sd(FSSessionWorker::assistantIdentity());
}
// Explicit UTF-8 conversion avoids C++20 u8path deprecation under MSVC /WX.
std::filesystem::path localPath(const std::string& value)
{
#if LL_WINDOWS
    return std::filesystem::path(ll_convert_string_to_wide(value,value.size(),CP_UTF8));
#else
    return std::filesystem::path(value);
#endif
}
bool destination(const LLUUID& id,std::string& error)
{
    auto* category=gInventory.getCategory(id);
    if(!category || (id!=gInventory.getRootFolderID() && !fs_content::agentInventory(id)))
    {error="Destination must be a folder in this character's inventory";return false;}
    std::set<LLUUID> seen;
    while(category && seen.insert(category->getUUID()).second)
    {
        switch(category->getPreferredType())
        {
        case LLFolderType::FT_NONE: case LLFolderType::FT_ROOT_INVENTORY:
        case LLFolderType::FT_TEXTURE: case LLFolderType::FT_SOUND: case LLFolderType::FT_ANIMATION: break;
        default: error="Protected, outfit, Marketplace, Library and Trash destinations are unavailable";return false;
        }
        if(category->getUUID()==gInventory.getRootFolderID())return true;
        category=gInventory.getCategory(category->getParentUUID());
    }
    error="Destination ancestry is incomplete";return false;
}
LLSD destinationEvidence(const LLUUID& id)
{
    auto* category=gInventory.getCategory(id);
    return category?llsd::map("id",id,"parent",category->getParentUUID(),"type",static_cast<S32>(category->getPreferredType())):LLSD();
}
struct Temporary
{
    std::string path;
    explicit Temporary(const std::string& extension):path(gDirUtilp->getTempFilename()+"."+extension){}
    ~Temporary(){LLFile::remove(path);}
    bool write(const std::string& bytes)const
    {
        auto* file=LLFile::fopen(path,"wb");if(!file)return false;
        const bool ok=fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
        return fclose(file)==0 && ok;
    }
};
bool readFile(const std::string& path,std::string& bytes,std::string& error)
{
    if(path.empty() || path.size()>4096 || path.find('\0')!=std::string::npos || !localPath(path).is_absolute())
    {error="An absolute local file path is required";return false;}
#if LL_WINDOWS
    const auto drive=localPath(path).root_name().wstring();
    if(drive.size()!=2 || drive[1]!=L':') {error="Network and device paths are not upload inputs";return false;}
#endif
    std::error_code ec;
    // Refuse symlinks, devices and unbounded streams even if called without the sidecar.
    std::vector<std::filesystem::path> parents;
    for(auto current=localPath(path);!current.empty();current=current.parent_path())
    {parents.push_back(current);if(current==current.parent_path())break;}
    for(auto it=parents.rbegin();it!=parents.rend();++it)
    {
        const auto status=std::filesystem::symlink_status(*it,ec);
        if(ec || std::filesystem::is_symlink(status)){error="Missing or linked upload path";return false;}
    }
    if(!std::filesystem::is_regular_file(localPath(path),ec) || ec)
    {error="Upload source must be a regular local file";return false;}
#if LL_WINDOWS
    const auto handle=CreateFileW(localPath(path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(handle==INVALID_HANDLE_VALUE){error="Cannot open upload file";return false;}
    struct Close {HANDLE value;~Close(){CloseHandle(value);}} close{handle};
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
    if(GetFileType(handle)!=FILE_TYPE_DISK || !GetFileInformationByHandle(handle,&info) ||
       (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) ||
       !GetFileSizeEx(handle,&size) || size.QuadPart<=0 || size.QuadPart>static_cast<LONGLONG>(model::maxFileBytes))
    {error="Upload input must be a regular 1..64 MiB disk file";return false;}
    bytes.resize(static_cast<std::size_t>(size.QuadPart));DWORD received=0;
    if(!ReadFile(handle,bytes.data(),static_cast<DWORD>(bytes.size()),&received,nullptr) || received!=bytes.size())
    {error="Could not read complete upload input";bytes.clear();return false;}
#else
    const auto fd=::open(path.c_str(),O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0){error="Cannot open upload file";return false;}
    struct Close {int value;~Close(){::close(value);}} close{fd};
    struct stat info{};
    if(::fstat(fd,&info)!=0 || !S_ISREG(info.st_mode) || info.st_size<=0 || static_cast<std::size_t>(info.st_size)>model::maxFileBytes)
    {error="Upload input must be a regular 1..64 MiB file";return false;}
    bytes.resize(static_cast<std::size_t>(info.st_size));std::size_t offset=0;
    while(offset<bytes.size())
    {
        const auto received=::read(fd,bytes.data()+offset,bytes.size()-offset);
        if(received<0 && errno==EINTR)continue;
        if(received<=0){error="Could not read complete upload input";bytes.clear();return false;}
        offset+=static_cast<std::size_t>(received);
    }
    char extra=0;
    if(::read(fd,&extra,1)!=0){error="Upload input changed while reading";bytes.clear();return false;}
#endif
    return true;
}
bool verifiedInput(const LLSD& input,std::string& bytes,std::string& error)
{
    const auto count=input["bytes"].asInteger();
    if(!input["bytes"].isInteger() || count<=0 || count>static_cast<S32>(model::maxFileBytes) ||
       !model::hash(input["sha256"].asString()) || !readFile(input["path"].asString(),bytes,error))
    {if(error.empty())error="Explicit file size and SHA-256 are required";return false;}
    if(bytes.size()!=static_cast<std::size_t>(count) || fs_content::hash(bytes)!=input["sha256"].asString())
    {error="File differs from the sidecar's manifest; preview it again";return false;}
    return true;
}
struct File
{
    std::string name,relative,encoded;
    LLAssetType::EType asset=LLAssetType::AT_NONE;
    LLInventoryType::EType inventory=LLInventoryType::IT_NONE;
    LLFolderType::EType folder=LLFolderType::FT_NONE;
    S32 width=0,height=0,cost=-1;
};
S32 quote(const File& file)
{
    const auto& benefits=LLAgentBenefitsMgr::current();
    if(file.asset==LLAssetType::AT_TEXTURE)return benefits.getTextureUploadCost(file.width,file.height);
    if(file.asset==LLAssetType::AT_SOUND)return benefits.getSoundUploadCost();
    if(file.asset==LLAssetType::AT_ANIMATION)return benefits.getAnimationUploadCost();
    return -1;
}
bool encode(const LLSD& input,S32 dimension,File& output,std::string& error)
{
    output.name=input["name"].asString();output.relative=input["relative_folder"].asString();
    if(!model::name(output.name) || !model::folder(output.relative))
    {error="Invalid asset name or relative folder";return false;}
    std::string raw;if(!verifiedInput(input,raw,error))return false;
    auto ext=gDirUtilp->getExtension(input["path"].asString());LLStringUtil::toLower(ext);
    Temporary in(ext),out("asset");if(!in.write(raw)){error="Cannot stage native upload input";return false;}
    if(ext=="png" || ext=="jpg" || ext=="jpeg" || ext=="bmp" || ext=="tga")
    {
        LLAssetType::EType type;U32 codec=IMG_CODEC_INVALID;
        if(!LLResourceUploadInfo::findAssetTypeAndCodecOfExtension(ext,type,codec) || type!=LLAssetType::AT_TEXTURE)
        {error="Unsupported native image codec";return false;}
        LLPointer<LLImageFormatted> image=LLImageFormatted::createFromType(static_cast<S8>(codec));
        if(image.isNull() || !image->load(in.path) || image->getWidth()<=0 || image->getHeight()<=0 ||
           image->getWidth()>8192 || image->getHeight()>8192)
        {error="Invalid image or image dimensions exceed 8192";return false;}
        if(!LLViewerTextureList::createUploadFile(in.path,out.path,static_cast<U8>(codec),dimension))
        {error="Native image encoding failed: "+LLImage::getLastThreadError();return false;}
        LLPointer<LLImageJ2C> encoded=new LLImageJ2C();
        if(!encoded->load(out.path) || encoded->getWidth()<=0 || encoded->getHeight()<=0)
        {error="Cannot determine encoded texture price";return false;}
        output.width=encoded->getWidth();output.height=encoded->getHeight();
        output.asset=LLAssetType::AT_TEXTURE;output.inventory=LLInventoryType::IT_TEXTURE;output.folder=LLFolderType::FT_TEXTURE;
        if(!readFile(out.path,output.encoded,error))return false;
    }
    else if(ext=="wav")
    {
        if(encode_vorbis_file(in.path,out.path,LLGridManager::instance().isInSecondLife())!=LLVORBISENC_NOERR)
        {error="Native WAV validation/encoding failed";return false;}
        output.asset=LLAssetType::AT_SOUND;output.inventory=LLInventoryType::IT_SOUND;output.folder=LLFolderType::FT_SOUND;
        if(!readFile(out.path,output.encoded,error))return false;
    }
    else if(ext=="anim" || ext=="bvh")
    {
        if(!isAgentAvatarValid()){error="Animation validation requires a ready native avatar";return false;}
        if(raw.size()>16*1024*1024){error="Animation source exceeds 16 MiB";return false;}
        if(ext=="bvh")
        {
            if(raw.find('\0')!=std::string::npos){error="BVH source contains NUL";return false;}
            ELoadStatus status=E_ST_OK;S32 line=0;auto aliases=gAgentAvatarp->getJointAliases();
            LLBVHLoader loader(raw.c_str(),status,line,aliases);
            if(!loader.isInitialized() || loader.getOutputSize()==0 || loader.getOutputSize()>16*1024*1024)
            {error="Native BVH conversion failed at line "+std::to_string(line);return false;}
            output.encoded.resize(loader.getOutputSize());
            LLDataPackerBinaryBuffer pack(reinterpret_cast<U8*>(output.encoded.data()),static_cast<S32>(output.encoded.size()));
            if(!loader.serialize(pack)){error="BVH serialization failed";return false;}
        }
        else output.encoded=std::move(raw);
        struct ValidationCache
        {
            LLUUID id=LLUUID::generateNewID();
            ~ValidationCache(){LLKeyframeDataCache::removeKeyframeData(id);}
        } validationCache;
        LLKeyframeMotion motion(validationCache.id);motion.setCharacter(gAgentAvatarp);
        LLDataPackerBinaryBuffer pack(reinterpret_cast<U8*>(output.encoded.data()),static_cast<S32>(output.encoded.size()));
        if(!motion.deserialize(pack,motion.getID(),false)){error="Native animation validation failed";return false;}
        output.asset=LLAssetType::AT_ANIMATION;output.inventory=LLInventoryType::IT_ANIMATION;output.folder=LLFolderType::FT_ANIMATION;
    }
    else {error="Unsupported automatic upload extension: "+ext+"; DAE uses native mesh review";return false;}
    output.cost=quote(output);
    if(output.cost<0){error="Account upload price is unavailable";return false;}
    return true;
}
struct Plan
{
    LLUUID id,destination;
    std::string origin,epoch;
    LLSD evidence,receipt,summary;
    std::vector<File> files;
    std::map<std::string,LLUUID> folders;
    model::Progress progress;
    fs_assistant::AsyncTaskPtr task;
    double expires=0;
    U32 nextOwner=0,group=0,everyone=0;
};
std::map<LLUUID,std::shared_ptr<Plan>> plans;
std::string plansSource;
void releaseBytes(const std::shared_ptr<Plan>& plan)
{for(auto& file:plan->files)std::string().swap(file.encoded);}
void stop(const std::shared_ptr<Plan>& plan,const std::string& reason)
{
    if(plan->progress.state==model::State::Complete || plan->progress.state==model::State::Stopped)return;
    plan->progress.stop();plan->receipt["state"]="stopped";plan->receipt["reason"]=reason;
    if(plan->progress.submissionStarted || plan->receipt["folder_in_flight"].asBoolean())plan->receipt["uncertain"]=true;
    if(plan->progress.next<plan->files.size())
    {
        auto& row=plan->receipt["files"][static_cast<S32>(plan->progress.next)];
        const auto state=row["state"].asString();
        if(state=="queued" || state=="creating_folder" || state=="not_submitted")
            row["state"]=plan->receipt["uncertain"].asBoolean()?"submitted_unknown":"not_submitted";
    }
    releaseBytes(plan);plan->task.reset();
}
void prune()
{
    const auto current=source();
    if(plansSource!=current)
    {for(const auto& entry:plans)stop(entry.second,"source_changed");plans.clear();plansSource=current;}
    for(auto it=plans.begin();it!=plans.end();)
    {
        auto& plan=it->second;
        if(plan->progress.state==model::State::Prepared && now()>=plan->expires)it=plans.erase(it);
        else ++it;
    }
    while(plans.size()>=32)
    {
        const auto old=std::find_if(plans.begin(),plans.end(),[](const auto& entry){return entry.second->progress.state==model::State::Complete || entry.second->progress.state==model::State::Stopped;});
        if(old==plans.end())break;
        plans.erase(old);
    }
}
std::shared_ptr<Plan> findPlan(const LLSD& request,std::string& error)
{
    prune();const auto it=plans.find(request["plan_id"].asUUID());
    if(it==plans.end() || it->second->origin!=source() || it->second->epoch!=request["__assistant_epoch"].asString())
    {error="Upload plan expired or belongs to a different login/connection";return {};}
    return it->second;
}
bool live(const std::shared_ptr<Plan>& plan,std::string& error,bool costs)
{
    if(plan->origin!=source() || !destination(plan->destination,error) || plan->evidence!=destinationEvidence(plan->destination))
    {if(error.empty())error="Upload source or destination changed";return false;}
    // The approved hierarchy is exact, including folders created by this plan.
    // This check is also reached by the durable guard at queued AIS submission.
    for(const auto& folder:plan->folders)
    {
        const auto slash=folder.first.rfind('/');
        const auto parentPath=slash==std::string::npos?std::string():folder.first.substr(0,slash);
        const auto expectedParent=parentPath.empty()?plan->destination:plan->folders.at(parentPath);
        const auto expectedName=slash==std::string::npos?folder.first:folder.first.substr(slash+1);
        auto* category=gInventory.getCategory(folder.second);
        if(!category || category->getParentUUID()!=expectedParent || category->getName()!=expectedName)
        {error="Created upload folder moved, renamed or disappeared";return false;}
    }
    if(costs)for(std::size_t index=plan->progress.next;index<plan->files.size();++index)
        if(!model::quoteAccepted(plan->files[index].cost,quote(plan->files[index]))){error="Upload price increased or became unavailable; obtain a fresh plan";return false;}
    return true;
}
class AssistantUploadInfo final:public LLNewBufferedResourceUploadInfo
{
public:
    using LLNewBufferedResourceUploadInfo::LLNewBufferedResourceUploadInfo;
    ~AssistantUploadInfo() override { LLFileSystem::removeFile(getAssetId(),getAssetType()); }
    bool showUploadDialog()const override{return false;}
    bool requiresQuotedUploadPrice()const override{return true;}
};
void advance(const std::shared_ptr<Plan>& plan);
void schedule(const std::shared_ptr<Plan>& plan)
{doOnIdleOneTime([plan](){advance(plan);});}
void advance(const std::shared_ptr<Plan>& plan)
{
    if(!plan->progress.running())return;
    const auto index=plan->progress.next;
    // Reconcile a completed upload before checking authority for more work.
    // Finishing this receipt neither submits nor grants another native action.
    if(index==plan->files.size())
    {plan->progress.complete(index);plan->receipt["state"]="complete";releaseBytes(plan);plan->task.reset();return;}
    fs_assistant::AsyncScope scope(plan->task?plan->task->guard():fs_assistant::AsyncGuard());
    std::string error;
    if(!plan->task || !fs_assistant::asyncAllowed() || !live(plan,error,true))
    {stop(plan,error.empty()?"Upload authority revoked or expired":error);return;}
    auto& file=plan->files[index];
    std::string prefix;LLUUID parent=plan->destination;
    for(std::size_t begin=0;begin<file.relative.size();)
    {
        const auto end=file.relative.find('/',begin);
        const auto part=file.relative.substr(begin,end==std::string::npos?file.relative.size()-begin:end-begin);
        prefix+=(prefix.empty()?"":"/")+part;
        const auto found=plan->folders.find(prefix);
        if(found==plan->folders.end())
        {
            if(!destination(parent,error) || (parent!=plan->destination && !gInventory.isObjectDescendentOf(parent,plan->destination)))
            {stop(plan,error.empty()?"Folder parent left the chosen upload destination":error);return;}
            plan->receipt["files"][static_cast<S32>(index)]["state"]="creating_folder";
            const auto folderTask=plan->task;
            const auto priorIDs=folderTask->guard()->ids();
            const auto priorSubmissions=folderTask->guard()->submitted();
            plan->receipt["folder_in_flight"]=true;
            gInventory.createNewCategory(parent,LLFolderType::FT_NONE,part,[plan,prefix,parent,part,folderTask,priorIDs,priorSubmissions](const LLUUID& suppliedID)
            {
                fs_assistant::AsyncScope callbackScope(folderTask->guard());
                LLUUID id=suppliedID;
                // Native wrappers hide the result after revocation, but first
                // acknowledge its raw ID in the durable operation receipt. Use
                // that same-login evidence only for reporting, never to resume.
                if(plan->origin==source())
                {
                    if(id.isNull())for(const auto& candidate:folderTask->guard()->ids())
                    {
                        if(std::find(priorIDs.begin(),priorIDs.end(),candidate)!=priorIDs.end())continue;
                        auto* created=gInventory.getCategory(LLUUID(candidate));
                        if(created && created->getParentUUID()==parent && created->getName()==part) {id=created->getUUID();break;}
                    }
                    if(id.notNull())
                    {plan->folders[prefix]=id;plan->receipt["created_folders"].append(llsd::map("id",id,"path",prefix));plan->receipt["folder_in_flight"]=false;plan->receipt["uncertain"]=plan->progress.submissionStarted;}
                    else if(folderTask->guard()->submitted()==priorSubmissions)
                    {plan->receipt["folder_in_flight"]=false;plan->receipt["uncertain"]=plan->progress.submissionStarted;}
                }
                if(!plan->progress.running())return;
                if(!plan->task || !fs_assistant::asyncAllowed()) {stop(plan,"Authority changed while creating folder");return;}
                auto* category=gInventory.getCategory(id);
                if(id.isNull() || !category || category->getParentUUID()!=parent || category->getName()!=part)
                {stop(plan,"Could not create folder: "+prefix);return;}
                schedule(plan);
            });
            return;
        }
        parent=found->second;
        if(end==std::string::npos)break;
        begin=end+1;
    }
    if(!destination(parent,error) || (parent!=plan->destination && !gInventory.isObjectDescendentOf(parent,plan->destination)))
    {stop(plan,error.empty()?"Created upload folder moved outside the chosen destination":error);return;}
    auto finished=[plan,index](LLUUID asset,LLSD result)
    {
        if(!plan->progress.running() || plan->progress.next!=index)return;
        auto& row=plan->receipt["files"][static_cast<S32>(index)];
        row["asset_id"]=asset;row["item_id"]=result["new_inventory_item"];row["state"]="complete";
        row["charged_cost"]=result.has("upload_price")?result["upload_price"]:LLSD();
        if(!plan->progress.acknowledge(index))return;
        plan->receipt["uncertain"]=false;std::string().swap(plan->files[index].encoded);
        if(plan->task && plan->task->guard())plan->task->guard()->acknowledge(result["new_inventory_item"].asString());
        if(plan->progress.next==plan->files.size())
        {plan->progress.complete(plan->files.size());plan->receipt["state"]="complete";releaseBytes(plan);plan->task.reset();return;}
        schedule(plan);
    };
    auto failed=[plan,index](LLUUID,LLSD,const std::string& reason)
    {
        plan->receipt["files"][static_cast<S32>(index)]["state"]=plan->progress.submissionStarted?"submitted_unknown":"failed";
        stop(plan,reason);return true;
    };
    LLResourceUploadInfo::ptr_t upload=std::make_shared<AssistantUploadInfo>(file.encoded,LLUUID::generateNewID(),
        file.name,"",0,file.folder,file.inventory,file.asset,plan->nextOwner,plan->group,plan->everyone,file.cost,parent,false,finished,failed);
    const auto task=plan->task;
    upload->setAssistantHooks({[plan,task,index,parent](const std::string& stage,bool submit)
    {
        std::string error;
        if(!plan->progress.running() || plan->progress.next!=index || !fs_assistant::asyncTaskAllowed(task) || !live(plan,error,true) || !destination(parent,error) ||
           (parent!=plan->destination && !gInventory.isObjectDescendentOf(parent,plan->destination)))return false;
        if(submit && !fs_assistant::asyncTaskSubmit(task))return false;
        if(submit)plan->progress.submissionStarted=true;
        if(stage=="capability" && submit) {plan->receipt["files"][static_cast<S32>(index)]["state"]="submitted_unknown";plan->receipt["uncertain"]=true;}
        if(stage=="source" && submit)
        {plan->progress.sourceSubmitted=true;plan->receipt["files"][static_cast<S32>(index)]["state"]="submitted_unknown";plan->receipt["uncertain"]=true;}
        return true;
    },[plan,index](const LLSD& result,const std::string& stage)
    {
        auto& row=plan->receipt["files"][static_cast<S32>(index)];
        if(plan->origin==source() && result["state"].asString()=="complete" && result["new_asset"].asUUID().notNull())
        {row["state"]="server_confirmed";row["asset_id"]=result["new_asset"];row["item_id"]=result["new_inventory_item"];if(result.has("upload_price"))row["charged_cost"]=result["upload_price"];plan->receipt["uncertain"]=false;plan->progress.sourceSubmitted=false;plan->progress.submissionStarted=false;}
        else row["state"]=plan->progress.submissionStarted?"submitted_unknown":"not_submitted";
        stop(plan,"Upload stopped at "+stage+"; inspect results before retrying");
    }});
    plan->receipt["files"][static_cast<S32>(index)]["state"]="queued";
    LLViewerAssetUpload::EnqueueInventoryUpload(gAgent.getRegionCapability("NewFileAgentInventory"),upload);
}
LLSD material(const LLSD& request)
{
    LLSD result=LLSD::emptyMap();
    for(const char* key:{"op","files","folder_id","max_texture_dimension","plan_id","file"})if(request.has(key))result[key]=request[key];
    return result;
}
class FSFileUploadListener final:public LLEventAPI,public FSAssistantFeature
{
public:
    FSFileUploadListener():LLEventAPI("FSUpload","Source-bound native asset uploads")
    {
        for(const char* op:{"preview","execute","status","cancel","meshReview"})add(op,"Native file upload service",&FSFileUploadListener::dispatch);
        gIdleCallbacks.addFunction([](void*)
        {
            prune();for(const auto& entry:plans)if(entry.second->progress.running() && (!entry.second->task || !fs_assistant::asyncTaskAllowed(entry.second->task)))stop(entry.second,"Upload authority revoked or expired");
        });
    }
    const char* assistantCapability()const override{return "file_uploads";}
    LLSD assistantLimits()const override
    {return llsd::map("files",32,"file_bytes",static_cast<S32>(model::maxFileBytes),"batch_bytes",static_cast<S32>(model::maxBatchBytes),"folder_depth",8,"plan_seconds",120,"automatic_retry",false,"mesh_requires_native_review",true);}
    bool prepareAssistant(FSAssistantPreparedOperation& operation,std::string& error)const override
    {
        prune();const auto& request=operation.request;const auto op=request["op"].asString();
        if(source().empty()){error="A ready exact viewer login is required";return false;}
        operation.validation=llsd::map("source",source(),"material",material(request));
        if(op=="preview")
        {
            if(!request["files"].isArray() || request["files"].size()==0 || request["files"].size()>static_cast<S32>(model::maxFiles) ||
               (request["max_texture_dimension"].asInteger()!=1024 && request["max_texture_dimension"].asInteger()!=2048))
            {error="Supply 1..32 files and max_texture_dimension 1024 or 2048";return false;}
            return destination(request["folder_id"].asUUID(),error);
        }
        if(op=="meshReview")
        {
            if(!destination(request["folder_id"].asUUID(),error))return false;
            if(gDirUtilp->getExtension(request["file"]["path"].asString())!="dae") {error="Native mesh review requires a local DAE";return false;}
            const auto* preview=LLFloaterReg::findTypedInstance<LLFloaterModelPreview>("upload_model");
            if(preview){error="Close the existing mesh importer before opening another";return false;}
            operation.requiredClasses.push_back("read");operation.requiredClasses.push_back("create");
            operation.summary=llsd::map("action","Open native mesh upload review (does not upload)","subject_count",1,"subjects",LLSD::emptyArray());
            operation.summary["subjects"].append(llsd::map("name",request["file"]["name"],"path",request["file"]["path"]));
            return true;
        }
        const auto plan=findPlan(request,error);if(!plan)return false;
        if(op=="status" || op=="cancel")return true;
        if(op!="execute"){error="Unknown upload operation";return false;}
        if(plan->progress.state==model::State::Prepared && !live(plan,error,true))return false;
        operation.requiredClasses.push_back("read");operation.requiredClasses.push_back("create");
        operation.summary=plan->summary;
        return true;
    }
    bool validateAssistant(const FSAssistantPreparedOperation& operation,std::string& error)const override
    {
        const auto& request=operation.request;const auto op=request["op"].asString();
        if(source().empty() || source()!=operation.validation["source"].asString() || !llsd_equals(material(request),operation.validation["material"]))
        {error="Upload source or material inputs changed";return false;}
        if(op=="preview" || op=="meshReview")return destination(request["folder_id"].asUUID(),error);
        const auto plan=findPlan(request,error);if(!plan)return false;
        return op!="execute" || live(plan,error,true);
    }
    void dispatch(const LLSD& request)
    {
        Response response(LLSD(),request);
        if(!fs_assistant_request_allowed(request))return response.error("Upload request expired or permission changed");
        fs_assistant::AsyncScope scope(fs_assistant_async_context(request));
        if(!fs_assistant::AsyncScope::current())return response.error("Durable assistant authority is required");
        const auto op=request["op"].asString();std::string error;
        if(op=="preview")
        {
            if(!request["files"].isArray() || request["files"].size()==0 || request["files"].size()>static_cast<S32>(model::maxFiles) ||
               (request["max_texture_dimension"].asInteger()!=1024 && request["max_texture_dimension"].asInteger()!=2048))return response.error("Invalid bounded upload manifest");
            if(!destination(request["folder_id"].asUUID(),error))return response.error(error);
            std::size_t staged=0;unsigned prepared=0;
            for(const auto& entry:plans)
            {if(entry.second->progress.state==model::State::Prepared)++prepared;for(const auto& file:entry.second->files)staged+=file.encoded.size();}
            if(prepared>=4 || plans.size()>=32)return response.error("Upload plan capacity reached; cancel or wait for expiration");
            auto plan=std::make_shared<Plan>();plan->id=LLUUID::generateNewID();plan->destination=request["folder_id"].asUUID();
            plan->origin=source();plan->epoch=request["__assistant_epoch"].asString();plan->evidence=destinationEvidence(plan->destination);
            plan->expires=now()+120.;plan->nextOwner=LLFloaterPerms::getNextOwnerPerms("Uploads");plan->group=LLFloaterPerms::getGroupPerms("Uploads");plan->everyone=LLFloaterPerms::getEveryonePerms("Uploads");
            plan->receipt=llsd::map("plan_id",plan->id,"state","prepared","uncertain",false,"files",LLSD::emptyArray(),"created_folders",LLSD::emptyArray(),"viewer_session",FSEventAPIBridge::instance().sessionReference(),"account_id",gAgent.getID(),"grid",LLGridManager::instance().getGrid(),"expires_in_seconds",120);
            S32 total=0;std::size_t bytes=0;std::set<std::string> names,folderPaths;
            for(const auto& input:llsd::inArray(request["files"]))
            {
                const auto count=input["bytes"].asInteger();
                if(count<=0 || static_cast<std::size_t>(count)>model::maxFileBytes || (bytes+=static_cast<std::size_t>(count))>model::maxBatchBytes)
                    return response.error("Source batch exceeds upload byte limits");
                const auto relative=input["relative_folder"].asString();
                for(std::size_t end=relative.find('/');!relative.empty();end=relative.find('/',end+1))
                {folderPaths.insert(relative.substr(0,end));if(end==std::string::npos)break;}
                if(folderPaths.size()>64)return response.error("Batch exceeds 64 inventory folders; split the import");
                File file;if(!encode(input,request["max_texture_dimension"].asInteger(),file,error))return response.error(error);
                if(!fs_assistant::asyncAllowed())return response.error("Upload preview authority changed");
                staged+=file.encoded.size();if(staged>model::maxBatchBytes)return response.error("Encoded upload staging capacity reached");
                if(!names.insert(file.relative+"/"+file.name).second)return response.error("Duplicate inventory names in the same upload folder");
                if(file.cost>std::numeric_limits<S32>::max()-total)return response.error("Upload total exceeds native price bounds");
                total+=file.cost;
                plan->receipt["files"].append(llsd::map("name",file.name,"relative_folder",file.relative,"sha256",input["sha256"],"asset_type",LLAssetType::lookup(file.asset),"cost",file.cost,"width",file.width,"height",file.height,"state","not_submitted"));
                plan->files.push_back(std::move(file));
            }
            plan->receipt["cost"]=total;plan->receipt["folder_id"]=plan->destination;
            plan->receipt["next_owner_mask"]=LLSD::Integer(plan->nextOwner);plan->receipt["group_mask"]=LLSD::Integer(plan->group);plan->receipt["everyone_mask"]=LLSD::Integer(plan->everyone);
            plan->summary=llsd::map("op","execute","action","Upload local files","subject_count",static_cast<S32>(plan->files.size()),"cost",total,"destination",gInventory.getCategory(plan->destination)->getName(),"subjects",LLSD::emptyArray(),"value","Next owner: "+std::to_string(plan->nextOwner)+"; group: "+std::to_string(plan->group)+"; everyone: "+std::to_string(plan->everyone));
            for(const auto& row:llsd::inArray(plan->receipt["files"]))plan->summary["subjects"].append(llsd::map("name",row["name"],"path",row["relative_folder"],"after_name",row["asset_type"].asString()+" · L$"+row["cost"].asString()));
            plans.emplace(plan->id,plan);response.setResponse(plan->receipt);return;
        }
        if(op=="meshReview")
        {
            if(!FSSessionWorker::assistantActiveAllowed() || !destination(request["folder_id"].asUUID(),error))return response.error("Native mesh review requires the active character and a valid destination");
            if(LLFloaterReg::findTypedInstance<LLFloaterModelPreview>("upload_model"))return response.error("The native mesh importer is occupied");
            std::string bytes;if(!verifiedInput(request["file"],bytes,error))return response.error(error);
            // Handoff only: subsequent native review/upload is an explicit resident
            // action, not deferred MCP work running under an expired authority.
            fs_assistant::AsyncScope nativeReview(fs_assistant::AsyncGuard{});
            auto* preview=LLFloaterReg::getTypedInstance<LLFloaterModelPreview>("upload_model");
            if(!preview)return response.error("Native mesh importer unavailable");
            preview->setUploadDestination(request["folder_id"].asUUID());preview->openFloater();
            preview->loadModel(3,request["file"]["path"].asString(),true);
            response.setResponse(llsd::map("state","native_review","uploaded",false,"requires_native_completion",true));return;
        }
        const auto plan=findPlan(request,error);if(!plan)return response.error(error);
        if(op=="execute" && plan->progress.state==model::State::Prepared)
        {
            if(gAgent.getRegionCapability("NewFileAgentInventory").empty())return response.error("This region has no guarded NewFileAgentInventory upload capability");
            if(!live(plan,error,true))return response.error(error);
            plan->progress.start();plan->task=fs_assistant::asyncTask();plan->receipt["operation_id"]=request["reqid"];plan->receipt["state"]="running";schedule(plan);
        }
        else if(op=="cancel")
        {if(plan->task && plan->task->guard())plan->task->guard()->cancel("Upload cancelled by user");stop(plan,"Cancelled; accepted uploads and charges are retained");}
        else if(op!="status" && op!="execute")return response.error("Unknown upload operation");
        response.setResponse(plan->receipt);
    }
};
}
void fs_initialize_file_upload_api(){static FSFileUploadListener listener;}
