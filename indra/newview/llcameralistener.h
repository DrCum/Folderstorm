/**
 * @file llcameralistener.h
 * @brief Local Event API for camera pose and snapshots.
 */

#ifndef LL_LLCAMERALISTENER_H
#define LL_LLCAMERALISTENER_H

#include "lleventapi.h"
#include "llimage.h"

#include <string>

class LLCameraListener : public LLEventAPI
{
public:
    LLCameraListener();

private:
    void get(LLSD const& data);
    void setPose(LLSD const& data);
    void set(LLSD const& data);
    void reset(LLSD const& data);
    void snapshot(LLSD const& data);
};

// Shared capture used by LLCamera snapshot and inventory_snapshot_upload.
struct FSSnapshotDefaults
{
    bool viewport_only = false;
    // When size is omitted, capture a square of square_edge instead of the window.
    bool square = false;
    int square_edge = 1024;
};

struct FSSnapshotFrame
{
    int width = 0;
    int height = 0;
    bool show_ui = false;
    bool show_hud = false;
    bool viewport_only = false;
    // True when width and height were requested, including a square default.
    bool explicit_size = false;
};

bool fs_parse_snapshot_frame(const LLSD& data, const FSSnapshotDefaults& defaults, FSSnapshotFrame& frame, std::string& error);
bool fs_capture_snapshot_image(const FSSnapshotFrame& frame, LLPointer<LLImageRaw>& image, std::string& error);

#endif
