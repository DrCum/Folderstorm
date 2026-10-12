/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_CHOICE_STATE_H
#define FS_SESSION_CHOICE_STATE_H
#include <optional>
#include <string>
#include <vector>
namespace fs_session
{
// Own the original IDs through CLOSEUP: Windows can send it before selection
// acceptance. Polling must not replace the list during that interval.
struct ChoiceTransaction
{
    bool open = false;
    std::string original;
    std::vector<std::string> ids;
    void begin(const std::vector<std::string>& choices, const std::string& current)
    { open = true; ids = choices; original = current; }
    std::optional<std::string> accept(int index)
    {
        if (!open || index < 0 || static_cast<std::size_t>(index) >= ids.size()) return {};
        auto selected = ids[static_cast<std::size_t>(index)]; reset(); return selected;
    }
    std::optional<std::string> cancel()
    { if (!open) return {}; auto saved = original; reset(); return saved; }
    void reset() { open = false; ids.clear(); original.clear(); }
};
}
#endif
