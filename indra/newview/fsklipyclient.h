/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_KLIPY_CLIENT_H
#define FS_KLIPY_CLIENT_H
#include "fsklipy.h"
#include "lluuid.h"
namespace FSKlipy
{
struct Snapshot
{
    std::vector<fs_klipy::Item> items;
    std::vector<std::string> categories;
    bool loading = false, more = false;
    unsigned revision = 0;
    unsigned page = 0; // Last successful result page; failed More requests retry it.
    std::string error;
};
const Snapshot& snapshot();
bool configured();
void browse(fs_klipy::Kind kind,const std::string& query,const std::vector<std::string>& slugs,unsigned page,bool append);
void cancelBrowse();
void acceptedShare(const std::string& slug,const std::string& query,const LLUUID& account,const LLUUID& login,const std::string& grid);
bool reportItem(const std::string& slug,const std::string& reason,const LLUUID& account,const LLUUID& login,const std::string& grid);
void preferencesOpening();
void applyPreferences();
void shutdown();
}
#endif
