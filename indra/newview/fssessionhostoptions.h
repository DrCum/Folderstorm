/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_HOST_OPTIONS_H
#define FS_SESSION_HOST_OPTIONS_H
#include "fssessionregistry.h"
#include <sstream>
#include <set>
namespace fs_session
{
struct HostOptions
{
    std::array<Mode, MaxCharacters> standby = warmModes();
    bool chatDetached = false, controlsDetached = false;
    bool voice = false, muteBackground = true, hosted = false, chat = true, cinematic = false;
    unsigned int chrome = 0; // 0 normal, 1 condensed, 2 title-bar-only chrome.
    unsigned int previewSize = 0, previewRate = 1; // Off until explicitly opened for a ready session.
    std::string encode() const
    {
        std::ostringstream out;
        out << "FSH1\n";
        for (int i = 0; i < MaxCharacters; ++i) out << "background" << i + 1 << "=" << static_cast<unsigned int>(standby[i]) << '\n';
        out << "voice=" << voice << "\nmute=" << muteBackground << "\nhost=" << hosted << "\nchat=" << chat
            << "\nchat_detached=" << chatDetached << "\ncontrols_detached=" << controlsDetached << "\nchrome=" << chrome << "\npreview_size=" << previewSize << "\npreview_rate=" << previewRate << "\ncinematic=" << cinematic << '\n';
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
            if (key.size() == 11 && key.compare(0, 10, "background") == 0 && key.back() >= '1' && key.back() <= '0' + MaxCharacters)
            {
                if (value != 1 && value != 2) return false;
                result.standby[static_cast<std::size_t>(key.back() - '1')] = static_cast<Mode>(value);
            }
            else if (key == "chrome") { if (value > 2) return false; result.chrome = static_cast<unsigned int>(value); }
            else if (key == "preview_size") { if (value > 2) return false; result.previewSize = static_cast<unsigned int>(value); }
            else if (key == "preview_rate") result.previewRate = static_cast<unsigned int>(value);
            else
            {
                if (value > 1) return false;
                if (key == "chat_detached") result.chatDetached = value != 0;
                else if (key == "controls_detached") result.controlsDetached = value != 0;
                else if (key == "voice") result.voice = value != 0;
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
