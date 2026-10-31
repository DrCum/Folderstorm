/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_WORKSPACE_BACKGROUND_MODEL_H
#define FS_WORKSPACE_BACKGROUND_MODEL_H

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <vector>

class LLSD;
namespace FSWorkspaceBackground
{
constexpr int MaxDisplays = 16;
enum class Source { Color, Texture, Local };
enum class Mode { Fill, Fit, Stretch, Center, Tile, Span };
struct Image
{
    Source source = Source::Color;
    Mode mode = Mode::Fill;
    std::array<float, 3> color{0.f, 0.f, 0.f};
    float brightness = 1.f;
    int width = 0, height = 0; // Original local pixel size before texture normalization.
    std::string asset, grid; // Texture UUID/grid, or an imported UUID.png basename.
};
struct Config
{
    Image common;
    std::string arrangement; // Normalized geometry only; never native display IDs.
    std::map<int, Image> displays;
};
struct Choice
{
    bool custom = false;
    Config config;
};
struct Rect
{
    double left = 0., bottom = 0., right = 0., top = 0.;
    double width() const { return right - left; }
    double height() const { return top - bottom; }
    bool empty() const { return width() <= 0. || height() <= 0.; }
};
inline Rect intersect(const Rect& a, const Rect& b)
{ return {(std::max)(a.left,b.left), (std::max)(a.bottom,b.bottom), (std::min)(a.right,b.right), (std::min)(a.top,b.top)}; }
// Non-overlapping strips: never paint across the interactive world viewport.
inline std::vector<Rect> outside(const Rect& frame, const Rect& world)
{
    if (frame.empty()) return {};
    const auto hole = intersect(frame, world);
    if (hole.empty()) return {frame};
    std::vector<Rect> result;
    for (const auto& r : {Rect{frame.left,frame.bottom,frame.right,hole.bottom},
        Rect{frame.left,hole.top,frame.right,frame.top}, Rect{frame.left,hole.bottom,hole.left,hole.top},
        Rect{hole.right,hole.bottom,frame.right,hole.top}}) if (!r.empty()) result.push_back(r);
    return result;
}
inline Rect bounds(const std::vector<Rect>& regions)
{
    Rect result;
    for (const auto& r : regions) if (!r.empty())
    {
        if (result.empty()) result = r;
        else result = {(std::min)(result.left,r.left),(std::min)(result.bottom,r.bottom),
            (std::max)(result.right,r.right),(std::max)(result.top,r.top)};
    }
    return result;
}
struct Placement { Rect destination, uv{0.,0.,1.,1.}; };
inline Placement place(const Rect& target, double width, double height, Mode mode)
{
    if (target.empty() || width <= 0. || height <= 0.) return {};
    Placement p{target,{0.,0.,1.,1.}};
    if (mode == Mode::Tile) { p.uv.right = target.width()/width; p.uv.top = target.height()/height; return p; }
    if (mode == Mode::Stretch) return p;
    double scale = 1.;
    if (mode == Mode::Fill || mode == Mode::Span) scale = (std::max)(target.width()/width,target.height()/height);
    if (mode == Mode::Fit) scale = (std::min)(target.width()/width,target.height()/height);
    const double w = width*scale, h = height*scale;
    p.destination = {target.left+(target.width()-w)*.5,target.bottom+(target.height()-h)*.5,
        target.left+(target.width()+w)*.5,target.bottom+(target.height()+h)*.5};
    return p;
}
inline std::vector<Rect> orderedDisplays(std::vector<Rect> displays)
{
    displays.erase(std::remove_if(displays.begin(),displays.end(),[](const Rect& r){return r.empty();}),displays.end());
    std::sort(displays.begin(),displays.end(),[](const Rect& a,const Rect& b)
    { return a.left == b.left ? a.top > b.top : a.left < b.left; });
    if (displays.size() > MaxDisplays) displays.resize(MaxDisplays);
    return displays;
}
inline std::string arrangement(const std::vector<Rect>& displays)
{
    const auto frame = bounds(displays);
    if (frame.empty()) return {};
    std::string key = std::to_string(displays.size());
    for (const auto& r : displays)
        for (double v : {(r.left-frame.left)/frame.width(),(r.bottom-frame.bottom)/frame.height(),
            r.width()/frame.width(),r.height()/frame.height()}) key += ":" + std::to_string(static_cast<int>(std::lround(v*1000.)));
    return key;
}
inline const Config& resolve(const Choice& choice, const Config& defaults)
{ return choice.custom ? choice.config : defaults; }
inline const Image& forDisplay(const Config& config, const std::string& topology, int index)
{
    const auto found = config.displays.find(index);
    return config.common.mode != Mode::Span && config.arrangement == topology && found != config.displays.end() ? found->second : config.common;
}
LLSD toLLSD(const Config& config);
LLSD toLLSD(const Choice& choice);
bool fromLLSD(const LLSD& data, Config& config);
bool fromLLSD(const LLSD& data, Choice& choice);
bool localAsset(const std::string& asset);
}
#endif
