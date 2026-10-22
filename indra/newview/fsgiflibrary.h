/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_GIF_LIBRARY_H
#define FS_GIF_LIBRARY_H
#include "fsgifboardmodel.h"
namespace FSGifLibrary
{
bool load(std::vector<fs_gif_board::Item>& items, std::string& error);
bool save(fs_gif_board::Item item, std::string& error);
bool remove(const std::string& id, std::string& error);
bool importFile(const std::string& file, std::string& error);
bool exportFile(const std::string& file, std::string& error);
}
#endif
