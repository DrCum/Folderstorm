/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_HOST_OPTIONS_H
#define FS_SESSION_HOST_OPTIONS_H
#include "fssessionprotocol.h"
#include <sstream>
#include <set>
namespace fs_session
{
struct HostOptions
{
    Mode standby[2]{Mode::Warm, Mode::Warm};
    bool voice = false, muteBackground = true, hosted = false, chat = true, cinematic = false;
    unsigned int previewSize = 0, previewRate = 1; // Off until explicitly opened for a ready session.
    std::string encode() const
    {
        std::ostringstream out;
        out << "FSH1\nbackground1=" << static_cast<unsigned int>(standby[0]) << "\nbackground2=" << static_cast<unsigned int>(standby[1])
            << "\nvoice=" << voice << "\nmute=" << muteBackground << "\nhost=" << hosted << "\nchat=" << chat
            << "\npreview_size=" << previewSize << "\npreview_rate=" << previewRate << "\ncinematic=" << cinematic << '\n';
        return out.str();
    }
    static bool decode(const std::string& text, HostOptions& output)
    {
        if (text.size() > 512 || text.substr(0, 5) != "FSH1\n") return false;
        HostOptions result; std::istringstream in(text.substr(5)); std::string line; std::set<std::string> seen;
        while (std::getline(in, line))
        {
            const auto equal = line.find('=');
            if (equal == std::string::npos || line.size() != equal + 2 || line.back() < '0' || line.back() > '3') return false;
            const auto key = line.substr(0, equal); const int value = line.back() - '0';
            if (!seen.insert(key).second) return false;
            if (key == "background1" || key == "background2")
            {
                if (value != 1 && value != 2) return false;
                result.standby[key == "background1" ? 0 : 1] = static_cast<Mode>(value);
            }
            else if (key == "preview_size") { if (value > 2) return false; result.previewSize = static_cast<unsigned int>(value); }
            else if (key == "preview_rate") result.previewRate = static_cast<unsigned int>(value);
            else
            {
                if (value > 1) return false;
                if (key == "voice") result.voice = value != 0;
                else if (key == "mute") result.muteBackground = value != 0;
                else if (key == "host") result.hosted = value != 0;
                else if (key == "chat") result.chat = value != 0;
                else if (key == "cinematic") result.cinematic = value != 0;
                else return false;
            }
        }
        for (const char* required : {"background1", "background2", "voice", "mute", "host", "chat"})
            if (!seen.count(required)) return false;
        output = result; return true;
    }
};
}
#endif
