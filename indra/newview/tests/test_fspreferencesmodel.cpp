/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "../fspreferencesmodel.h"
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>

static void check(bool value) { if (!value) throw std::runtime_error("Preference transaction regression"); }
using Model = fs_preferences::Transaction<double,std::equal_to<double>>;
using Layers = Model::State;
static fs_preferences::Identity source()
{ return {"account","login","grid","opaque-login","profile","worker","generation",true}; }
struct Store
{
    std::map<std::string,Layers> values {{"a",{1,1,1,false,true}}, {"b",{2,3,0,true,true}}, {"preset",{5,5,0,false,true}}};
    std::vector<std::string> writes;
    bool callbacks = false;
    Layers read(const std::string& key) const { return values.at(key); }
    void write(const std::string& key,double value,bool saved)
    {
        auto& state = values.at(key);
        if (saved) { state.saved = state.effective = value; state.unsaved = false; }
        else if (state.effective != value) { state.effective = value; state.unsaved = true; }
        writes.push_back(key);
        // Native camera/graphics signals invalidate the runtime preset label.
        if (callbacks && key == "a") { values["preset"].effective = 0; values["preset"].unsaved = true; }
    }
};
struct CameraCallbacks
{
    std::map<std::string,Layers> values {{"camera",{1,1,1,false,true}},
        {"label",{10,10,0,false,true}}, {"type",{0,0,0,false,true}}};
    bool writing = false;
    std::set<std::string> owned;
    std::vector<std::string> writes;
    Layers read(const std::string& key) const { return values.at(key); }
    void scope(const std::vector<std::string>& keys) { writing = true; owned = {keys.begin(),keys.end()}; }
    void populatePresetCombo()
    {
        if (read("label").effective == 0 && !fs_preferences::dependentPreviewWrite(writing,owned.count("type") != 0,3.,
            [this](double value,bool saved){write("type",value,saved);})) write("type",3,true);
    }
    void write(const std::string& key,double value,bool saved)
    {
        auto& state = values.at(key);
        const auto previous = state.effective;
        if (saved) { state.saved = state.effective = value; state.unsaved = false; }
        else if (state.effective != value) { state.effective = value; state.unsaved = true; }
        writes.push_back(key);
        // Native chain: settingChanged -> triggerChangeCameraSignal -> the
        // constructed floater's populatePresetCombo -> CameraPresetType write.
        if (key == "camera" && previous != value && read("label").effective != 0)
        {
            if (!fs_preferences::dependentPreviewWrite(writing,owned.count("label") != 0,0.,
                [this](double label,bool persist){write("label",label,persist);})) write("label",0,true);
            populatePresetCombo();
        }
    }
};
int main()
{
    using namespace fs_preferences;
    check(catalog.size() == 16 && MAX_CHANGES == 16 && MAX_TTL == 300);
    for (const auto* op : {"schema","get","status","preview","accept","cancel"}) check(operation(op));
    for (const auto* op : {"set","toggle","groups","vars","saveSettings","apply","jump","grant"}) check(!operation(op));
    for (const auto* key : {"LocalAssistantEnabled","LocalAssistantPermissions","DisableCameraConstraints","AudioLevelMic","VoiceInputAudioDevice","ProxyHost","CacheLocation","MuteVoice"}) check(!descriptor(key));
    check(descriptor("look_at_targets")->account && descriptor("look_at_targets")->type == Type::Integer);
    check(!descriptor("master_volume")->writable && !descriptor("effects_volume")->writable && !descriptor("mute_audio")->writable && !descriptor("graphics_quality")->writable);
    check(numberAllowed(*descriptor("mouse_sensitivity"),0) && numberAllowed(*descriptor("mouse_sensitivity"),15));
    check(!numberAllowed(*descriptor("mouse_sensitivity"),15.1) && !numberAllowed(*descriptor("mouse_sensitivity"),std::numeric_limits<double>::quiet_NaN()));
    check(!numberAllowed(*descriptor("look_at_targets"),.5) && !numberAllowed(*descriptor("camera_fov"),3));
    const WorkspaceOwnership dormant{true,true,true,false,false,true,true,false,false};
    check(dormantWorkspaceBaseline(dormant));
    for (int field = 0; field < 9; ++field)
    {
        auto state = dormant;
        if (field == 0) state.transaction = false;
        if (field == 1) state.ready = false;
        if (field == 2) state.sameIdentity = false;
        if (field == 3) state.nativeVisible = true;
        if (field == 4) state.nativeOpening = true;
        if (field == 5) state.definitionsUnchanged = false;
        if (field == 6) state.activeUnchanged = false;
        if (field == 7) state.edited = true;
        if (field == 8) state.deferred = true;
        check(!dormantWorkspaceBaseline(state));
    }

    Store store; Model tx;
    const auto read = [&store](const std::string& key){return store.read(key);};
    const auto write = [&store](const std::string& key,double value,bool saved){store.write(key,value,saved);};
    const auto allowed = [](const std::string&){return true;};
    check(!tx.begin(source(),"t",0,10,300,{"a"},read));
    check(!tx.begin(source(),"t",tx.revision(),10,301,{"a"},read));
    check(!tx.begin(source(),"t",tx.revision(),10,300,{"a","a"},read));
    check(tx.begin(source(),"t",tx.revision(),10,300,{"a","b"},read));
    check(!tx.begin(source(),"other",tx.revision(),10,300,{"a"},read));
    write("a",4,false); write("b",6,false); tx.remember(read);
    check(store.read("a").saved == 1 && store.read("b").saved == 2);
    const auto previewRevision = tx.revision();
    check(!tx.canAccept(source(),"t",previewRevision-1,11,read));
    check(!tx.canAccept(source(),"wrong",previewRevision,11,read));
    check(!tx.canAccept(source(),"t",previewRevision,310,read));
    check(tx.canAccept(source(),"t",previewRevision,309,read));
    auto cleanup = tx.cancel(source(),read,write,allowed);
    check(cleanup.state == "cancelled" && cleanup.restored.size() == 2 && !tx.busy());
    check(store.read("a").saved == 1 && store.read("a").effective == 1 && !store.read("a").unsaved);
    check(store.read("b").saved == 2 && store.read("b").effective == 3 && store.read("b").unsaved);

    check(tx.begin(source(),"accept",tx.revision(),20,1,{"a","b"},read));
    write("a",4,false); write("b",6,false); tx.remember(read);
    cleanup = tx.accept(source(),"accept",tx.revision(),20.5,read,write);
    check(cleanup.state == "accepted" && !tx.busy());
    check(store.read("a").saved == 4 && store.read("a").effective == 4 && !store.read("a").unsaved);
    check(store.read("b").saved == 6 && !store.read("b").unsaved);

    check(tx.begin(source(),"external",tx.revision(),30,300,{"a","b"},read));
    write("a",8,false); write("b",9,false); tx.remember(read);
    // A saved-layer writer with an unchanged effective value still conflicts.
    store.values["a"].saved = 7;
    check(!tx.unchanged(read));
    const auto writesBeforeConflict = store.writes.size();
    cleanup = tx.accept(source(),"external",tx.revision(),31,read,write);
    check(cleanup.state == "conflict" && tx.busy() && store.writes.size() == writesBeforeConflict);
    cleanup = tx.cancel(source(),read,write,allowed);
    check(cleanup.state == "partially_restored" && cleanup.skipped == std::vector<std::string>{"a"});
    check(store.read("a").saved == 7 && store.read("a").effective == 8 && store.read("b").effective == 6);

    check(tx.begin(source(),"aba",tx.revision(),40,300,{"b"},read));
    write("b",9,false); tx.remember(read);
    tx.changed("b"); // native writer changed away and back before a poll
    check(!tx.unchanged(read));
    cleanup = tx.cancel(source(),read,write,allowed);
    check(cleanup.state == "partially_restored" && store.read("b").effective == 9);

    // Every component of the actual profile/login/bridge binding matters.
    for (int field = 0; field < 8; ++field)
    {
        auto other = source();
        if (field == 0) other.account += "x";
        if (field == 1) other.login += "x";
        if (field == 2) other.grid += "x";
        if (field == 3) other.viewerSession += "x";
        if (field == 4) other.profile += "x";
        if (field == 5) other.worker += "x";
        if (field == 6) other.generation += "x";
        if (field == 7) other.managed = false;
        check(tx.begin(source(),"identity",tx.revision(),50,300,{"a"},read));
        write("a",10,false); tx.remember(read);
        const auto writesBefore = store.writes.size();
        cleanup = tx.cancel(other,read,write,allowed);
        check(cleanup.state == "abandoned_identity_changed" && store.writes.size() == writesBefore && !tx.busy());
    }
    auto invalid = source(); invalid.profile.clear();
    check(!tx.begin(invalid,"invalid",tx.revision(),60,300,{"a"},read));

    // Primary restore invokes preset invalidation; metadata restores last.
    store = Store{}; store.callbacks = true;
    check(tx.begin(source(),"preset",tx.revision(),70,300,{"a","preset"},read));
    write("a",8,false); tx.remember(read);
    cleanup = tx.cancel(source(),read,write,allowed);
    check(cleanup.state == "cancelled" && store.read("preset").saved == 5 && store.read("preset").effective == 5 && !store.read("preset").unsaved);
    check(store.writes.back() == "preset");

    // A native restriction/ownership change can prevent only part of cleanup.
    check(tx.begin(source(),"restricted",tx.revision(),80,300,{"a","b"},read));
    write("a",8,false); write("b",9,false); tx.remember(read);
    cleanup = tx.cancel(source(),read,write,[](const std::string& key){return key != "a";});
    check(cleanup.state == "partially_restored" && cleanup.skipped == std::vector<std::string>{"a"} && store.read("b").effective == 3);

    CameraCallbacks camera; Model cameraTx;
    const auto cameraRead = [&camera](const std::string& key){return camera.read(key);};
    const auto cameraWrite = [&camera](const std::string& key,double value,bool saved){camera.write(key,value,saved);};
    const std::vector<std::string> cameraKeys {"camera","label","type"};
    check(cameraTx.begin(source(),"camera-mode",cameraTx.revision(),90,300,cameraKeys,cameraRead));
    camera.scope(cameraKeys); camera.write("camera",2,false); cameraTx.remember(cameraRead); camera.writing = false;
    check(camera.read("camera").saved == 1 && camera.read("label").saved == 10 && camera.read("type").saved == 0);
    check(camera.read("label").effective == 0 && camera.read("type").effective == 3 && camera.read("type").unsaved);
    camera.scope(cameraTx.owned(cameraRead));
    cleanup = cameraTx.cancel(source(),cameraRead,cameraWrite,allowed); camera.writing = false;
    check(cleanup.state == "cancelled" && camera.read("camera").effective == 1 && camera.read("label").effective == 10);
    check(camera.read("type").saved == 0 && camera.read("type").effective == 0 && !camera.read("type").unsaved && camera.writes.back() == "type");

    // Preserve an already present type override, including its saved layer.
    camera.values["type"] = {0,1,0,true,true};
    check(cameraTx.begin(source(),"camera-override",cameraTx.revision(),91,300,cameraKeys,cameraRead));
    camera.scope(cameraKeys); camera.write("camera",2,false); cameraTx.remember(cameraRead);
    camera.scope(cameraTx.owned(cameraRead));
    cleanup = cameraTx.cancel(source(),cameraRead,cameraWrite,allowed); camera.writing = false;
    check(cleanup.state == "cancelled" && camera.read("type").saved == 0 && camera.read("type").effective == 1 && camera.read("type").unsaved);

    check(cameraTx.begin(source(),"camera-manual",cameraTx.revision(),92,300,cameraKeys,cameraRead));
    camera.scope(cameraKeys); camera.write("camera",2,false); cameraTx.remember(cameraRead); camera.writing = false;
    camera.write("type",4,true); cameraTx.changed("type");
    check(!cameraTx.canAccept(source(),"camera-manual",cameraTx.revision(),93,cameraRead));
    camera.scope(cameraTx.owned(cameraRead));
    camera.populatePresetCombo(); // cleanup callback cannot overwrite manual type
    check(camera.read("type").saved == 4 && camera.read("type").effective == 4);
    cleanup = cameraTx.cancel(source(),cameraRead,cameraWrite,allowed); camera.writing = false;
    check(cleanup.state == "partially_restored" && cleanup.skipped == std::vector<std::string>{"type"});
    check(camera.read("type").saved == 4 && camera.read("type").effective == 4 && !camera.read("type").unsaved);

    // Normal native callbacks retain their saved-layer behavior outside a scope.
    camera.write("label",0,true); camera.populatePresetCombo();
    check(camera.read("type").saved == 3 && !camera.read("type").unsaved);

    camera = CameraCallbacks{};
    check(cameraTx.begin(source(),"camera-accept",cameraTx.revision(),94,300,cameraKeys,cameraRead));
    camera.scope(cameraKeys); camera.write("camera",2,false); cameraTx.remember(cameraRead);
    cleanup = cameraTx.accept(source(),"camera-accept",cameraTx.revision(),95,cameraRead,cameraWrite); camera.writing = false;
    check(cleanup.state == "accepted" && camera.read("camera").saved == 2 && camera.read("label").saved == 0);
    check(camera.read("type").saved == 3 && camera.read("type").effective == 3 && !camera.read("type").unsaved);
}
