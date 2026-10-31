/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_WORKSPACE_BACKGROUND_H
#define FS_WORKSPACE_BACKGROUND_H
#include "fsworkspacebackgroundmodel.h"
#include <functional>

namespace FSWorkspaceBackground
{
Choice current();
Config defaults();
std::vector<Rect> displays();
Rect client();
Rect viewport();
void initialize();
void shutdown();
void refresh();
// Draws on the default framebuffer after scene finalization, before HUD/UI.
void draw();
// Settings preview uses the same placement and shared fetched textures.
void preview(const Config& config, const Rect& target);
void previewDemand(const Config* config);
std::string imageStatus(const Image& image);
// One bounded import at a time. Callback is delivered on idle, for its source login only.
bool importImage(const std::string& filename, std::function<bool(Image,std::string)> done);
bool importing();
}
#endif
