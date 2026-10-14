/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_FRIENDS_UI_H
#define FS_HOST_FRIENDS_UI_H
#include <windows.h>
#include "fssessioncontacts.h"
namespace fs_host_ui
{
struct FriendsPane
{
    HWND window=nullptr,label=nullptr,filter=nullptr,searchButton=nullptr,friendsButton=nullptr,list=nullptr,open=nullptr,popout=nullptr,pin=nullptr;
    HFONT font=nullptr;
    UINT dpi=96;
    int slot=-1,monitor=-1;
    bool standalone=false,searching=false,updating=false;
    fs_session::ChatKey source;
    std::uint64_t token=0,revision=0,queryToken=0;
    std::string lastFilter;
    std::vector<fs_session::Contact> rows;
    std::vector<std::wstring> labels;
    ~FriendsPane(){if(font)DeleteObject(font);}
    FriendsPane()=default;FriendsPane(const FriendsPane&)=delete;FriendsPane& operator=(const FriendsPane&)=delete;
};
}
#endif
