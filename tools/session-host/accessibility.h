/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_ACCESSIBILITY_H
#define FS_HOST_ACCESSIBILITY_H
#include <windows.h>
#include <commctrl.h>
#include <oleacc.h>
#include <map>
#include <string>
namespace fs_host_ui
{
// Native controls continue exposing their native role/check state. Dynamic
// annotation keeps the accessible name independent of short visual captions.
class AccessibleNames
{
    IAccPropServices* service=nullptr;
    bool attempted=false;
    struct Entry {std::wstring name,description;};
    std::map<HWND,Entry> entries;
    static LRESULT CALLBACK proc(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data)
    {
        auto& names=*reinterpret_cast<AccessibleNames*>(data);
        if(message==WM_NCDESTROY)
        {
            if(names.service){const MSAAPROPID properties[]={PROPID_ACC_NAME,PROPID_ACC_DESCRIPTION};names.service->ClearHwndProps(child,OBJID_CLIENT,CHILDID_SELF,properties,2);}
            names.entries.erase(child);RemoveWindowSubclass(child,proc,9);
        }
        return DefSubclassProc(child,message,wp,lp);
    }
public:
    AccessibleNames()=default;
    AccessibleNames(const AccessibleNames&)=delete;
    AccessibleNames& operator=(const AccessibleNames&)=delete;
    ~AccessibleNames()
    {
        for(const auto& entry:entries)if(IsWindow(entry.first))RemoveWindowSubclass(entry.first,proc,9);
        if(service)service->Release();
    }
    void set(HWND child,const std::wstring& name,const std::wstring& description={})
    {
        if(!child)return;
        if(!attempted){attempted=true;CoCreateInstance(CLSID_AccPropServices,nullptr,CLSCTX_INPROC_SERVER,IID_IAccPropServices,reinterpret_cast<void**>(&service));}
        auto found=entries.find(child);
        if(found==entries.end()){if(!SetWindowSubclass(child,proc,9,reinterpret_cast<DWORD_PTR>(this)))return;found=entries.emplace(child,Entry{}).first;}
        auto& entry=found->second;
        if(entry.name!=name)
        {
            if(service)service->SetHwndPropStr(child,OBJID_CLIENT,CHILDID_SELF,PROPID_ACC_NAME,name.c_str());
            entry.name=name;NotifyWinEvent(EVENT_OBJECT_NAMECHANGE,child,OBJID_CLIENT,CHILDID_SELF);
        }
        if(entry.description!=description)
        {
            if(service)service->SetHwndPropStr(child,OBJID_CLIENT,CHILDID_SELF,PROPID_ACC_DESCRIPTION,description.c_str());
            entry.description=description;
        }
    }
};
inline HWND keyboardPopupAnchor=nullptr;
struct PopupAnchor
{
    HWND previous=keyboardPopupAnchor;
    PopupAnchor(HWND child,LPARAM point){if(point==-1)keyboardPopupAnchor=child;}
    ~PopupAnchor(){keyboardPopupAnchor=previous;}
};
inline POINT popupPoint()
{
    RECT rect{};if(keyboardPopupAnchor && GetWindowRect(keyboardPopupAnchor,&rect))return {rect.left,rect.bottom};
    POINT point{};GetCursorPos(&point);return point;
}
}
#endif
