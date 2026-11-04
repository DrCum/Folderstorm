/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "host_preview.h"
#include <commctrl.h>
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show)
{
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    using DpiContext=BOOL(WINAPI*)(HANDLE);const auto dpi=reinterpret_cast<DpiContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext"));
    if(dpi)dpi(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));else SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    const auto result=fs_host_theme::dispatchPreview(instance,show);if(SUCCEEDED(com))CoUninitialize();return result.value_or(2);
}
