/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_DIALOG_H
#define FS_HOST_DIALOG_H
#include <windows.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <cwchar>
namespace fs_host_ui
{
enum class FieldType { Text, Number, Choice, Check, Color };
struct Field
{
    std::wstring label, value;
    FieldType type = FieldType::Text;
    std::vector<std::wstring> choices;
    unsigned int minimum = 0, maximum = 64;
    HWND control = nullptr;
};
inline unsigned int number(const Field& field) { return static_cast<unsigned int>(std::wcstoul(field.value.c_str(), nullptr, 10)); }
struct Editor { const wchar_t* title; std::vector<Field>* fields; };
inline INT_PTR CALLBACK editorProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* editor = reinterpret_cast<Editor*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        editor = reinterpret_cast<Editor*>(lparam); SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        SetWindowTextW(dialog, editor->title);
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        const auto font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const HDC dc = GetDC(dialog); const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96; if (dc) ReleaseDC(dialog, dc);
        const auto scale = [dpi](int n) { return MulDiv(n, dpi, 96); };
        int y = 14;
        for (std::size_t i = 0; i < editor->fields->size(); ++i)
        {
            auto& field = (*editor->fields)[i];
            HWND label = CreateWindowExW(0,L"STATIC",field.label.c_str(),WS_CHILD|WS_VISIBLE,scale(12),scale(y+4),scale(180),scale(25),dialog,nullptr,instance,nullptr);
            const bool check = field.type == FieldType::Check, choice = field.type == FieldType::Choice, color = field.type == FieldType::Color;
            const DWORD style = WS_CHILD|WS_VISIBLE|WS_TABSTOP | (check ? BS_AUTOCHECKBOX : choice ? CBS_DROPDOWNLIST|WS_VSCROLL : color ? BS_PUSHBUTTON : WS_BORDER|ES_AUTOHSCROLL);
            field.control = CreateWindowExW(0, check || color ? L"BUTTON" : choice ? L"COMBOBOX" : L"EDIT", color ? L"Choose color…" : field.value.c_str(),
                style,scale(196),scale(y),scale(310),scale(choice ? 220 : 25),dialog,reinterpret_cast<HMENU>(static_cast<INT_PTR>(100+i)),instance,nullptr);
            SendMessageW(label,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); SendMessageW(field.control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
            if (check) SendMessageW(field.control,BM_SETCHECK,number(field) ? BST_CHECKED : BST_UNCHECKED,0);
            if (choice)
            { for (const auto& text : field.choices) SendMessageW(field.control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str())); SendMessageW(field.control,CB_SETCURSEL,number(field),0); }
            if (!check && !choice && !color) SendMessageW(field.control,EM_SETLIMITTEXT,field.type == FieldType::Number ? 6 : field.maximum,0);
            y += 36;
        }
        const auto button = [&](const wchar_t* text,int x,int id)
        {
            HWND child = CreateWindowExW(0,L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|(id == IDOK ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
                scale(x),scale(y+8),scale(110),scale(28),dialog,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
            SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        };
        button(L"Apply",276,IDOK); button(L"Cancel",396,IDCANCEL);
        RECT size{0,0,scale(520),scale(y+50)}; AdjustWindowRectEx(&size,static_cast<DWORD>(GetWindowLongPtrW(dialog,GWL_STYLE)),FALSE,0);
        RECT owner{}; GetWindowRect(GetParent(dialog),&owner);
        SetWindowPos(dialog,nullptr,owner.left+30,owner.top+30,size.right-size.left,size.bottom-size.top,SWP_NOZORDER);
        return TRUE;
    }
    if (!editor) return FALSE;
    if (message == WM_CLOSE) { EndDialog(dialog,IDCANCEL); return TRUE; }
    if (message != WM_COMMAND) return FALSE;
    const unsigned int id = LOWORD(wparam);
    if (id >= 100 && id < 100 + editor->fields->size())
    {
        auto& field = (*editor->fields)[id-100];
        if (field.type == FieldType::Color && HIWORD(wparam) == BN_CLICKED)
        {
            static COLORREF custom[16]{}; const auto rgb = number(field);
            CHOOSECOLORW color{}; color.lStructSize = sizeof(color); color.hwndOwner = dialog; color.lpCustColors = custom;
            color.rgbResult = RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255); color.Flags = CC_FULLOPEN|CC_RGBINIT;
            if (ChooseColorW(&color))
            { field.value = std::to_wstring((static_cast<unsigned int>(GetRValue(color.rgbResult))<<16)|(static_cast<unsigned int>(GetGValue(color.rgbResult))<<8)|GetBValue(color.rgbResult)); }
        }
        return TRUE;
    }
    if (id == IDCANCEL) { EndDialog(dialog,IDCANCEL); return TRUE; }
    if (id != IDOK) return FALSE;
    for (auto& field : *editor->fields)
    {
        if (field.type == FieldType::Color) continue;
        if (field.type == FieldType::Check) field.value = SendMessageW(field.control,BM_GETCHECK,0,0) == BST_CHECKED ? L"1" : L"0";
        else if (field.type == FieldType::Choice)
        {
            const auto selected = SendMessageW(field.control,CB_GETCURSEL,0,0);
            if (selected < 0) { SetFocus(field.control); return TRUE; }
            field.value = std::to_wstring(selected);
        }
        else
        {
            std::vector<wchar_t> buffer(static_cast<std::size_t>(GetWindowTextLengthW(field.control))+1);
            GetWindowTextW(field.control,buffer.data(),static_cast<int>(buffer.size())); field.value = buffer.data();
            if (field.type == FieldType::Number)
            {
                wchar_t* end = nullptr; const auto n = std::wcstoul(field.value.c_str(),&end,10);
                if (field.value.empty() || field.value.find_first_not_of(L"0123456789") != std::wstring::npos || !end || *end || n < field.minimum || n > field.maximum)
                { MessageBoxW(dialog,L"Enter a value in the displayed range.",field.label.c_str(),MB_OK|MB_ICONINFORMATION); SetFocus(field.control); return TRUE; }
            }
        }
    }
    EndDialog(dialog,IDOK); return TRUE;
}
inline bool edit(HWND owner,const wchar_t* title,std::vector<Field>& fields)
{
    struct Template { DLGTEMPLATE dialog; WORD menu = 0, klass = 0, title = 0; } layout{};
    layout.dialog.style = WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;
    layout.dialog.cx = 300; layout.dialog.cy = 200;
    Editor editor{title,&fields};
    return DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&layout.dialog,owner,editorProc,reinterpret_cast<LPARAM>(&editor)) == IDOK;
}
}
#endif
