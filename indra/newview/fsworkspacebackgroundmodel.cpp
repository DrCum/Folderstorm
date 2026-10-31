/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsworkspacebackgroundmodel.h"
#include "llsd.h"
#include "lluuid.h"
#include <utility>

namespace FSWorkspaceBackground
{
namespace
{
bool uuid(const std::string& text)
{ LLUUID id; return text.size() == 36 && id.set(text,false) && id.notNull() && id.asString() == text; }
bool number(const LLSD& value, float& out, float low, float high)
{
    if ((!value.isReal() && !value.isInteger()) || !std::isfinite(value.asReal()) || value.asReal() < low || value.asReal() > high) return false;
    out = static_cast<float>(value.asReal()); return true;
}
LLSD imageData(const Image& image)
{
    LLSD data; data["source"] = static_cast<int>(image.source); data["mode"] = static_cast<int>(image.mode);
    data["brightness"] = image.brightness; data["color"] = LLSD::emptyArray();
    for (float c : image.color) data["color"].append(c);
    data["asset"] = image.asset; data["grid"] = image.grid; data["width"] = image.width; data["height"] = image.height; return data;
}
bool imageFrom(const LLSD& data, Image& image)
{
    if (!data.isMap() || data.size() != 8 || !data["source"].isInteger() || !data["mode"].isInteger() ||
        data["source"].asInteger() < 0 || data["source"].asInteger() > 2 || data["mode"].asInteger() < 0 || data["mode"].asInteger() > 5 ||
        !number(data["brightness"],image.brightness,0.f,1.f) || !data["color"].isArray() || data["color"].size() != 3 ||
        !data["asset"].isString() || !data["grid"].isString() || !data["width"].isInteger() || !data["height"].isInteger()) return false;
    for (int i=0;i<3;++i) if (!number(data["color"][i],image.color[i],0.f,1.f)) return false;
    image.source = static_cast<Source>(data["source"].asInteger()); image.mode = static_cast<Mode>(data["mode"].asInteger());
    image.asset = data["asset"].asString(); image.grid = data["grid"].asString();
    image.width = data["width"].asInteger(); image.height = data["height"].asInteger();
    if (image.source != Source::Local && (image.width || image.height)) return false;
    if (image.source == Source::Color) return image.asset.empty() && image.grid.empty();
    if (image.source == Source::Local) return localAsset(image.asset) && image.grid.empty() && image.width > 0 && image.height > 0 && image.width <= 8192 && image.height <= 8192;
    return uuid(image.asset) && !image.grid.empty() && image.grid.size() <= 512 && image.grid.find_first_of("\r\n") == std::string::npos;
}
}
bool localAsset(const std::string& asset)
{ return asset.size() == 40 && asset.substr(36) == ".png" && uuid(asset.substr(0,36)); }
LLSD toLLSD(const Config& config)
{
    LLSD data; data["common"] = imageData(config.common); data["arrangement"] = config.arrangement; data["displays"] = LLSD::emptyArray();
    for (const auto& pair : config.displays) { LLSD entry; entry["index"] = pair.first; entry["image"] = imageData(pair.second); data["displays"].append(entry); }
    return data;
}
LLSD toLLSD(const Choice& choice)
{ LLSD data; data["custom"] = choice.custom; if (choice.custom) data["config"] = toLLSD(choice.config); return data; }
bool fromLLSD(const LLSD& data, Config& config)
{
    if (!data.isMap() || data.size() != 3 || !data["arrangement"].isString() || !data["displays"].isArray() || data["displays"].size() > MaxDisplays) return false;
    Config parsed;
    if (!imageFrom(data["common"],parsed.common)) return false;
    parsed.arrangement = data["arrangement"].asString();
    if (parsed.arrangement.size() > 1024 || parsed.arrangement.find_first_not_of("0123456789:") != std::string::npos) return false;
    for (auto it=data["displays"].beginArray();it!=data["displays"].endArray();++it)
    {
        if (!it->isMap() || it->size() != 2 || !(*it)["index"].isInteger()) return false;
        const int index = (*it)["index"].asInteger(); Image image;
        if (index < 0 || index >= MaxDisplays || parsed.displays.count(index) || !imageFrom((*it)["image"],image) || image.mode == Mode::Span) return false;
        parsed.displays.emplace(index,std::move(image));
    }
    if (!parsed.displays.empty() && parsed.arrangement.empty()) return false;
    config = std::move(parsed); return true;
}
bool fromLLSD(const LLSD& data, Choice& choice)
{
    if (!data.isMap() || !data["custom"].isBoolean()) return false;
    Choice parsed; parsed.custom = data["custom"].asBoolean();
    if (data.size() != (parsed.custom ? 2 : 1) || (parsed.custom && !fromLLSD(data["config"],parsed.config))) return false;
    choice = std::move(parsed); return true;
}
}
