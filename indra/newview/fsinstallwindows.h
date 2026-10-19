/** SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#if defined(_WIN32)
#include "fsinstallpolicy.h"
#include "fsupdatewindows.h"
#include "fsupdateconfig.h"
#include "fsreleasebuild.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <vector>
#include <memory>
namespace fs_update::shortcuts
{
using Path=std::filesystem::path;
struct Com
{
    HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ~Com(){if(SUCCEEDED(result))CoUninitialize();}
    explicit operator bool()const{return SUCCEEDED(result)||result==RPC_E_CHANGED_MODE;}
};
inline std::wstring widen(const std::string& value){return {value.begin(),value.end()};}
inline std::wstring key(){return L"Software\\Folderstorm\\Install\\"+widen(AppId);}
inline Path knownFolder(REFKNOWNFOLDERID id)
{
    PWSTR value=nullptr;
    if(FAILED(SHGetKnownFolderPath(id,0,nullptr,&value))||!value)return {};
    const Path result(value);CoTaskMemFree(value);return result;
}
struct Saved {std::optional<std::uint32_t> choices;std::wstring commit;};
inline Saved load()
{
    Saved saved;DWORD value=0,size=sizeof(value);
    if(RegGetValueW(HKEY_CURRENT_USER,key().c_str(),L"ShortcutsV1",RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS && size==sizeof(value))saved.choices=fs_install::decode(value);
    wchar_t commit[64]{};size=sizeof(commit);
    if(RegGetValueW(HKEY_CURRENT_USER,key().c_str(),L"ShortcutsCommit",RRF_RT_REG_SZ,nullptr,commit,&size)==ERROR_SUCCESS)saved.commit=commit;
    return saved;
}
inline bool save(std::uint32_t choices)
{
    HKEY registry=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,key().c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&registry,nullptr)!=ERROR_SUCCESS)return false;
    const DWORD value=fs_install::encode(choices);const auto commit=widen(SourceCommit);
    const auto first=RegSetValueExW(registry,L"ShortcutsV1",0,REG_DWORD,reinterpret_cast<const BYTE*>(&value),sizeof(value));
    const auto second=RegSetValueExW(registry,L"ShortcutsCommit",0,REG_SZ,reinterpret_cast<const BYTE*>(commit.c_str()),static_cast<DWORD>((commit.size()+1)*sizeof(wchar_t)));
    RegCloseKey(registry);return first==ERROR_SUCCESS&&second==ERROR_SUCCESS;
}
inline void removeLegacyRegistration(const Path& root)
{
    HKEY registry=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",0,
        KEY_READ|KEY_WRITE|DELETE,&registry)!=ERROR_SUCCESS)return;
    const auto expected=L"\""+(root/L"Update.exe").wstring()+L"\" --uninstall";
    std::vector<std::wstring> owned;
    // Collect before deletion, so enumeration indices cannot skip an entry.
    for(DWORD index=0;index<4096;++index)
    {
        wchar_t name[256]{};DWORD length=static_cast<DWORD>(sizeof(name)/sizeof(name[0]));
        const auto result=RegEnumKeyExW(registry,index,name,&length,nullptr,nullptr,nullptr,nullptr);
        if(result==ERROR_NO_MORE_ITEMS)break;
        if(result!=ERROR_SUCCESS)continue;
        if(!fs_install::legacyRegistrationOwned(name,true))continue;
        wchar_t command[32768]{};DWORD bytes=sizeof(command);
        if(RegGetValueW(registry,name,L"UninstallString",RRF_RT_REG_SZ,nullptr,command,&bytes)!=ERROR_SUCCESS)continue;
        const bool matches=CompareStringOrdinal(command,-1,expected.c_str(),-1,TRUE)==CSTR_EQUAL;
        if(fs_install::legacyRegistrationOwned(name,matches))owned.emplace_back(name);
    }
    for(const auto& name:owned)RegDeleteTreeW(registry,name.c_str());
    RegCloseKey(registry);
}
struct Entry {Path link,target,icon;};
inline bool entries(const Path& root,std::vector<Entry>& result)
{
    const auto current=root/L"current";
    if(!regular(current/L"folderstorm-session-viewer.txt"))return false;
    std::ifstream file(current/L"folderstorm-session-viewer.txt",std::ios::binary);
    std::string name(257,'\0');file.read(name.data(),static_cast<std::streamsize>(name.size()));name.resize(static_cast<std::size_t>(file.gcount()));
    while(!name.empty()&&(name.back()=='\r'||name.back()=='\n'))name.pop_back();
    if(!fs_install::simpleExeName(name))return false;
    const auto viewer=current/widen(name),host=current/L"folderstorm-session-host.exe";
    if(!regular(viewer)||!regular(host))return false;
    const auto desktop=knownFolder(FOLDERID_Desktop),programs=knownFolder(FOLDERID_Programs);
    if(desktop.empty()||programs.empty())return false;
    const auto build=fs_release::runningBuild();
    const auto suffix=L" "+widen(std::string(fs_release::DefaultChannel)=="stable"?"Stable":"Nightly")+L" ("+
        widen(build.flavor=="opensim"?"OpenSim":"Second Life")+L", "+widen(build.cpu)+L")";
    const auto viewerName=L"Folderstorm"+suffix,hostName=L"Folderstorm Sessions"+suffix;
    const auto folder=programs/(L"Folderstorm"+suffix);
    const auto launcher=root/(widen(AppId)+L".exe");
    const auto target=regular(launcher)?launcher:viewer;
    // Stable launcher/current paths remain valid after a Velopack replacement.
    result={{desktop/(viewerName+L".lnk"),target,viewer},{desktop/(hostName+L".lnk"),host,host},
        {folder/(viewerName+L".lnk"),target,viewer},{folder/(hostName+L".lnk"),host,host}};
    return true;
}
inline bool samePath(const Path& a,const Path& b)
{
    const auto left=a.lexically_normal().wstring(),right=b.lexically_normal().wstring();
    return CompareStringOrdinal(left.c_str(),-1,right.c_str(),-1,TRUE)==CSTR_EQUAL;
}
inline bool owns(IShellLinkW* link,const Entry& entry)
{
    std::vector<wchar_t> target(32768);
    if(FAILED(link->GetPath(target.data(),static_cast<int>(target.size()),nullptr,SLGP_RAWPATH)))return false;
    std::vector<wchar_t> arguments(32768);
    if(FAILED(link->GetArguments(arguments.data(),static_cast<int>(arguments.size())))||arguments[0])return false;
    // Accept either of this install's stable viewer paths, never another root
    // or a user-customized shortcut with account/profile arguments.
    return samePath(Path(target.data()),entry.target)||samePath(Path(target.data()),entry.icon);
}
inline bool apply(const std::vector<Entry>& links,std::uint32_t choices,std::string& error)
{
    Com com;if(!com){error="Windows shortcut services could not start.";return false;}
    for(std::size_t i=0;i<links.size();++i)
    {
        const auto& entry=links[i];IShellLinkW* link=nullptr;
        if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,reinterpret_cast<void**>(&link)))){error="A shortcut could not be opened.";return false;}
        IPersistFile* persist=nullptr;
        const auto queried=link->QueryInterface(IID_IPersistFile,reinterpret_cast<void**>(&persist));
        if(FAILED(queried)){link->Release();error="A shortcut could not be saved.";return false;}
        const DWORD attributes=GetFileAttributesW(entry.link.c_str());
        const bool exists=attributes!=INVALID_FILE_ATTRIBUTES;
        const bool owned=exists && !(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) &&
            SUCCEEDED(persist->Load(entry.link.c_str(),STGM_READ)) && owns(link,entry);
        const auto action=fs_install::action((choices&(1u<<i))!=0,exists,owned);
        bool ok=true;
        if(action==fs_install::LinkAction::Conflict){error="A shortcut with this name belongs to another installation. It was left unchanged.";ok=false;}
        else if(action==fs_install::LinkAction::Remove)ok=DeleteFileW(entry.link.c_str())!=FALSE;
        else if(action==fs_install::LinkAction::Write)
        {
            std::error_code filesystemError;std::filesystem::create_directories(entry.link.parent_path(),filesystemError);
            ok=!filesystemError && SUCCEEDED(link->SetPath(entry.target.c_str())) && SUCCEEDED(link->SetArguments(L"")) &&
                SUCCEEDED(link->SetDescription(i%2?L"Folderstorm character sessions":L"Folderstorm viewer")) &&
                SUCCEEDED(link->SetWorkingDirectory(entry.icon.parent_path().c_str())) && SUCCEEDED(link->SetIconLocation(entry.icon.c_str(),0)) &&
                SUCCEEDED(persist->Save(entry.link.c_str(),TRUE));
        }
        persist->Release();link->Release();
        if(!ok){if(error.empty())error="A shortcut could not be changed. Your saved choices were kept.";return false;}
    }
    return true;
}
// Native dialog units and DS_SETFONT keep keyboard navigation and Windows DPI
// scaling. It runs only in an interactive app, never an install/update hook.
struct Dialog
{
    std::uint32_t choices=fs_install::AllShortcuts;
    static INT_PTR CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
    {
        auto* self=reinterpret_cast<Dialog*>(GetWindowLongPtrW(window,DWLP_USER));
        if(message==WM_INITDIALOG)
        {
            self=reinterpret_cast<Dialog*>(lparam);SetWindowLongPtrW(window,DWLP_USER,lparam);
            for(unsigned i=0;i<4;++i)CheckDlgButton(window,100+static_cast<int>(i),(self->choices&(1u<<i))?BST_CHECKED:BST_UNCHECKED);
            return TRUE;
        }
        if(message==WM_COMMAND && self)
        {
            if(LOWORD(wparam)==IDOK)
            {
                self->choices=0;for(unsigned i=0;i<4;++i)if(IsDlgButtonChecked(window,100+static_cast<int>(i))==BST_CHECKED)self->choices|=1u<<i;
                EndDialog(window,IDOK);return TRUE;
            }
            if(LOWORD(wparam)==IDCANCEL){EndDialog(window,IDCANCEL);return TRUE;}
        }
        return FALSE;
    }
    INT_PTR show()
    {
        std::vector<WORD> data;
        const auto dword=[&](DWORD value){data.push_back(static_cast<WORD>(value));data.push_back(static_cast<WORD>(value>>16));};
        const auto text=[&](const wchar_t* value){while(*value)data.push_back(static_cast<WORD>(*value++));data.push_back(0);};
        dword(WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT);dword(0);data.insert(data.end(),{8,0,0,290,191,0,0});text(L"Folderstorm shortcuts");data.push_back(9);text(L"Segoe UI");
        const auto control=[&](DWORD style,WORD x,WORD y,WORD width,WORD height,WORD id,WORD type,const wchar_t* label)
        {
            if(data.size()%2)data.push_back(0);
            dword(WS_CHILD|WS_VISIBLE|style);dword(0);data.insert(data.end(),{x,y,width,height,id,0xffff,type});text(label);data.push_back(0);
        };
        control(0,12,10,266,23,10,0x82,L"Choose shortcuts for this Windows user.");
        const wchar_t* labels[]={L"Desktop: viewer",L"Desktop: character sessions",L"Start menu: viewer",L"Start menu: character sessions"};
        for(WORD i=0;i<4;++i)control(WS_TABSTOP|BS_AUTOCHECKBOX,14,static_cast<WORD>(35+23*i),260,16,static_cast<WORD>(100+i),0x80,labels[i]);
        control(0,12,132,266,25,11,0x82,L"Choices are remembered. Shortcuts for other installs are kept. You can change these choices from Help or host Support.");
        control(WS_TABSTOP|BS_DEFPUSHBUTTON,132,164,68,18,IDOK,0x80,L"Apply");
        control(WS_TABSTOP|BS_PUSHBUTTON,210,164,68,18,IDCANCEL,0x80,L"Not now");
        return DialogBoxIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(data.data()),GetActiveWindow(),procedure,reinterpret_cast<LPARAM>(this));
    }
};
// Hold the inter-process chooser lease across asynchronous viewer UI. The host
// uses the same preparation/commit path with its synchronous native dialog.
struct Setup
{
    Path root;
    Handle mutex;
    Saved saved;
    std::uint32_t choices=0;
    bool choose=false,locked=false;
    void unlock(){if(locked){ReleaseMutex(mutex.value);locked=false;}}
    ~Setup(){unlock();}
    bool finish(std::optional<std::uint32_t> selected,std::string& error)
    {
        struct Release {Setup& owner;~Release(){owner.unlock();}} release{*this};
        if(!locked){error="This shortcut chooser is no longer active.";return false;}
        if(!selected)
        {
            if(saved.choices || save(0))return true;
            error="Shortcut choices could not be saved.";return false;
        }
        if(*selected&~fs_install::AllShortcuts){error="Invalid shortcut choices.";return false;}
        std::vector<Entry> links;
        if(!entries(root,links)){error="The matching viewer/session-host files or Windows shortcut folders are unavailable.";return false;}
        if(!apply(links,*selected,error))return false;
        if(!save(*selected)){error="Shortcuts changed, but their choices could not be saved.";return false;}
        return true;
    }
};
inline std::shared_ptr<Setup> prepare(bool manual,std::string& error)
{
    auto setup=std::make_shared<Setup>();setup->root=installation(binaryPath());
    if(setup->root.empty()){if(manual)error="Shortcut setup is available in a Velopack installation.";return {};}
    const auto mutexName=L"Local\\Folderstorm.Shortcuts."+widen(AppId);
    setup->mutex.value=CreateMutexW(nullptr,FALSE,mutexName.c_str());
    if(!setup->mutex){error="Shortcut choices could not be opened.";return {};}
    const auto wait=WaitForSingleObject(setup->mutex.value,0);
    if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED){if(manual)error="Shortcut setup is already open in another Folderstorm window.";return {};}
    setup->locked=true;setup->saved=load();
    setup->choices=setup->saved.choices.value_or(fs_install::AllShortcuts);
    if(!manual&&setup->saved.choices&&setup->saved.commit==widen(SourceCommit))return {};
    removeLegacyRegistration(setup->root);
    setup->choose=manual||!setup->saved.choices;
    return setup;
}
inline bool run(bool manual,std::string& error)
{
    auto setup=prepare(manual,error);if(!setup)return error.empty();
    if(!setup->choose)return setup->finish(setup->choices,error);
    Dialog dialog;dialog.choices=setup->choices;const auto result=dialog.show();
    if(result==-1){error="The shortcut chooser could not open.";return false;}
    return setup->finish(result==IDOK?std::optional<std::uint32_t>(dialog.choices):std::nullopt,error);
}
inline void remove()
{
    const auto root=installation(binaryPath());if(root.empty())return;
    removeLegacyRegistration(root);
    std::vector<Entry> links;std::string error;
    if(entries(root,links)&&apply(links,0,error))RemoveDirectoryW(links[2].link.parent_path().c_str());
    // Keep the four choices, but force a same-build reinstall to apply them.
    HKEY registry=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,key().c_str(),0,KEY_SET_VALUE,&registry)==ERROR_SUCCESS)
    {RegDeleteValueW(registry,L"ShortcutsCommit");RegCloseKey(registry);}
}
}
#endif
