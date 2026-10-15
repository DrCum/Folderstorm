/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * $/LicenseInfo$
 */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../dialog.h"
#include <cassert>
int main()
{
    const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);assert(SUCCEEDED(initialized));
    INITCOMMONCONTROLSEX classes{sizeof(classes),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};assert(InitCommonControlsEx(&classes));
    {
        fs_host_ui::Theme theme;
        const auto parent=CreateWindowExW(0,L"STATIC",L"Host control check",WS_OVERLAPPEDWINDOW,0,0,500,400,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(parent);
        const auto button=CreateWindowExW(0,L"BUTTON",L"BG Audio",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,0,0,120,32,parent,nullptr,GetModuleHandleW(nullptr),nullptr);assert(button);
        theme.window(parent);theme.checkButton(button,true);
        assert(SendMessageW(button,BM_GETCHECK,0,0)==BST_CHECKED);
        theme.accessible.set(button,L"BG Audio",L"Inactive viewer audio only");SetWindowTextW(button,L"Muted");
        IAccessible* accessible=nullptr;
        assert(SUCCEEDED(AccessibleObjectFromWindow(button,OBJID_CLIENT,IID_IAccessible,reinterpret_cast<void**>(&accessible))) && accessible);
        VARIANT self;VariantInit(&self);self.vt=VT_I4;self.lVal=CHILDID_SELF;
        VARIANT role,state;VariantInit(&role);VariantInit(&state);
        assert(SUCCEEDED(accessible->get_accRole(self,&role)) && role.vt==VT_I4 && role.lVal==ROLE_SYSTEM_CHECKBUTTON);
        assert(SUCCEEDED(accessible->get_accState(self,&state)) && state.vt==VT_I4 && (state.lVal&STATE_SYSTEM_CHECKED));
        BSTR name=nullptr;assert(SUCCEEDED(accessible->get_accName(self,&name)) && name && std::wstring(name)==L"BG Audio");SysFreeString(name);
        theme.checkButton(button,false);VariantClear(&state);assert(SUCCEEDED(accessible->get_accState(self,&state)) && !(state.lVal&STATE_SYSTEM_CHECKED));
        // Manual checked buttons send a command without performing a second model inversion.
        SendMessageW(button,BM_CLICK,0,0);assert(SendMessageW(button,BM_GETCHECK,0,0)==BST_UNCHECKED);
        ValidateRect(button,nullptr);SendMessageW(button,WM_SIZE,SIZE_RESTORED,MAKELPARAM(120,32));
        RECT dirty{},client{};assert(GetUpdateRect(button,&dirty,FALSE));GetClientRect(button,&client);assert(EqualRect(&dirty,&client));
        theme.checkButton(button,std::nullopt);assert((GetWindowLongPtrW(button,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW);
        assert(!GetPropW(button,L"FolderstormToggleControl"));
        {fs_host_ui::PopupAnchor anchor(button,-1);RECT bounds{};assert(GetWindowRect(button,&bounds));const auto point=fs_host_ui::popupPoint();assert(point.x==bounds.left && point.y==bounds.bottom);}
        assert(!fs_host_ui::keyboardPopupAnchor);
        VariantClear(&role);VariantClear(&state);accessible->Release();
        std::vector<fs_host_ui::Field> fields(3);
        for(std::size_t i=0;i<fields.size();++i)
        {
            auto& field=fields[i];field.type=fs_host_ui::FieldType::Choice;field.value=L"0";if(i)field.mirror=0;
            field.control=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|CBS_DROPDOWNLIST,0,0,180,100,parent,nullptr,GetModuleHandleW(nullptr),nullptr);assert(field.control);
            for(const auto* label:{L"Icon and text",L"Text only",L"Icons only"})SendMessageW(field.control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
            SendMessageW(field.control,CB_SETCURSEL,0,0);
        }
        fs_host_ui::Editor editor(L"Preferences",fields,{});
        SendMessageW(fields[1].control,CB_SETCURSEL,2,0);assert(&fs_host_ui::synchronizeEditorField(editor,1)==&fields[0]);
        for(const auto& field:fields)assert(field.value==L"2" && SendMessageW(field.control,CB_GETCURSEL,0,0)==2);
        SendMessageW(fields[0].control,CB_SETCURSEL,1,0);fs_host_ui::synchronizeEditorField(editor,0);
        for(const auto& field:fields)assert(field.value==L"1" && SendMessageW(field.control,CB_GETCURSEL,0,0)==1);
        assert(!editor.updating);
        theme.set(1,true);assert(theme.highContrast);if(!theme.systemHighContrast)assert(theme.background==RGB(0,0,0) && theme.text==RGB(255,255,255));
        theme.set(1,false);if(!theme.systemHighContrast)assert(!theme.highContrast && !theme.dark);
        DestroyWindow(parent);
    }
    CoUninitialize();
}
