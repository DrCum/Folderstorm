/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fspreferenceslistener.h"
#include "fspreferencesmodel.h"
#include "fseventapibridge.h"
#include "fssessionworker.h"
#include "fsworkspacecontroller.h"
#include "fsworkspacecontextadapter.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llapp.h"
#include "llappviewer.h"
#include "llfloaterreg.h"
#include "llperfstats.h"
#include "llpresetsmanager.h"
#include "llsdutil.h"
#include "llstartup.h"
#include "lltimer.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewerdisplay.h"
#include "llviewernetwork.h"
#include "llviewerwindow.h"
#include "rlvactions.h"
#include "rlvhandler.h"

#include <deque>
#include <set>

namespace
{
using namespace fs_preferences;
constexpr const char* CAMERA_PRESET = "@camera_preset";
constexpr const char* CAMERA_PRESET_TYPE = "@camera_preset_type";
constexpr const char* GRAPHICS_PRESET = "@graphics_preset";
struct Equal { bool operator()(const LLSD& a, const LLSD& b) const { return llsd_equals(a,b); } };
using Model = Transaction<LLSD, Equal>;
using State = Model::State;
double now() { return LLTimer::getElapsedSeconds(); }
Identity identity()
{
    const LLSD worker = FSSessionWorker::assistantIdentity();
    return {gAgent.getID().isNull() ? "" : gAgent.getID().asString(),
        gAgent.getSessionID().isNull() ? "" : gAgent.getSessionID().asString(),
        LLGridManager::instance().getGrid(),
        FSEventAPIBridge::instanceExists() ? FSEventAPIBridge::instance().sessionReference() : "",
        worker["profile"].asString(), worker["worker"].asString(), worker["generation"].asString(), FSSessionWorker::managed()};
}
bool nativeReady()
{
    return !LLApp::isExiting() && !gDisconnected && LLStartUp::getStartupState() == STATE_STARTED &&
        gAgent.getRegion() && identity().valid();
}
bool ready()
{
    if (!nativeReady() || !FSEventAPIBridge::instanceExists() || FSSessionWorker::assistantPaused()) return false;
    const auto status = FSEventAPIBridge::instance().getStatusSnapshot();
    return status.enabled && status.ready && identity().valid();
}
LLControlVariable* control(const std::string& key)
{
    if (key == CAMERA_PRESET) return gSavedSettings.getControl("PresetCameraActive");
    if (key == CAMERA_PRESET_TYPE) return gSavedSettings.getControl("CameraPresetType");
    if (key == GRAPHICS_PRESET) return gSavedSettings.getControl("PresetGraphicActive");
    const auto* entry = descriptor(key);
    return entry ? (entry->account ? gSavedPerAccountSettings : gSavedSettings).getControl(entry->control) : nullptr;
}
State read(const std::string& key)
{
    const auto var = control(key);
    return var ? State{var->getSaveValue(),var->getValue(),var->getDefault(),var->hasUnsavedValue(),var->isPersisted()} : State{};
}
bool nativeType(const Descriptor& entry)
{
    const auto var = control(entry.id);
    if (!var || !var->isPersisted()) return false;
    if (entry.type == Type::Boolean) return var->isType(TYPE_BOOLEAN);
    if (entry.type == Type::Real) return var->isType(TYPE_F32);
    return var->isType(entry.effect == Effect::Quality ? TYPE_U32 : TYPE_S32);
}
std::string unavailable(const Descriptor& entry, bool nativeOpening = false, bool cleanup = false)
{
    if (!nativeType(entry)) return "control_unavailable";
    if (!entry.writable) return entry.effect == Effect::Audio ? "output_only_refresh_not_supported" : "native_quality_apply_not_supported";
    if (!(cleanup ? nativeReady() : ready())) return "not_ready";
    if (!nativeOpening && (LLFloaterReg::instanceVisible("preferences") || fs_preferences_native_opening_in_progress())) return "busy_native_preferences";
    if (entry.active && (!gViewerWindow || (!nativeOpening && (gTeleportDisplay || gAgent.getTeleportState() != LLAgent::TELEPORT_NONE)))) return "native_ui_unavailable";
    if (entry.active && !nativeOpening && !FSSessionWorker::assistantActiveAllowed()) return "requires_active_character";
    if (entry.effect == Effect::Camera && !FSWorkspaceContext::cameraAllowed()) return "native_camera_restriction";
    if (entry.effect == Effect::LookAt && (gRlvHandler.hasBehaviour(RLV_BHVR_SHOWNAMES) || !RlvActions::canShowNearbyAgents())) return "native_name_restriction";
    if (entry.effect == Effect::Graphics && (LLPerfStats::tunables.userAutoTuneEnabled || gSavedSettings.getBOOL("AutoTuneFPS") || gSavedSettings.getBOOL("AutoTuneLock"))) return "automatic_graphics_control_active";
    if (entry.effect == Effect::Graphics && (gSavedDrawDistance > 0.f || gSavedSettings.getF32("FSSavedRenderFarClip") > 0.f)) return "draw_distance_stepping_active";
    return {};
}
bool revision(const LLSD& value, std::uint64_t& result)
{
    if (!value.isString()) return false;
    const auto text = value.asString();
    if (text.empty() || text[0] == '0' || text.size() > 19 || text.find_first_not_of("0123456789") != std::string::npos) return false;
    try { result = std::stoull(text); return result != 0; } catch (...) { return false; }
}
bool value(const Descriptor& entry, const LLSD& input, LLSD& output)
{
    if (entry.type == Type::Boolean) { if (!input.isBoolean()) return false; output = input; return true; }
    if ((entry.type == Type::Integer && !input.isInteger()) || (entry.type == Type::Real && !input.isInteger() && !input.isReal()) || !numberAllowed(entry,input.asReal())) return false;
    if (entry.effect == Effect::Camera && std::string(entry.id) == "camera_fov" &&
        (input.asReal() < LLViewerCamera::getInstance()->getMinView() || input.asReal() > LLViewerCamera::getInstance()->getMaxView())) return false;
    output = entry.type == Type::Integer ? LLSD(input.asInteger()) : LLSD(static_cast<F32>(input.asReal()));
    return true;
}
LLSD layers(const State& state)
{
    return llsd::map("saved",state.saved,"effective",state.effective,"default",state.defaultValue,
        "unsaved",state.unsaved,"persisted",state.persisted);
}
std::string label(const LLSD& value)
{
    if (value.isBoolean()) return value.asBoolean() ? "true" : "false";
    std::string result = value.asString().substr(0,128);
    std::replace(result.begin(),result.end(),'\n',' ');
    std::replace(result.begin(),result.end(),'\r',' ');
    return value.isString() ? "\"" + result + "\"" : result;
}
std::string label(const State& state)
{ return "saved " + label(state.saved) + ", effective " + label(state.effective) + (state.unsaved ? " (runtime override)" : ""); }
LLSD material(const LLSD& request)
{
    LLSD result = llsd::map("op",request["op"]);
    for (const char* key : {"ids","changes","expected_revision","transaction_id","ttl_seconds"})
        if (request.has(key)) result[key] = request[key];
    return result;
}
struct Service
{
    Model transaction;
    Identity source;
    std::map<std::string,State> observed;
    std::map<std::string,boost::signals2::scoped_connection> connections;
    std::map<std::string,LLControlVariable*> connected;
    bool writing = false, cameraDirtyBefore = false, cameraTouched = false;
    unsigned int nativeOpening = 0;
    std::set<std::string> metadataAllowed;
    int policyGeneration = 0;
    Cleanup last;
    std::string lastReason;
    struct Prepared { Identity source; LLSD material; bool executed = false; };
    std::map<std::string,Prepared> prepared;
    std::deque<std::string> order;

    void observe()
    {
        std::vector<std::string> keys;
        for (const auto& entry : catalog) keys.emplace_back(entry.id);
        keys.emplace_back(CAMERA_PRESET); keys.emplace_back(CAMERA_PRESET_TYPE); keys.emplace_back(GRAPHICS_PRESET);
        for (const auto& key : keys)
        {
            auto* var = control(key);
            if (!var) continue;
            if (connected[key] != var)
            {
                connected[key] = var;
                // Catalog/control groups are stable for the process. A replaced
                // account control gets a new connection and a fresh revision.
                connections[key] = var->getCommitSignal()->connect([this,key](LLControlVariable*,const LLSD&,const LLSD&)
                { if (!writing) transaction.changed(key); });
            }
            const auto current = read(key);
            const auto previous = observed.find(key);
            if (previous != observed.end() && !transaction.same(previous->second,current)) transaction.changed(key);
            observed[key] = current;
        }
    }
    void remember()
    {
        for (auto& entry : observed) entry.second = read(entry.first);
    }
    void write(const std::string& key, const LLSD& val, bool saved)
    {
        if (auto* var = control(key)) var->setValue(val,saved);
        const auto* entry = descriptor(key);
        if (entry && entry->effect == Effect::UI && gViewerWindow) gViewerWindow->requestResolutionUpdate();
    }
    struct WriteScope
    {
        Service& self;
        explicit WriteScope(Service& service,const std::vector<std::string>& owned) : self(service)
        { self.writing = true; self.metadataAllowed = {owned.begin(),owned.end()}; }
        ~WriteScope() { self.metadataAllowed.clear(); self.writing = false; self.remember(); }
    };
    void cancel(const std::string& reason, bool opening = false)
    {
        if (!transaction.busy()) return;
        const auto current = identity();
        auto owned = current == transaction.source() ? transaction.owned(read) : std::vector<std::string>();
        for (const auto effect : {Effect::Camera,Effect::Graphics})
            for (const auto& entry : transaction.entries())
                if (const auto* desc = descriptor(entry.key))
                    if (desc->effect == effect && (std::find(owned.begin(),owned.end(),entry.key) == owned.end() || !unavailable(*desc,opening,true).empty()))
                    {
                        // A label cannot be restored while any associated
                        // draft value remains unrestored or externally owned.
                        const std::string metadata = effect == Effect::Camera ? CAMERA_PRESET : GRAPHICS_PRESET;
                        owned.erase(std::remove(owned.begin(),owned.end(),metadata),owned.end());
                        if (effect == Effect::Camera) owned.erase(std::remove(owned.begin(),owned.end(),CAMERA_PRESET_TYPE),owned.end());
                    }
        bool restoreDirty = cameraTouched;
        for (const auto& entry : transaction.entries())
            if (const auto* desc = descriptor(entry.key)) if (desc->effect == Effect::Camera && (entry.external || std::find(owned.begin(),owned.end(),entry.key) == owned.end())) restoreDirty = false;
        const bool dirtyOwned = restoreDirty && LLPresetsManager::instance().isCameraDirty();
        WriteScope scope(*this,owned);
        last = transaction.cancel(current,read,[this](const std::string& key,const LLSD& val,bool saved){write(key,val,saved);},
            [opening,&owned](const std::string& key)
            {
                if (const auto* entry = descriptor(key)) return unavailable(*entry,opening,true).empty();
                return nativeReady() && std::find(owned.begin(),owned.end(),key) != owned.end();
            });
        if (dirtyOwned && current == source && last.skipped.empty()) LLPresetsManager::setCameraDirty(cameraDirtyBefore);
        cameraTouched = false; lastReason = reason;
    }
    void maintain()
    {
        // Native setting signals can synchronously query workspace/UI state.
        // They must not mistake our in-progress writes for an external writer.
        if (writing) return;
        const auto current = identity();
        if (!(current == source))
        {
            // A new account/profile must never receive old saved/runtime layers.
            if (transaction.busy()) cancel("identity_changed");
            source = current; prepared.clear(); order.clear(); last = Cleanup{}; lastReason.clear(); observed.clear();
        }
        observe();
        if (!transaction.busy()) return;
        if (!ready()) { cancel("not_ready"); return; }
        if (FSEventAPIBridge::getPolicyGeneration() != policyGeneration || FSEventAPIBridge::getPermissionLevel("preferences_write") == "deny" || FSEventAPIBridge::getPermissionLevel("preferences_read") != "allow") { cancel("authority_changed"); return; }
        if (now() >= transaction.deadline()) { cancel("expired"); return; }
        if (LLFloaterReg::instanceVisible("preferences")) { cancel("native_preferences_opened",true); return; }
        for (const auto& entry : transaction.entries())
            if (const auto* desc = descriptor(entry.key)) if (!unavailable(*desc).empty()) { cancel("native_availability_changed"); return; }
    }
    LLSD base() const
    {
        // Never publish native login/session credentials.
        LLSD result = llsd::map("preferences_tools",1,"schema_version",SCHEMA_VERSION,"revision",std::to_string(transaction.revision()),
            "viewer_session",source.viewerSession,"account_id",source.account,"grid",source.grid);
        if (!source.profile.empty()) result["profile"] = source.profile;
        result["native_preferences_open"] = LLFloaterReg::instanceVisible("preferences");
        result["preview_active"] = transaction.busy();
        if (transaction.busy()) { result["transaction_id"] = transaction.id(); result["expires_in_seconds"] = (std::max)(0.,transaction.deadline()-now()); }
        result["last_cleanup"]["state"] = last.state; result["last_cleanup"]["reason"] = lastReason;
        result["last_cleanup"]["restored_count"] = static_cast<S32>(last.restored.size());
        result["last_cleanup"]["skipped_count"] = static_cast<S32>(last.skipped.size());
        result["last_cleanup"]["skipped"] = LLSD::emptyArray();
        for (const auto& key : last.skipped) result["last_cleanup"]["skipped"].append(key);
        return result;
    }
    LLSD rows(const std::vector<std::string>& ids, bool schema) const
    {
        LLSD rows = LLSD::emptyArray();
        for (const auto& id : ids)
        {
            const auto& entry = *descriptor(id);
            LLSD row = llsd::map("id",id,"storage",entry.account ? "account" : "private_profile");
            const auto reason = unavailable(entry);
            row["read_available"] = nativeType(entry); row["write_available"] = reason.empty();
            row["unavailable_reason"] = reason; row["restart_required"] = false;
            if (schema)
            {
                row["type"] = entry.type == Type::Boolean ? "boolean" : entry.type == Type::Real ? "number" : "integer";
                if (entry.type != Type::Boolean) { row["minimum"] = entry.minimum; row["maximum"] = entry.maximum; }
                if (entry.effect == Effect::Camera && id == "camera_fov")
                { row["minimum"] = (std::max)(entry.minimum,static_cast<double>(LLViewerCamera::getInstance()->getMinView())); row["maximum"] = (std::min)(entry.maximum,static_cast<double>(LLViewerCamera::getInstance()->getMaxView())); }
                row["preview_supported"] = entry.writable; row["requires_active_character"] = entry.active;
                if (id == "quick_jump") row["description"] = "Skip the finish animation for future jumps; does not jump or move the avatar";
                if (id == "selection_beam") row["description"] = "Local selection-beam display; does not suppress outgoing beam effects";
            }
            else if (nativeType(entry)) row["layers"] = layers(read(id));
            rows.append(row);
        }
        return rows;
    }
    LLSD draft() const
    {
        LLSD rows = LLSD::emptyArray();
        for (const auto& entry : transaction.entries())
        {
            LLSD row = llsd::map("id",entry.key,"before",layers(entry.before),"after",layers(entry.expected),"external_conflict",entry.external || !transaction.same(read(entry.key),entry.expected));
            rows.append(row);
        }
        return rows;
    }
};
Service& service() { static Service value; return value; }

bool arguments(const LLSD& request, std::vector<std::string>& ids, LLSD& changes, std::uint64_t& expected, int& ttl, std::string& error)
{
    const auto op = request["op"].asString();
    if (!operation(op)) { error = "invalid_args: unknown preferences operation"; return false; }
    ttl = 120;
    if (op == "schema" || op == "status") return true;
    if (op == "get")
    {
        if (!request.has("ids")) { for (const auto& entry : catalog) ids.emplace_back(entry.id); return true; }
        if (!request["ids"].isArray() || request["ids"].size() < 1 || request["ids"].size() > MAX_CHANGES) { error = "invalid_args: ids must contain 1..16 curated IDs"; return false; }
        for (const auto& item : llsd::inArray(request["ids"]))
        {
            if (!item.isString() || !descriptor(item.asString()) || std::find(ids.begin(),ids.end(),item.asString()) != ids.end()) { error = "invalid_args: unknown or duplicate preference ID"; return false; }
            ids.push_back(item.asString());
        }
        return true;
    }
    if (!revision(request["expected_revision"],expected)) { error = "invalid_args: exact expected_revision from preferences_get/status is required"; return false; }
    if (op == "accept" || op == "cancel")
    {
        if (!request["transaction_id"].isString() || request["transaction_id"].asString().empty() || request["transaction_id"].asString().size() > 64) { error = "invalid_args: exact transaction_id is required"; return false; }
        return true;
    }
    if (request.has("transaction_id")) { error = "busy: finish the current preview before starting another"; return false; }
    if (request.has("ttl_seconds"))
    {
        if (!request["ttl_seconds"].isInteger()) { error = "invalid_args: ttl_seconds must be an integer"; return false; }
        ttl = request["ttl_seconds"].asInteger();
    }
    if (ttl < 1 || ttl > MAX_TTL || !request["changes"].isMap() || request["changes"].size() < 1 || request["changes"].size() > MAX_CHANGES) { error = "invalid_args: preview needs 1..16 curated changes and ttl_seconds 1..300"; return false; }
    changes = LLSD::emptyMap();
    for (auto it = request["changes"].beginMap(); it != request["changes"].endMap(); ++it)
    {
        const auto* entry = descriptor(it->first); LLSD canonical;
        if (!entry || !entry->writable || !value(*entry,it->second,canonical)) { error = "invalid_args: unknown, read-only or out-of-range curated preference"; return false; }
        ids.push_back(it->first); changes[it->first] = canonical;
    }
    return true;
}
bool available(const LLSD& request, const std::vector<std::string>& ids, std::uint64_t expected, std::string& error)
{
    auto& self = service();
    if (!ready()) { error = "not_ready: preferences require the exact ready login"; return false; }
    if (!mutation(request["op"].asString())) return true;
    if (LLFloaterReg::instanceVisible("preferences") || self.nativeOpening) { error = "busy: finish native Preferences before changing MCP preferences"; return false; }
    if (request["op"].asString() == "preview" && FSWorkspaceController::instance().preferencesBusy()) { error = "busy: a workspace transaction or deferred placement owns viewer settings"; return false; }
    if (expected != self.transaction.revision()) { error = "conflict: preference revision changed; read current values"; return false; }
    const auto op = request["op"].asString();
    if (op == "preview")
    {
        if (self.transaction.busy()) { error = "busy: another preview owns preferences"; return false; }
        for (const auto& id : ids) if (const auto reason = unavailable(*descriptor(id)); !reason.empty()) { error = reason + ": preference is unavailable"; return false; }
    }
    else
    {
        if (!self.transaction.busy() || request["transaction_id"].asString() != self.transaction.id() || !(identity() == self.transaction.source())) { error = "conflict: transaction does not belong to this login"; return false; }
        if (op == "accept")
        {
            if (!self.transaction.canAccept(identity(),request["transaction_id"].asString(),expected,now(),read)) { error = "conflict: draft changed externally or expired"; return false; }
            for (const auto& entry : self.transaction.entries()) if (const auto* desc = descriptor(entry.key)) if (const auto reason = unavailable(*desc); !reason.empty()) { error = reason + ": draft is unavailable"; return false; }
        }
    }
    return true;
}
}

FSPreferencesListener::FSPreferencesListener() : LLEventAPI("FSPreferences","Curated profile-bound preference transactions")
{
    for (const char* op : {"schema","get","status","preview","accept","cancel"}) add(op,"Curated preferences; separate read/write permissions",&FSPreferencesListener::dispatch);
}
LLSD FSPreferencesListener::assistantLimits() const
{ return llsd::map("version",1,"schema_version",SCHEMA_VERSION,"max_changes",static_cast<S32>(MAX_CHANGES),"max_ttl_seconds",MAX_TTL,"max_previews",1,"arbitrary_settings",false); }
bool FSPreferencesListener::prepareAssistant(FSAssistantPreparedOperation& operation, std::string& error) const
{
    auto& self = service(); self.maintain();
    std::vector<std::string> ids; LLSD changes; std::uint64_t expected = 0; int ttl;
    if (!arguments(operation.request,ids,changes,expected,ttl,error) || !available(operation.request,ids,expected,error)) return false;
    const bool write = mutation(operation.request["op"].asString());
    operation.requiredClasses = write ? std::vector<std::string>{"preferences_write","preferences_read"} : std::vector<std::string>{"preferences_read"};
    const auto nonce = LLUUID::generateNewID().asString();
    while (self.order.size() >= 64) { self.prepared.erase(self.order.front()); self.order.pop_front(); }
    self.order.push_back(nonce); self.prepared.emplace(nonce,Service::Prepared{identity(),material(operation.request),false});
    operation.validation = llsd::map("preferences_prepared",nonce);
    operation.request["__preferences_prepared"] = nonce;
    operation.summary = llsd::map("op",std::string("preferences_")+operation.request["op"].asString(),"subject_count",static_cast<S32>(ids.size()),"subjects",LLSD::emptyArray());
    if (write && operation.request["op"].asString() != "preview")
        for (const auto& entry : self.transaction.entries())
        {
            const auto after = operation.request["op"].asString() == "cancel" ? label(entry.before) : "saved and effective " + label(entry.expected.effective);
            LLSD row = llsd::map("name",entry.key,"before_name",label(read(entry.key)),"after_name",after); operation.summary["subjects"].append(row);
        }
    else for (const auto& id : ids)
    { LLSD row = llsd::map("name",id); if (write) { row["before_name"] = label(read(id)); row["after_name"] = "runtime " + label(changes[id]) + "; saved value unchanged"; } operation.summary["subjects"].append(row); }
    if (write && operation.request["op"].asString() == "preview")
        for (const auto effect : {Effect::Camera,Effect::Graphics})
            if (std::any_of(ids.begin(),ids.end(),[effect](const std::string& id){return descriptor(id)->effect == effect;}))
            {
                const std::string key = effect == Effect::Camera ? CAMERA_PRESET : GRAPHICS_PRESET;
                operation.summary["subjects"].append(llsd::map("name",key,"before_name",label(read(key)),"after_name","preset label may clear at runtime; saved value unchanged"));
                if (effect == Effect::Camera) operation.summary["subjects"].append(llsd::map("name",CAMERA_PRESET_TYPE,"before_name",label(read(CAMERA_PRESET_TYPE)),"after_name","camera preset type may become Custom at runtime; saved value unchanged"));
            }
    operation.summary["subject_count"] = static_cast<S32>(operation.summary["subjects"].size());
    return true;
}
bool FSPreferencesListener::validateAssistant(const FSAssistantPreparedOperation& operation, std::string& error) const
{
    auto& self = service(); self.maintain();
    const auto found = self.prepared.find(operation.validation["preferences_prepared"].asString());
    if (found == self.prepared.end() || !(found->second.source == identity()) || !llsd_equals(found->second.material,material(operation.request)) || !ready()) { error = "session_changed: preference source or approved material changed"; return false; }
    // Synchronous dispatch changes the revision itself. Keep the source check
    // for its delivery receipt without re-running its pre-mutation baseline.
    if (found->second.executed) return true;
    std::vector<std::string> ids; LLSD changes; std::uint64_t expected = 0; int ttl;
    if (!arguments(operation.request,ids,changes,expected,ttl,error)) return false;
    return available(operation.request,ids,expected,error);
}
void FSPreferencesListener::dispatch(const LLSD& request)
{
    Response response(LLSD(),request);
    if (!request.has("__assistant_guard") || !fs_assistant_request_allowed(request)) return response.error("session_changed: guarded bridge authority is required");
    const auto guard = fs_assistant_async_context(request);
    fs_assistant::AsyncScope scope(guard);
    if (!guard || !fs_assistant::asyncAllowed()) return response.error("session_changed: assistant authority expired");
    auto& self = service(); self.maintain();
    const auto nonce = request["__preferences_prepared"].asString();
    const auto prepared = self.prepared.find(nonce);
    if (prepared == self.prepared.end() || !(prepared->second.source == identity()) || !llsd_equals(prepared->second.material,material(request)) || request["__assistant_epoch"].asString() != identity().viewerSession) return response.error("session_changed: exact prepared source and material are required");
    std::vector<std::string> ids; LLSD changes; std::uint64_t expected = 0; int ttl; std::string error;
    if (!arguments(request,ids,changes,expected,ttl,error) || !available(request,ids,expected,error)) return response.error(error);
    const auto op = request["op"].asString();
    if (mutation(op) && (FSEventAPIBridge::getPermissionLevel("preferences_write") == "deny" || FSEventAPIBridge::getPermissionLevel("preferences_read") != "allow")) return response.error("not_permitted: preference authority was revoked");
    LLSD result;
    if (op == "preview")
    {
        std::vector<std::string> keys = ids;
        bool camera = false, graphics = false;
        for (const auto& id : ids) { camera |= descriptor(id)->effect == Effect::Camera; graphics |= descriptor(id)->effect == Effect::Graphics; }
        // The label's native camera-floater callback can also change the type.
        // Restore that dependent type after the label and all primary values.
        if (camera) { keys.emplace_back(CAMERA_PRESET); keys.emplace_back(CAMERA_PRESET_TYPE); }
        if (graphics) keys.emplace_back(GRAPHICS_PRESET);
        for (const auto& key : keys)
        {
            auto* var = control(key);
            if (!var || !var->isPersisted() || ((key == CAMERA_PRESET || key == GRAPHICS_PRESET) && !var->isType(TYPE_STRING))) return response.error("unsupported: required native control is missing or has changed type");
            if (key == CAMERA_PRESET_TYPE && !var->isType(TYPE_U32)) return response.error("unsupported: native camera preset type has changed type");
        }
        if (!fs_assistant::asyncAllowed()) return response.error("session_changed: preview authority changed");
        if (!FSWorkspaceController::instance().preparePreferencesPreview()) return response.error("busy: a workspace transaction owns viewer settings");
        if (!self.transaction.begin(identity(),LLUUID::generateNewID().asString(),expected,now(),ttl,keys,read)) return response.error("conflict: preview could not acquire this revision");
        self.policyGeneration = FSEventAPIBridge::getPolicyGeneration();
        self.cameraTouched = camera; self.cameraDirtyBefore = LLPresetsManager::instance().isCameraDirty();
        {
            Service::WriteScope writing(self,keys);
            for (const auto& id : ids) self.write(id,changes[id],false);
            self.transaction.remember(read);
        }
        for (const auto& id : ids) if (!llsd_equals(read(id).effective,changes[id]))
        { self.cancel("native_preview_refused"); return response.error("conflict: native control refused the exact draft; inspect status for restoration"); }
        for (const auto& entry : self.transaction.entries())
            if (!llsd_equals(read(entry.key).saved,entry.before.saved) || read(entry.key).persisted != entry.before.persisted || !llsd_equals(read(entry.key).defaultValue,entry.before.defaultValue))
            { self.cancel("native_preview_changed_saved_layer"); return response.error("conflict: native preview changed a saved/default layer; inspect status for restoration"); }
        result = self.base(); result["status"] = "previewed"; result["changes"] = self.draft();
    }
    else if (op == "accept")
    {
        if (!fs_assistant::asyncAllowed()) return response.error("session_changed: acceptance authority changed");
        const auto draft = self.draft();
        Service::WriteScope writing(self,self.transaction.owned(read));
        self.last = self.transaction.accept(identity(),request["transaction_id"].asString(),expected,now(),read,[&self](const std::string& key,const LLSD& val,bool saved){self.write(key,val,saved);});
        self.lastReason = "explicit_mcp_accept"; self.cameraTouched = false;
        result = self.base(); result["status"] = self.last.state; result["accepted_draft"] = draft;
        result["persistence"] = "saved_control_layers; normal viewer settings-file lifecycle";
    }
    else if (op == "cancel")
    { self.cancel("explicit_mcp_cancel"); result = self.base(); result["status"] = self.last.state; }
    else
    {
        result = self.base(); result["status"] = "observed";
        if (op == "schema") { for (const auto& entry : catalog) ids.emplace_back(entry.id); result["entries"] = self.rows(ids,true); }
        if (op == "get") result["entries"] = self.rows(ids,false);
        if (op == "status" && self.transaction.busy()) result["changes"] = self.draft();
    }
    self.prepared[nonce].executed = true;
    if (!(prepared->second.source == identity()) || !fs_assistant_request_allowed(request) || FSEventAPIBridge::getPermissionLevel("preferences_read") != "allow") return response.error("session_changed: preference result authority changed");
    response.setResponse(result);
}
void fs_initialize_preferences_api() { static FSPreferencesListener listener; }
void fs_preferences_tick() { service().maintain(); }
bool fs_preferences_busy() { service().maintain(); return service().transaction.busy(); }
void fs_preferences_native_opening()
{ service().observe(); service().cancel("native_preferences_opened",true); ++service().nativeOpening; }
void fs_preferences_native_opened() { if (service().nativeOpening) --service().nativeOpening; }
bool fs_preferences_native_opening_in_progress() { return service().nativeOpening != 0; }
bool fs_preferences_preset_write(const std::string& name,const std::string& value)
{
    auto& self = service();
    if (!self.writing || (name != "PresetCameraActive" && name != "PresetGraphicActive")) return false;
    const std::string key = name == "PresetCameraActive" ? CAMERA_PRESET : GRAPHICS_PRESET;
    if (self.metadataAllowed.count(key)) if (auto* var = control(key)) var->setValue(value,false);
    return true;
}
bool fs_preferences_preset_invalidated(bool camera)
{
    auto& self = service();
    if (self.writing || !self.transaction.busy()) return false;
    const std::string key = camera ? CAMERA_PRESET : GRAPHICS_PRESET;
    for (const auto& entry : self.transaction.entries())
        if (entry.key == key)
        {
            const bool hidden = !entry.external && self.transaction.same(read(key),entry.expected) &&
                !entry.before.effective.asString().empty() && entry.expected.effective.asString().empty();
            self.transaction.changed(key);
            if (camera) self.transaction.changed(CAMERA_PRESET_TYPE);
            return hidden;
        }
    return false;
}
bool fs_preferences_camera_preset_type_write(S32 value)
{
    auto& self = service();
    return dependentPreviewWrite(self.writing,self.metadataAllowed.count(CAMERA_PRESET_TYPE) != 0,value,
        [](S32 type,bool saved)
        {
            if (auto* var = control(CAMERA_PRESET_TYPE)) var->setValue(LLSD(type),saved);
        });
}
void fs_preferences_camera_ui_opening()
{
    auto& self = service();
    cancelForCameraUI(self.writing,self.transaction.busy(),self.cameraTouched,[&self]()
    {
        self.observe();
        self.cancel("native_camera_ui_opened");
    });
}
