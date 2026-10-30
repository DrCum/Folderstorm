/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "host_preview.h"
#include "host_palette_ui.h"
#include <shlobj.h>
#include <shellapi.h>
#include <commctrl.h>
#include <fstream>
#include <array>
#include <algorithm>

namespace fs_host_theme
{
namespace
{
std::string text(const boost::json::object& object,const char* key)
{
    const auto* value=object.if_contains(key);fs_theme::require(value && value->is_string(),"host_preview",key);
    return std::string(value->as_string());
}
std::int64_t integer(const boost::json::object& object,const char* key)
{
    const auto* value=object.if_contains(key);
    fs_theme::require(value && (value->is_int64() || value->is_uint64() && value->as_uint64()<=INT64_MAX),"host_preview",key);
    return value->is_int64()?value->as_int64():static_cast<std::int64_t>(value->as_uint64());
}
std::filesystem::path known(REFKNOWNFOLDERID id)
{
    PWSTR value=nullptr;const auto result=SHGetKnownFolderPath(id,0,nullptr,&value);
    fs_theme::require(SUCCEEDED(result) && value,"host_preview","Windows user folder is unavailable");
    const std::filesystem::path path(value);CoTaskMemFree(value);return path;
}
std::filesystem::path executableDirectory()
{
    std::vector<wchar_t> value(32768);const auto size=GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));
    fs_theme::require(size && size<value.size(),"host_preview","Host path is unavailable");return std::filesystem::path(std::wstring(value.data(),size)).parent_path();
}
std::filesystem::path requestPath(const std::filesystem::path& input)
{
    const auto absolute=std::filesystem::absolute(input).lexically_normal();const auto leaf=fs_theme::toUtf8(absolute.parent_path().filename());
    fs_theme::require(leaf.size()==32 && std::all_of(leaf.begin(),leaf.end(),[](char ch){return ch>='0' && ch<='9' || ch>='a' && ch<='f';}),"host_preview","Invalid preview directory");
    const auto expected=fs_theme::containedPath(known(FOLDERID_LocalAppData),"Folderstorm/theme-host-preview/"+leaf+"/request.json");
    const auto spelling=[](std::wstring path){if(path.rfind(L"\\\\?\\UNC\\",0)==0)return L"\\\\"+path.substr(8);if(path.rfind(L"\\\\?\\",0)==0)return path.substr(4);return path;};
    const auto first=spelling(absolute.wstring()),second=spelling(expected.wstring());
    fs_theme::require(CompareStringOrdinal(first.c_str(),-1,second.c_str(),-1,TRUE)==CSTR_EQUAL,"host_preview","Preview request is outside its owned user directory");
    return expected;
}
void writeStatus(const std::filesystem::path& session,const boost::json::object& value)
{
    const auto target=fs_theme::containedPath(session,"status.json"),temporary=fs_theme::containedPath(session,"status.json.tmp");
    const auto bytes=boost::json::serialize(value);std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);stream.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));stream.close();
    fs_theme::require(stream && MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"host_preview","Could not save preview status");
}
unsigned int windowDpi(HWND window)
{
    using GetDpi=UINT(WINAPI*)(HWND);const auto call=reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));return call?call(window):96u;
}
unsigned int systemDpi()
{
    using GetDpi=UINT(WINAPI*)();const auto call=reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForSystem"));return call?call():96u;
}
struct Preview
{
    PreviewRequest request;std::filesystem::path session;std::string revision,diagnostic;
    std::optional<fs_host_ui::Palette> palette;fs_host_ui::Theme theme;
    HWND window=nullptr,edit=nullptr,combo=nullptr,list=nullptr,check=nullptr,button=nullptr,contrast=nullptr,close=nullptr;
    HFONT font=nullptr;bool forcedContrast=false;unsigned int dpi=96;int exitCode=0;
    ~Preview(){if(window && IsWindow(window))DestroyWindow(window);if(font)DeleteObject(font);}
    void status(const char* state) const
    {
        const auto rgb=[](COLORREF value){return static_cast<std::uint32_t>((GetRValue(value)<<16)|(GetGValue(value)<<8)|GetBValue(value));};
        writeStatus(session,{{"host_preview_version",1},{"request_id",request.id},{"generation",request.generation},{"pid",GetCurrentProcessId()},
            {"state",state},{"revision",revision},{"diagnostic",diagnostic},{"high_contrast",theme.highContrast},{"system_high_contrast",theme.systemHighContrast},{"dpi",dpi},{"text_scale_percent",theme.textPercent},
            {"colors",boost::json::object{{"background",rgb(theme.background)},{"panel",rgb(theme.panel)},{"input",rgb(theme.input)},{"text",rgb(theme.text)},{"muted",rgb(theme.muted)},{"accent",rgb(theme.accent)},{"border",rgb(theme.border)}}}});
    }
    void apply()
    {
        theme.set(request.baseline,forcedContrast,palette?&*palette:nullptr);theme.window(window);
        const auto next=CreateFontW(-theme.scale(14,dpi),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        fs_theme::require(next!=nullptr,"host_preview","Could not create the preview font");const auto previous=font;font=next;
        for(const auto child:{edit,combo,list,check,button,contrast,close})if(child)fs_host_ui::setNativeFont(child,font);
        if(list)SendMessageW(list,LB_SETITEMHEIGHT,0,theme.scale(28,dpi));if(previous)DeleteObject(previous);
        InvalidateRect(window,nullptr,TRUE);
    }
    HWND control(const wchar_t* type,const wchar_t* label,DWORD style,int id)
    {return CreateWindowExW(0,type,label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);}
    bool create()
    {
        dpi=windowDpi(window);edit=control(L"EDIT",L"Type something",ES_AUTOHSCROLL|WS_BORDER,10);
        combo=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL,11);
        list=control(L"LISTBOX",L"",LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|WS_BORDER|WS_VSCROLL,12);
        check=control(L"BUTTON",L"Selected",BS_AUTOCHECKBOX,13);button=control(L"BUTTON",L"Sample button",BS_PUSHBUTTON,14);
        contrast=control(L"BUTTON",L"Preview high contrast",BS_AUTOCHECKBOX,15);close=control(L"BUTTON",L"Close preview",BS_PUSHBUTTON,16);
        if(!edit || !combo || !list || !check || !button || !contrast || !close)return false;
        for(const auto* label:{L"First choice",L"Second choice",L"Third choice"})SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        for(int i=1;i<=12;++i){const auto label=L"Sample item "+std::to_wstring(i);SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
        SendMessageW(combo,CB_SETCURSEL,0,0);SendMessageW(list,LB_SETCURSEL,1,0);SendMessageW(check,BM_SETCHECK,BST_CHECKED,0);apply();layout();return true;
    }
    void layout()
    {
        if(!edit)return;RECT rect{};GetClientRect(window,&rect);const auto s=[&](int value){return theme.scale(value,dpi);};
        const int margin=s(24),gap=s(18),column=(static_cast<int>(rect.right)-margin*2-gap)/2;
        const auto move=[&](HWND child,int y,int height){MoveWindow(child,margin,s(y),column,s(height),TRUE);};
        move(edit,106,30);move(combo,152,180);move(check,196,30);move(button,240,36);move(list,294,148);
        MoveWindow(contrast,margin,static_cast<int>(rect.bottom)-s(58),column,s(32),TRUE);
        MoveWindow(close,static_cast<int>(rect.right)-margin-s(154),static_cast<int>(rect.bottom)-s(58),s(154),s(34),TRUE);
    }
    void paint(HDC dc)
    {
        RECT area{};GetClientRect(window,&area);FillRect(dc,&area,theme.backBrush);const int s=theme.scale(24,dpi);const auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);
        RECT title{s,s,area.right-s,s+theme.scale(30,dpi)};SetTextColor(dc,theme.text);DrawTextW(dc,L"Host theme preview",-1,&title,DT_LEFT|DT_SINGLELINE|DT_VCENTER);
        RECT note{s,theme.scale(64,dpi),area.right-s,theme.scale(86,dpi)};SetTextColor(dc,theme.muted);
        DrawTextW(dc,diagnostic.empty()?L"Native controls. This preview is offline.":L"Built-in fallback. The selected package could not load.",-1,&note,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
        const int left=(area.right+theme.scale(18,dpi))/2;RECT panel{left,theme.scale(106,dpi),area.right-s,theme.scale(442,dpi)};
        FillRect(dc,&panel,theme.panelBrush);const auto border=CreateSolidBrush(theme.border);FrameRect(dc,&panel,border);DeleteObject(border);
        RECT textRect{panel.left+s,panel.top+s,panel.right-s,panel.top+theme.scale(80,dpi)};SetTextColor(dc,theme.text);DrawTextW(dc,L"Welcome back",-1,&textRect,DT_LEFT|DT_SINGLELINE);
        textRect.top+=theme.scale(54,dpi);textRect.bottom+=theme.scale(54,dpi);SetTextColor(dc,theme.muted);DrawTextW(dc,L"Muted text",-1,&textRect,DT_LEFT|DT_SINGLELINE);
        textRect.top+=theme.scale(54,dpi);textRect.bottom+=theme.scale(54,dpi);SetTextColor(dc,theme.accent);DrawTextW(dc,L"Accent color",-1,&textRect,DT_LEFT|DT_SINGLELINE);
        if(old)SelectObject(dc,old);
    }
};
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    auto* preview=reinterpret_cast<Preview*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){preview=static_cast<Preview*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);preview->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(preview));}
    if(!preview)return DefWindowProcW(window,message,wp,lp);
    try
    {
        switch(message)
        {
        case WM_CREATE:return preview->create()?0:-1;
        case WM_SIZE:preview->layout();return 0;
        case WM_GETMINMAXINFO:{auto& size=*reinterpret_cast<MINMAXINFO*>(lp);size.ptMinTrackSize={preview->theme.scale(720,preview->dpi),preview->theme.scale(560,preview->dpi)};return 0;}
        case WM_DPICHANGED:{preview->dpi=HIWORD(wp);preview->apply();const auto& rect=*reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE);preview->layout();return 0;}
        case WM_SETTINGCHANGE:preview->apply();preview->layout();return 0;
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);preview->paint(dc);EndPaint(window,&paint);return 0;}
        case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:return preview->theme.color(message,wp,lp);
        case WM_DRAWITEM:{const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(preview->theme.drawCombo(item) || preview->theme.button(item))return TRUE;
            if(item.CtlType==ODT_LISTBOX){preview->theme.selection(item);if(item.itemID!=static_cast<UINT>(-1)){wchar_t textValue[64]{};SendMessageW(item.hwndItem,LB_GETTEXT,item.itemID,reinterpret_cast<LPARAM>(textValue));RECT rect=item.rcItem;rect.left+=6;SetBkMode(item.hDC,TRANSPARENT);const auto old=SelectObject(item.hDC,preview->font);DrawTextW(item.hDC,textValue,-1,&rect,DT_LEFT|DT_SINGLELINE|DT_VCENTER);if(old)SelectObject(item.hDC,old);}preview->theme.selectionFocus(item);return TRUE;}break;}
        case WM_MEASUREITEM:reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight=static_cast<UINT>(preview->theme.scale(28,preview->dpi));return TRUE;
        case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED){if(LOWORD(wp)==16){SendMessageW(window,WM_CLOSE,0,0);return 0;}if(LOWORD(wp)==15){preview->forcedContrast=SendMessageW(preview->contrast,BM_GETCHECK,0,0)==BST_CHECKED;preview->apply();preview->layout();preview->status(preview->diagnostic.empty()?"ready":"fallback_ready");return 0;}if(LOWORD(wp)==14){SetWindowTextW(preview->button,L"Button clicked");return 0;}}break;
        case WM_CLOSE:DestroyWindow(window);return 0;
        case WM_DESTROY:try{preview->status(preview->exitCode?"failed":"closed");}catch(const std::exception&){}PostQuitMessage(preview->exitCode);return 0;
        }
    }
    catch(const std::exception& error){preview->diagnostic=error.what();preview->exitCode=2;try{preview->status("failed");}catch(const std::exception&){}if(message==WM_CREATE)return -1;DestroyWindow(window);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
int run(HINSTANCE instance,int show,const std::filesystem::path& input)
{
    const auto path=requestPath(input);Preview preview;preview.session=path.parent_path();preview.request=previewRequest(fs_theme::readFile(path,4096));
    if(preview.request.choice)
    {
        try{const auto captured=capture(*preview.request.choice,known(FOLDERID_RoamingAppData)/L"Folderstorm"/L"theme-library",executableDirectory());preview.palette=uiPalette(captured.palette);preview.revision=captured.choice.revision;}
        catch(const std::exception& error){preview.diagnostic=std::string(error.what()).substr(0,2048);}
    }
    preview.theme.set(preview.request.baseline,false,preview.palette?&*preview.palette:nullptr);
    WNDCLASSEXW klass{};klass.cbSize=sizeof(klass);klass.lpfnWndProc=windowProc;klass.hInstance=instance;klass.lpszClassName=L"FolderstormHostThemePreview";klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    fs_theme::require(RegisterClassExW(&klass)!=0,"host_preview","Could not register host preview window");
    preview.dpi=systemDpi();
    const auto window=CreateWindowExW(0,klass.lpszClassName,L"Folderstorm host theme preview",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,preview.theme.scale(860,preview.dpi),preview.theme.scale(640,preview.dpi),nullptr,nullptr,instance,&preview);
    fs_theme::require(window!=nullptr,"host_preview","Could not open host preview window");ShowWindow(window,show);UpdateWindow(window);preview.status(preview.diagnostic.empty()?"ready":"fallback_ready");
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(message.message==WM_KEYDOWN && message.wParam==VK_ESCAPE){SendMessageW(window,WM_CLOSE,0,0);continue;}if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    return static_cast<int>(message.wParam);
}
}
PreviewRequest previewRequest(const std::string& bytes)
{
    fs_theme::require(bytes.size()<=4096,"size","Host preview request exceeds4 KiB");const auto value=fs_theme::parse(bytes);
    fs_theme::require(value.is_object(),"host_preview","Host preview request must be an object");const auto& object=value.as_object();
    fs_theme::require(object.size()==6 && integer(object,"host_preview_version")==1,"host_preview","Unsupported host preview request");
    PreviewRequest result;result.id=text(object,"request_id");result.generation=integer(object,"generation");const auto baseline=integer(object,"baseline");
    fs_theme::require(fs_theme::validHash(result.id) && result.generation>0 && baseline>=0 && baseline<=1,"host_preview","Invalid host preview owner or baseline");result.baseline=static_cast<unsigned int>(baseline);
    const auto id=text(object,"theme_id"),revision=text(object,"revision");
    if(!id.empty() || !revision.empty())result.choice=choose({},Choice{id,revision}).current;
    return result;
}
std::optional<int> dispatchPreview(HINSTANCE instance,int show)
{
    int count=0;auto** arguments=CommandLineToArgvW(GetCommandLineW(),&count);if(!arguments)return 2;
    bool found=false;for(int i=1;i<count;++i)if(std::wstring(arguments[i])==L"--fs-theme-host-preview")found=true;
    if(!found){LocalFree(arguments);return std::nullopt;}
    std::filesystem::path input;if(count==3 && std::wstring(arguments[1])==L"--fs-theme-host-preview")input=arguments[2];LocalFree(arguments);
    if(input.empty())return 2;
    try{return run(instance,show,input);}catch(const std::exception&){return 2;}
}
}
