"""Execute the actual deferred startup UI/lease code with small platform stubs.

This is a focused lifecycle regression, not a Windows UI or renderer test.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

VIEWER = Path(__file__).resolve().parents[1]


def block(source, signature):
    begin = source.index(signature)
    start = source.index("{", begin)
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end]


def without_includes(source):
    return "\n".join(line for line in source.splitlines()
                     if not line.startswith(("#include", "#pragma once")))


class StartupPrompts(unittest.TestCase):
    def run_cpp(self, source):
        with tempfile.TemporaryDirectory(prefix="folderstorm-startup-") as temporary:
            cpp = Path(temporary) / "test.cpp"
            cpp.write_text(source)
            exe = cpp.with_suffix("")
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                "-I"+str(VIEWER), str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)

    def test_viewer_prompt_lifecycle(self):
        source = r'''
#include "fsupdate.h"
#include "fscrash.h"
#include <cassert>
#include <map>
#include <memory>
#include <utility>
#include <vector>
using KEY=int;using MASK=int;
constexpr int KEY_ESCAPE=27,MASK_NONE=0,STATE_LOGIN_WAIT=5,STATE_STARTED=10;
struct LLSD {
    bool flag=false;std::string text;
    LLSD()=default;LLSD(bool f):flag(f){}LLSD(const char* t):text(t){}LLSD(std::string t):text(std::move(t)){}
    bool asBoolean()const{return flag;}
    LLSD& operator[](const char*){return *this;}
};
struct LLUICtrl {LLSD value;virtual ~LLUICtrl()=default;void setValue(LLSD v){value=std::move(v);}LLSD getValue(){return value;}};
struct Widget:LLUICtrl {
    bool visible=true,focus=false;std::string label;
    void setVisible(bool v){visible=v;}void setFocus(bool v){focus=v;}void setLabel(std::string v){label=std::move(v);}
    void setCommitCallback(std::function<void(LLUICtrl*,const LLSD&)>){ }
};
using LLButton=Widget;using LLCheckBoxCtrl=Widget;
struct LLFloater {
    bool visible=false;std::string title,defaultButton;std::map<std::string,Widget> widgets;
    explicit LLFloater(const LLSD&){}virtual ~LLFloater()=default;
    virtual bool postBuild(){return true;}virtual void onClose(bool){}virtual bool handleKeyHere(KEY,MASK){return false;}
    template<class T>T* getChild(const char* name){return static_cast<T*>(&widgets[name]);}
    void setTitle(std::string v){title=std::move(v);}void setDefaultBtn(const char* name){defaultButton=name;}
    void openFloater(){visible=true;}void closeFloater(bool quitting=false){onClose(quitting);visible=false;}
};
struct LLApp {inline static bool quitting=false;static bool isQuitting(){return quitting;}};
struct FSSessionWorker {inline static bool worker=false;static bool managed(){return worker;}};
struct LLStartUp {inline static int state=0;static int getStartupState(){return state;}};
struct LLNotificationsUtil {inline static int errors=0;static void add(const char*,const LLSD&){++errors;}};
struct LLFloaterReg {
    inline static std::unique_ptr<LLFloater> instance;inline static bool allowed=true;
    template<class T>static T* getTypedInstance(const char*){
        if(!instance){instance=std::make_unique<T>(LLSD{});instance->postBuild();}return static_cast<T*>(instance.get());}
    template<class T>static T* findTypedInstance(const char*){return static_cast<T*>(instance.get());}
    static bool instanceVisible(const char*){return instance&&instance->visible;}
    static LLFloater* showInstance(const char*){if(!allowed)return nullptr;if(instance)instance->visible=true;return instance.get();}
};
namespace fs_update {
int prepared=0,responses=0;bool saved=false,fail=false;std::uint32_t choices=15;std::optional<std::uint32_t> answer;
std::string lastError(){return fail?"shortcut failure":"";}
std::optional<ShortcutPrompt> shortcutPrompt(bool manual){
    ++prepared;if(saved&&!manual)return {};
    return ShortcutPrompt{choices,[](std::optional<std::uint32_t> choice){
        ++responses;answer=choice;if(fail)return false;if(choice)choices=*choice;else if(!saved)choices=0;saved=true;return true;}};
}
}
namespace fs_crash {
int prepared=0,responses=0;bool saved=false,answer=false;
std::optional<ConsentPrompt> consentPrompt(){
    ++prepared;if(saved)return {};
    return ConsentPrompt{[](std::optional<bool> choice){++responses;if(choice){saved=true;answer=*choice;}return true;}};
}
}
''' + without_includes((VIEWER / "fsinstallviewer.h").read_text())
        source += without_includes((VIEWER / "fsinstallviewer.cpp").read_text())
        source += r'''
int main(){
    using namespace FSInstallViewer;
    poll();assert(fs_update::prepared==0); // init/pre-login must never open UI.
    LLStartUp::state=STATE_LOGIN_WAIT+1;poll();assert(fs_update::prepared==0); // Wait out auto-login progress.
    LLStartUp::state=STATE_LOGIN_WAIT;FSSessionWorker::worker=true;poll();assert(fs_update::prepared==0);
    FSSessionWorker::worker=false;LLApp::quitting=true;poll();assert(fs_update::prepared==0);LLApp::quitting=false;
    poll();assert(LLFloaterReg::instanceVisible("folderstorm_setup")&&fs_update::prepared==1);
    auto* ui=LLFloaterReg::getTypedInstance<FSFloaterSetup>("folderstorm_setup");
    assert(ui->title=="Folderstorm shortcuts");
    ui->getChild<Widget>("desktop_host")->setValue(false);
    FSInstallViewer::shortcuts();assert(fs_update::prepared==1); // Help does not overwrite the open draft.
    for(int frames=0;frames<5000;++frames)poll(); // Main-loop work keeps running while question remains open.
    assert(fs_update::responses==0&&fs_crash::prepared==0);
    ui->visible=false;poll();assert(fs_crash::prepared==0); // Temporarily hiding UI is not a response.
    FSInstallViewer::shortcuts();assert(ui->visible&&fs_update::prepared==1);
    ui->getChild<Widget>("accept")->setFocus(true);
    // Actual production button handler; private access is not needed to activate its registered callback below.
    ui->handleKeyHere(KEY_ESCAPE,MASK_NONE);assert(fs_update::responses==1&&!fs_update::answer&&fs_update::saved);
    poll();assert(ui->title=="Folderstorm crash reporting");
    assert(ui->defaultButton=="later"&&ui->getChild<Widget>("later")->focus);
    assert(!ui->getChild<Widget>("desktop_host")->visible);
    ui->handleKeyHere(KEY_ESCAPE,MASK_NONE);assert(fs_crash::saved&&!fs_crash::answer);
    poll();poll();assert(fs_crash::prepared==1); // Explicit decline does not repeat.
    FSInstallViewer::shortcuts();assert(fs_update::prepared==2);
    ui->closeFloater();assert(fs_update::choices==0); // Later cancel preserves saved choices.
    FSInstallViewer::shortcuts();const auto responses=fs_update::responses;
    ui->closeFloater(true);assert(fs_update::responses==responses); // Shutdown is not a decision.
    fs_crash::saved=false;stage=Stage::Consent;poll();
    const auto before=fs_crash::responses;ui->closeFloater(true);assert(fs_crash::responses==before);
    LLFloaterReg::allowed=false;FSInstallViewer::shortcuts();assert(!ui->pending()&&fs_update::responses==responses);
    stage=Stage::Consent;poll();assert(!ui->pending()&&fs_crash::responses==before); // Respect native floater restrictions.
    LLFloaterReg::allowed=true;FSInstallViewer::shortcuts();assert(ui->pending());ui->closeFloater(true);
}
'''
        # Keep production callbacks executable rather than simulating accept().
        source = source.replace('void setCommitCallback(std::function<void(LLUICtrl*,const LLSD&)>){ }',
                                'std::function<void(LLUICtrl*,const LLSD&)> callback;'
                                'void setCommitCallback(std::function<void(LLUICtrl*,const LLSD&)> cb){callback=std::move(cb);}')
        source = source.replace('    FSInstallViewer::shortcuts();const auto responses=', r'''
    FSInstallViewer::shortcuts();
    ui->getChild<Widget>("desktop_viewer")->setValue(true);
    ui->getChild<Widget>("start_host")->setValue(true);
    ui->getChild<Widget>("accept")->callback(nullptr,LLSD{});assert(fs_update::choices==9&&fs_update::answer==9);
    FSInstallViewer::shortcuts();fs_update::fail=true;
    ui->getChild<Widget>("accept")->callback(nullptr,LLSD{});assert(LLNotificationsUtil::errors==1);
    fs_update::fail=false;
    FSInstallViewer::shortcuts();const auto responses=''')
        source = source.replace('    const auto before=fs_crash::responses;', r'''
    ui->getChild<Widget>("accept")->callback(nullptr,LLSD{});assert(fs_crash::saved&&fs_crash::answer);
    fs_crash::saved=false;stage=Stage::Consent;poll();
    const auto before=fs_crash::responses;''')
        self.run_cpp(source)

    def test_actual_deferred_consent_lease(self):
        source = r'''
#include "fscrash.h"
#include <cassert>
#include <memory>
using HANDLE=int;constexpr int FALSE=0,WAIT_OBJECT_0=0,WAIT_ABANDONED=128;
int releases=0,closes=0;bool available=true,occupied=false,writeOk=true;
HANDLE CreateMutexW(void*,int,const wchar_t*){return 1;}
int WaitForSingleObject(HANDLE,int){if(occupied)return 258;occupied=true;return WAIT_OBJECT_0;}
void ReleaseMutex(HANDLE){assert(occupied);occupied=false;++releases;}void CloseHandle(HANDLE){++closes;}
namespace fs_crash {
bool started=true;std::string processRole="viewer";Consent saved;
bool configured(){return available;}Consent consent(){return saved;}
bool setConsent(bool enabled){assert(occupied);if(!writeOk)return false;saved={enabled,std::string(32,'a')};return true;}
#define _WIN32 1
''' + block((VIEWER / "fscrash.cpp").read_text(), "std::optional<ConsentPrompt> consentPrompt()") + r'''
}
int main(){using namespace fs_crash;
    available=false;assert(!consentPrompt()&&closes==0);available=true;
    processRole="worker";assert(!consentPrompt());processRole="viewer";
    {auto prompt=consentPrompt();assert(prompt&&occupied&&!saved.enabled);
     assert(!consentPrompt());assert(prompt->respond(false));assert(!occupied&&!saved.enabled&&!saved.epoch.empty());
     assert(!prompt->respond(true));assert(!saved.enabled);}
    assert(!consentPrompt());saved={};
    {auto prompt=consentPrompt();assert(prompt&&occupied);}assert(!occupied&&saved.epoch.empty());
    {auto prompt=consentPrompt();assert(prompt->respond(std::nullopt));}assert(saved.epoch.empty());
    {auto prompt=consentPrompt();saved={true,std::string(32,'b')};assert(prompt->respond(false));}
    assert(saved.enabled&&saved.epoch==std::string(32,'b'));saved={};writeOk=false;
    {auto prompt=consentPrompt();assert(!prompt->respond(true));}assert(!occupied&&saved.epoch.empty());
    writeOk=true;{auto prompt=consentPrompt();assert(prompt->respond(true));}assert(saved.enabled);
    assert(releases==7);
}
'''
        self.run_cpp(source)

    def test_actual_deferred_shortcut_lease(self):
        runtime = (VIEWER / "fsinstallwindows.h").read_text()
        source = r'''
#include "fsinstallpolicy.h"
#include <cassert>
#include <memory>
#include <filesystem>
#include <vector>
using HANDLE=int;using Path=std::filesystem::path;
constexpr int FALSE=0,WAIT_OBJECT_0=0,WAIT_ABANDONED=128;
bool occupied=false,installed=true,writeOk=true;int releases=0,writes=0,applies=0,migrations=0;
HANDLE CreateMutexW(void*,int,const wchar_t*){return 1;}
int WaitForSingleObject(HANDLE,int){if(occupied)return 258;occupied=true;return WAIT_OBJECT_0;}
void ReleaseMutex(HANDLE){assert(occupied);occupied=false;++releases;}
struct Handle {HANDLE value=0;explicit operator bool()const{return value!=0;}};
struct Saved {std::optional<std::uint32_t> choices;std::wstring commit;};Saved stored;
struct Entry{};const std::string AppId="test",SourceCommit="abc";
std::wstring widen(const std::string& s){return {s.begin(),s.end()};}
Path binaryPath(){return "root/current/viewer.exe";}Path installation(const Path&){return installed?Path("root"):Path{};}
Saved load(){return stored;}bool save(std::uint32_t choices){++writes;stored={choices,widen(SourceCommit)};return true;}
void removeLegacyRegistration(const Path&){++migrations;}
bool entries(const Path&,std::vector<Entry>&){return true;}
bool apply(const std::vector<Entry>&,std::uint32_t,std::string& error){++applies;if(!writeOk)error="write failed";return writeOk;}
''' + block(runtime, "struct Setup\n{") + ";\n" + block(runtime, "inline std::shared_ptr<Setup> prepare(") + r'''
int main(){std::string error;
    installed=false;assert(!prepare(false,error)&&error.empty());installed=true;
    {auto setup=prepare(false,error);assert(setup&&setup->choose&&setup->choices==15&&occupied);
     assert(!prepare(false,error));assert(setup->finish(std::nullopt,error));assert(!occupied&&stored.choices==0);}
    assert(writes==1&&applies==0);assert(!prepare(false,error)&&!occupied);
    {auto setup=prepare(true,error);assert(setup&&setup->choose);assert(setup->finish(std::nullopt,error));}
    assert(writes==1&&stored.choices==0); // Manual cancel preserves original.
    {auto setup=prepare(true,error);assert(setup->finish(9,error));assert(!setup->finish(1,error));}
    assert(stored.choices==9&&writes==2&&applies==1);error.clear();
    stored.commit=L"old";
    {auto setup=prepare(false,error);assert(setup&&!setup->choose&&setup->choices==9);
     assert(setup->finish(setup->choices,error));}assert(writes==3&&applies==2);
    {auto setup=prepare(true,error);}assert(!occupied&&writes==3); // Teardown saves nothing.
    {auto setup=prepare(true,error);assert(!setup->finish(16,error));}assert(!occupied&&writes==3);error.clear();
    writeOk=false;{auto setup=prepare(true,error);assert(!setup->finish(1,error));}
    assert(!occupied&&writes==3&&stored.choices==9);assert(releases==8&&migrations==7);
}
'''
        self.run_cpp(source)

    def test_viewer_wiring_and_controls(self):
        app = (VIEWER / "llappviewer.cpp").read_text()
        init = block(app, "bool LLAppViewer::init()")
        self.assertNotIn("offerShortcuts", init)
        self.assertNotIn("offerConsent", init)
        self.assertIn("FSInstallViewer::poll();", block(app, "void LLAppViewer::idle()"))
        source = (VIEWER / "fsinstallviewer.cpp").read_text()
        for modal in ("DialogBox", "MessageBox", "TaskDialog", "Sleep(", "future.get("):
            self.assertNotIn(modal, source)
        controls = ET.parse(VIEWER / "skins/default/xui/en/floater_folderstorm_setup.xml").getroot()
        names = {control.get("name") for control in controls}
        self.assertTrue({"explanation", "details", "desktop_viewer", "desktop_host", "start_viewer", "start_host", "accept", "later"} <= names)
        registry = (VIEWER / "llviewerfloaterreg.cpp").read_text()
        self.assertIn('"floater_folderstorm_setup.xml", &LLFloaterReg::build<FSFloaterSetup>', registry)
        self.assertIn("fsinstallviewer.cpp", (VIEWER / "CMakeLists.txt").read_text())


if __name__ == "__main__":
    unittest.main()
