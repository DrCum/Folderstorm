/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_THEME_VIEWER_H
#define FS_THEME_VIEWER_H
namespace FSThemeViewer
{
bool configure();
bool preview();
bool idlePreview();
void captureFrame();
void openEditor(bool library=false);
}
#endif
