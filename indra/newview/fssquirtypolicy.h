/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SQUIRTY_POLICY_H
#define FS_SQUIRTY_POLICY_H

#include <map>
#include <string>

namespace FSSquirtyPolicy
{
enum class Mode { Off, All, Custom };

inline bool allows(Mode mode, bool selected)
{
    return mode == Mode::All || (mode == Mode::Custom && selected);
}

inline bool validToken(const std::string& value)
{
    return value.size() >= 32 && value.size() <= 256 &&
           value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") == std::string::npos;
}

class Activity
{
public:
    // Called only for accepted live resident IMs. Suppressed messages still
    // reset the inactivity timer, including while Off or Do Not Disturb.
    bool incoming(const std::string& id, double now, double silence,
                  Mode mode, bool selected, bool suppressed)
    {
        auto previous = mLastMessage.find(id);
        const bool idle = previous == mLastMessage.end() || now - previous->second >= silence;
        if (previous == mLastMessage.end() && mLastMessage.size() >= 4096)
        {
            auto oldest = mLastMessage.begin();
            for (auto it = mLastMessage.begin(); it != mLastMessage.end(); ++it)
                if (it->second < oldest->second) oldest = it;
            mLastMessage.erase(oldest);
        }
        mLastMessage[id] = now;
        return idle && !suppressed && allows(mode, selected);
    }

    void seedPresence(const std::string& id, bool online) { mPresence[id] = online; }

    bool presence(const std::string& id, bool online, Mode mode, bool selected, bool suppressed)
    {
        auto previous = mPresence.find(id);
        const bool logged_on = previous != mPresence.end() && !previous->second && online;
        mPresence[id] = online;
        return logged_on && !suppressed && allows(mode, selected);
    }

    void reset() { mLastMessage.clear(); mPresence.clear(); }

private:
    std::map<std::string, double> mLastMessage;
    std::map<std::string, bool> mPresence;
};
}
#endif
