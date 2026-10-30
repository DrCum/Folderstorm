"""Portable production-model/adapter regressions, with typed LLSD and UI boundaries.

The fixtures do not replace native viewer, GPU, file picker or XML persistence
acceptance. Production headers are also checked separately against real LLSD/UI.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

from test_fsgestureboardcustomization import LLSD, UUID
from test_fsgestureboardsounds import VIEWER, function


def fixtures():
    uuid = UUID.replace('#include <string>', '#include <string>\n#include <cctype>\n#include <algorithm>')
    uuid = uuid.replace('bool isNull()const{return value.empty();}',
                        'bool isNull()const{return value.empty() || value=="00000000-0000-0000-0000-000000000000";}')
    uuid = uuid.replace('bool notNull()', r'''bool set(const std::string& text,bool) {
        if(text.size()!=36)return false;
        for(std::size_t i=0;i<text.size();++i) {
            if(i==8||i==13||i==18||i==23){if(text[i]!='-')return false;}
            else if(!std::isxdigit(static_cast<unsigned char>(text[i])))return false;
        }
        value=text;std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});return true;
    }
    bool notNull()''')
    sd = LLSD.replace('std::size_t size()const{return isMap()?map.size():isArray()?array.size():0;}',
                      'int size()const{return int(isMap()?map.size():isArray()?array.size():0);}')
    sd = sd.replace('bool isMap()const', 'bool isUndefined()const{return type==Undefined;}bool isMap()const')
    sd = sd.replace('bool has(const char* key)', 'bool has(const std::string& key)')
    sd = sd.replace('void erase(const char* key)', 'void erase(const std::string& key)')
    sd = sd.replace('operator[](const char* key)', 'operator[](const std::string& key)')
    sd = sd.replace('auto beginMap()const', 'auto beginArray()const{return array.begin();}auto endArray()const{return array.end();}\n    auto beginMap()const')
    return uuid, sd


class WorkspaceBackgrounds(unittest.TestCase):
    def cpp(self, source, *files, flags=(), expected=0):
        with tempfile.TemporaryDirectory(prefix='workspace-background-') as tmp:
            folder = Path(tmp)
            uuid, sd = fixtures()
            (folder/'lluuid.h').write_text(uuid)
            (folder/'llsd.h').write_text(sd)
            (folder/'llsdutil.h').write_text('#include "llsd.h"\n')
            (folder/'test.cpp').write_text(source)
            output = folder/'test'
            subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
                '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                '-I'+str(folder), '-I'+str(VIEWER), str(folder/'test.cpp'),
                *[str(VIEWER/file) for file in files], *flags, '-o', str(output)], check=True)
            result=subprocess.run([str(output)], timeout=20)
            self.assertEqual(result.returncode,expected)

    def test_background_schema_and_migration(self):
        self.cpp(r'''
#include "fsworkspacelayout.h"
#include "llsd.h"
#include <cassert>
#include <limits>
namespace bg=FSWorkspaceBackground;
int main() {
    bg::Config config;bg::Choice choice;choice.custom=true;
    choice.config.common.source=bg::Source::Local;
    choice.config.common.asset="aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa.png";
    choice.config.common.width=1920;choice.config.common.height=1080;
    choice.config.common.mode=bg::Mode::Tile;
    choice.config.arrangement="2:0:0:500:1000:500:0:500:1000";
    choice.config.displays[1].source=bg::Source::Texture;
    choice.config.displays[1].asset="bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb";
    choice.config.displays[1].grid="secondlife";
    auto data=bg::toLLSD(choice);bg::Choice parsed;
    assert(bg::fromLLSD(data,parsed)&&bg::toLLSD(parsed)==data);
    for(const auto& bad:{"../outside.png","C:\\image.png","https://example.com/a.png","aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa.jpg"}) {
        auto corrupt=data;corrupt["config"]["common"]["asset"]=bad;
        assert(!bg::fromLLSD(corrupt,parsed)&&bg::toLLSD(parsed)==data);
    }
    auto corrupt=data;corrupt["config"]["common"]["brightness"]=std::numeric_limits<double>::quiet_NaN();assert(!bg::fromLLSD(corrupt,parsed));
    corrupt=data;corrupt["config"]["common"]["width"]="1920";assert(!bg::fromLLSD(corrupt,parsed));
    corrupt=data;corrupt["config"]["displays"].append(corrupt["config"]["displays"][0]);assert(!bg::fromLLSD(corrupt,parsed));
    corrupt=data;corrupt["config"]["displays"][0]["index"]=bg::MaxDisplays;assert(!bg::fromLLSD(corrupt,parsed));
    corrupt=data;corrupt["config"]["displays"][0]["image"]["mode"]=5;assert(!bg::fromLLSD(corrupt,parsed));
    corrupt=data;corrupt["config"]["arrangement"]="native-device-id";assert(!bg::fromLLSD(corrupt,parsed));
    corrupt=data;corrupt["custom"]=false;assert(!bg::fromLLSD(corrupt,parsed));
    bg::Choice inherit;assert(bg::fromLLSD(bg::toLLSD(inherit),parsed)&&!parsed.custom);
    FSWorkspaceLayout::Workspace workspace,roundtrip,subset;
    workspace.components=FSWorkspaceLayout::Arrangement;workspace.background=choice;
    std::string error;auto saved=FSWorkspaceLayout::toLLSD(workspace);
    assert(FSWorkspaceLayout::fromLLSD(saved,roundtrip,error));
    assert(bg::toLLSD(roundtrip.background)==data);
    assert(FSWorkspaceLayout::selectGroups(roundtrip,FSWorkspaceLayout::Background,false,subset));
    assert(subset.components==FSWorkspaceLayout::Background&&subset.windows.empty());
    assert(FSWorkspaceLayout::fromLLSD(FSWorkspaceLayout::toLLSD(subset),roundtrip,error));
    assert(FSWorkspaceLayout::selectGroups(workspace,FSWorkspaceLayout::Chrome,false,subset)&&!subset.background.custom);
    for(int schema:{1,2}) {
        auto old=saved;old["schema"]=schema;old["components"]=FSWorkspaceLayout::All;old.erase("background");
        assert(FSWorkspaceLayout::fromLLSD(old,roundtrip,error));
        assert((roundtrip.components&FSWorkspaceLayout::Background)&&!roundtrip.background.custom);
        assert(FSWorkspaceLayout::selectGroups(roundtrip,FSWorkspaceLayout::Chrome,false,subset)&&!(subset.components&FSWorkspaceLayout::Background));
    }
    auto old=saved;old["schema"]=2;assert(!FSWorkspaceLayout::fromLLSD(old,roundtrip,error));
}
''', 'fsworkspacebackgroundmodel.cpp', 'fsworkspacelayout.cpp', 'fsworkspaceserialization.cpp')

    def test_existing_workspace_serialization_suite(self):
        # Exercise existing migration/partial-group assertions as well as the new cases.
        source=(VIEWER/'tests/test_fsworkspaceserialization.cpp').read_text().replace('"../fsworkspacelayout.h"','"fsworkspacelayout.h"')
        # Translate this fixture's byte-level UUID construction to equivalent text;
        # the portable LLUUID boundary intentionally has no native byte storage.
        source=source.replace('LLUUID id(folder_id); id.mData[14] = static_cast<U8>(i / 256); id.mData[15] = static_cast<U8>(i % 256);',
            "std::string idText=folder_id; const char* hex=\"0123456789abcdef\"; idText[32]=idText[33]='0'; idText[34]=hex[(i/16)%16]; idText[35]=hex[i%16]; LLUUID id(idText);")
        self.cpp(source,'fsworkspacebackgroundmodel.cpp','fsworkspacelayout.cpp','fsworkspaceserialization.cpp')

    def test_current_workspace_edit_keeps_other_groups(self):
        method=function((VIEWER/'fsworkspacecontroller.cpp').read_text(),'bool FSWorkspaceController::editBackground(')
        self.cpp(r'''
#include "fsworkspacelayout.h"
#include "llsd.h"
#include <cassert>
struct Settings {LLSD data;void setLLSD(const std::string& key,const LLSD& v){data[key]=v;}} gSavedPerAccountSettings;
namespace FSWorkspaceBackground {int refreshes=0;void refresh(){++refreshes;}}
struct FSWorkspaceController {
    bool mTransaction=true,online=true,owned=true,mBackgroundTouched=false,mComparisonDirty=false;
    unsigned long mRevision=0;LLSD mProfiles;std::string mActive="Current";
    void beginPreferencesSession(){}bool available(){return online;}bool sameSession(){return owned;}
    bool editBackground(const FSWorkspaceBackground::Choice&,const FSWorkspaceBackground::Config* = nullptr);
};
''' + method + r'''
int main(){
    FSWorkspaceController c;FSWorkspaceLayout::Workspace workspace;workspace.components=FSWorkspaceLayout::All;
    workspace.chrome.inset_left=25.f;c.mProfiles["Current"]=FSWorkspaceLayout::toLLSD(workspace);c.mProfiles["Other"]=c.mProfiles["Current"];
    const auto other=c.mProfiles["Other"];const auto chrome=other["chrome"];
    FSWorkspaceBackground::Choice choice;choice.custom=true;choice.config.common.color={1,0,0};
    assert(c.editBackground(choice));assert(c.mProfiles["Other"]==other&&c.mProfiles["Current"]["chrome"]==chrome);
    assert(c.mProfiles["Current"]["background"]==FSWorkspaceBackground::toLLSD(choice));
    FSWorkspaceBackground::Config defaults;defaults.common.color={0,1,0};
    assert(c.editBackground(choice,&defaults));
    assert(gSavedPerAccountSettings.data["FSWorkspaceBackgroundDefault"]==FSWorkspaceBackground::toLLSD(defaults));
    assert(c.mProfiles["Other"]==other);
    const auto before=gSavedPerAccountSettings.data;
    choice.config.common.brightness=2;assert(!c.editBackground(choice)&&gSavedPerAccountSettings.data==before);
    choice.config.common.brightness=1;c.online=false;assert(!c.editBackground(choice)&&gSavedPerAccountSettings.data==before);
    c.online=true;c.owned=false;assert(!c.editBackground(choice)&&gSavedPerAccountSettings.data==before);
}
''','fsworkspacebackgroundmodel.cpp','fsworkspacelayout.cpp','fsworkspaceserialization.cpp')

    def test_paint_demands_before_loading_and_reuses_textures(self):
        source=(VIEWER/'fsworkspacebackground.cpp').read_text()
        paint=function(source,'void paint(')
        self.cpp(r'''
#include "fsworkspacebackgroundmodel.h"
#include <cassert>
#include <memory>
using namespace FSWorkspaceBackground;
using F32=float;using S32=int;
struct LLColor4 { LLColor4(float,float,float,float){} };
struct LLRectf {float l,t,r,b;LLRectf(float a,float c,float d,float e):l(a),t(c),r(d),b(e){} };
namespace LLTexUnit {enum Mode{TAM_WRAP,TAM_CLAMP};}
struct Texture {
    bool loaded=false,missing=false;int stats=0;LLTexUnit::Mode address=LLTexUnit::TAM_CLAMP;
    bool isMissingAsset(){return missing;}bool hasGLTexture(){return loaded;}void addTextureStats(float){++stats;}
    int getFullWidth(){return 512;}int getFullHeight(){return 512;}auto getAddressMode(){return address;}
    void setAddressMode(LLTexUnit::Mode v){address=v;}
};
struct Service {std::map<std::string,std::shared_ptr<Texture>> textures;};
Service& service(){static Service s;return s;}
std::string key(const Image& image){return image.asset;}
S32 pixel(double v){return static_cast<S32>(std::lround(v));}
struct GL{void flush(){}}gGL;
int paints=0,colors=0;Rect last;double lastUV=0;
void gl_rect_2d(int,int,int,int,LLColor4){++colors;}
void gl_draw_scaled_image(int x,int y,int w,int h,std::shared_ptr<Texture>,LLColor4,LLRectf uv){++paints;last={double(x),double(y),double(x+w),double(y+h)};lastUV=uv.r-uv.l;}
''' + paint + r'''
int main(){
    Image image;image.source=Source::Local;image.asset="same";image.width=100;image.height=100;
    auto tex=std::make_shared<Texture>();service().textures["same"]=tex;
    const Rect target{0,0,1000,1000},clip{0,0,500,1000};
    paint(image,target,clip);assert(tex->stats==1&&paints==0&&colors==1);
    tex->loaded=true;
    for(int i=0;i<100;++i)paint(image,target,clip);
    assert(service().textures.size()==1&&paints==100&&last.right==500&&last.left==0);
    assert(lastUV==.5&&tex->address==LLTexUnit::TAM_CLAMP);
    image.mode=Mode::Tile;paint(image,target,clip);assert(lastUV==5&&tex->address==LLTexUnit::TAM_CLAMP);
    image.mode=Mode::Fit;image.width=200;image.height=100;paint(image,target,target);
    assert(last.width()==1000&&last.height()==500);
    tex->missing=true;const int old=paints;paint(image,target,target);assert(paints==old);
}
''')

    def test_render_import_and_transaction_boundaries(self):
        display=(VIEWER/'llviewerdisplay.cpp').read_text()
        start=display.index('void render_ui(F32 zoom_factor, int subfield)\n{')
        body=display[start:]
        self.assertLess(body.index('gPipeline.renderFinalize();'),body.index('FSWorkspaceBackground::draw();'))
        self.assertLess(body.index('FSWorkspaceBackground::draw();'),body.index('render_hud_elements();'))
        runtime=(VIEWER/'fsworkspacebackground.cpp').read_text()
        draw=function(runtime,'void draw()')
        self.assertIn('gSnapshot',draw)
        self.assertIn('FSSessionWorker::monitorRendering()',draw)
        self.assertIn('workspaceVisible()',draw)
        self.assertIn('outside(client(),viewport())',draw)
        panel=(VIEWER/'fspanelpreferencebackgrounds.cpp').read_text()
        choose=function(panel,'void FSPanelPreferenceBackgrounds::chooseFile(')
        for guard in ['sameSource()','mEdit!=edit','revision()!=revision','instanceVisible("preferences")']:
            self.assertGreaterEqual(choose.count(guard),2)
        self.assertIn('LLFile::remove(importDirectory',runtime)
        controller=(VIEWER/'fsworkspacecontroller.cpp').read_text()
        begin=function(controller,'void FSWorkspaceController::beginPreferencesSession(')
        cancel=function(controller,'void FSWorkspaceController::cancelPreferencesSession(')
        self.assertIn('mBackgroundDefault = gSavedPerAccountSettings',begin)
        self.assertIn('toLLSD(mBaseline.background)',cancel)
        self.assertIn('"FSWorkspaceBackgroundDefault",mBackgroundDefault',cancel)

    def test_late_import_delivery_and_cleanup(self):
        source=(VIEWER/'fsworkspacebackground.cpp').read_text()
        finish=function(source,'    void finishImport()').replace('    void finishImport()', 'void Service::finishImport()')
        self.cpp(r'''
#include "fsworkspacebackgroundmodel.h"
#include "lluuid.h"
#include <cassert>
#include <future>
#include <functional>
using namespace FSWorkspaceBackground;
bool online=true;bool ready(){return online;}
struct Agent {LLUUID account{"account"},login{"login"};LLUUID getID(){return account;}LLUUID getSessionID(){return login;}}gAgent;
struct LLGridManager {std::string grid="grid";static auto& instance(){static LLGridManager g;return g;}std::string getGrid(){return grid;}};
struct Directory {std::string getDirDelimiter(){return "/";}} dir,*gDirUtilp=&dir;
struct LLFile {static inline std::vector<std::string> removed;static void remove(std::string path){removed.push_back(path);}};
struct ImportResult {std::string asset,error;int width=0,height=0;};
struct Service {
    std::future<ImportResult> importJob;std::function<bool(Image,std::string)> done;
    LLUUID importAccount{"account"},importLogin{"login"};std::string importGrid="grid",importDirectory="/profile/backgrounds";
    void finishImport();
};
''' + finish + r'''
int main(){
    Service service;int calls=0;
    auto queued=[&](bool accept=true){
        std::promise<ImportResult> p;service.importJob=p.get_future();
        service.done=[&,accept](Image image,std::string error){++calls;assert(image.width==1920&&image.height==1080&&error.empty());return accept;};
        return p;
    };
    auto p=queued();service.finishImport();assert(service.importJob.valid()&&calls==0);
    p.set_value({"image.png","",1920,1080});service.finishImport();assert(calls==1&&LLFile::removed.empty()&&!service.importJob.valid());
    for(int source=0;source<4;++source){
        auto late=queued();
        if(source==0)gAgent.account=LLUUID("other");
        if(source==1)gAgent.login=LLUUID("new-login");
        if(source==2)LLGridManager::instance().grid="other-grid";
        if(source==3)online=false;
        late.set_value({"stale.png","",1920,1080});service.finishImport();assert(calls==1);
        assert(LLFile::removed.back()=="/profile/backgrounds/stale.png");
        gAgent.account=LLUUID("account");gAgent.login=LLUUID("login");LLGridManager::instance().grid="grid";online=true;
    }
    auto cancelled=queued(false);cancelled.set_value({"cancelled.png","",1920,1080});service.finishImport();assert(calls==2&&LLFile::removed.back()=="/profile/backgrounds/cancelled.png");
    auto closed=queued();service.done={};closed.set_value({"closed.png","",1920,1080});service.finishImport();assert(calls==2&&LLFile::removed.back()=="/profile/backgrounds/closed.png");
}
''')

    def test_xui_control_names_and_bounds(self):
        root=ET.parse(VIEWER/'skins/default/xui/en/panel_preferences_backgrounds.xml').getroot()
        names=[item.attrib['name'] for item in root.iter() if 'name' in item.attrib]
        self.assertEqual(len(names),len(set(names)))
        panel=(VIEWER/'fspanelpreferencebackgrounds.cpp').read_text()
        for name in re.findall(r'getChild<[^>]+>\("([^"]+)"\)',panel):
            self.assertIn(name,names)
        for item in root.iter():
            if 'top' in item.attrib and 'height' in item.attrib:
                self.assertLessEqual(int(item.attrib['top'])+int(item.attrib['height']),652)
        settings=ET.parse(VIEWER/'app_settings/settings_per_account.xml').getroot()
        keys=[x.text for x in settings.find('map') if x.tag=='key']
        for name in ['FSWorkspaceBackground','FSWorkspaceBackgroundDefault']:
            self.assertEqual(keys.count(name),1)


if __name__=='__main__':
    unittest.main()
