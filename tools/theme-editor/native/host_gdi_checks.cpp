/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "host_palette_ui.h"
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void brush(HBRUSH handle,COLORREF color)
{LOGBRUSH value{};check(GetObjectW(handle,sizeof(value),&value)==sizeof(value) && value.lbColor==color,"GDI brush did not use the exact RGB value");}
}
int main(int count,char** args)
{
    try
    {
        check(count==5,"Pass library root, base root, theme id and revision");
        const auto captured=fs_host_theme::capture({args[3],args[4]},fs_theme::fromUtf8(args[1]),fs_theme::fromUtf8(args[2]));
        const auto palette=fs_host_theme::uiPalette(captured.palette);
        fs_host_ui::Theme theme;theme.set(1,false,&palette);
        if(!theme.systemHighContrast)
        {
            check(theme.background==palette.background && theme.panel==palette.panel && theme.input==palette.input && theme.text==palette.text && theme.muted==palette.muted && theme.accent==palette.accent && theme.border==palette.border,"Custom palette did not replace all seven built-in roles");
            brush(theme.backBrush,palette.background);brush(theme.panelBrush,palette.panel);brush(theme.inputBrush,palette.input);
        }
        else check(theme.background==GetSysColor(COLOR_BTNFACE) && theme.text==GetSysColor(COLOR_WINDOWTEXT),"System high contrast lost precedence");
        // Compare equal, warmed sequences rather than the initial process state.
        for(int i=0;i<50;++i){theme.set(0,false,&palette);theme.set(1);}
        GdiFlush();
        const auto handles=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        for(int i=0;i<50;++i){theme.set(0,false,&palette);theme.set(1);}
        GdiFlush();const auto after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        if(after!=handles)throw std::runtime_error("Replacing the host palette changed GDI count from "+std::to_string(handles)+" to "+std::to_string(after));
        theme.set(0,true,&palette);
        check(theme.highContrast,"Host high contrast preference was ignored");
        if(!theme.systemHighContrast)check(theme.background==RGB(0,0,0) && theme.text==RGB(255,255,255) && theme.accent==RGB(255,255,0),"Custom palette overrode explicit host high contrast");
        brush(theme.backBrush,theme.background);brush(theme.selectionBrush,theme.accent);
        theme.set(0);if(!theme.systemHighContrast)check(theme.background==RGB(17,16,22) && theme.accent==RGB(191,164,252),"Removing custom palette did not restore built-in choice");
        std::cout<<"PASS captured immutable host revision, exact seven RGB roles/GDI brushes, repeated replacement, high-contrast precedence and built-in fallback; system_high_contrast="<<(theme.systemHighContrast?"on":"off")<<"; warmed_gdi_objects="<<after<<'\n';
        return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
