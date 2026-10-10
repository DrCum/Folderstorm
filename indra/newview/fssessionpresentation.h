/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_PRESENTATION_H
#define FS_SESSION_PRESENTATION_H
#include "fssessionprotocol.h"
#include <map>
#include <set>
#include <tuple>
#include <vector>
namespace fs_session
{
struct AccountKey
{
    std::string account, grid;
    bool operator<(const AccountKey& other) const { return std::tie(account, grid) < std::tie(other.account, other.grid); }
    bool operator==(const AccountKey& other) const { return account == other.account && grid == other.grid; }
    bool valid() const { return !account.empty() && validAccount(account) && !grid.empty() && grid.size() < 128 && validUtf8(grid); }
    static AccountKey from(const Message& message) { return {message.account, message.grid}; }
};
inline std::uint32_t defaultAccountColor(const AccountKey& key)
{
    const std::uint32_t palette[] = {0x247ac0, 0xa543a6, 0x17845c, 0xb36016, 0x635dc0};
    std::uint32_t hash = 2166136261u;
    for (unsigned char c : key.account + key.grid) hash = (hash ^ c) * 16777619u;
    return palette[hash % 5];
}
struct CharacterAppearance
{
    std::string alias;
    std::uint32_t color = 0x247ac0;
    unsigned int alerts = 0; // 0 off, 1 IM, 2 IM and supported attention.
};
struct ShellRect
{
    int x = 0, y = 0, width = 0, height = 0;
    unsigned int dpi = 96;
    bool valid() const { return width >= 100 && height >= 80 && width <= 8192 && height <= 8192 &&
        x >= -100000 && x <= 100000 && y >= -100000 && y <= 100000 && dpi >= 96 && dpi <= 768; }
};
inline ShellRect fitShellRect(ShellRect saved, const ShellRect& work, unsigned int dpi)
{
    if (!saved.valid() || !work.valid() || dpi < 96 || dpi > 768) return {};
    const auto scale = [dpi, saved](int n) { return static_cast<int>(static_cast<std::int64_t>(n) * dpi / saved.dpi); };
    saved.width = (std::min)(work.width, (std::max)(100, scale(saved.width)));
    saved.height = (std::min)(work.height, (std::max)(80, scale(saved.height)));
    saved.x = (std::max)(work.x, (std::min)(saved.x, work.x + work.width - saved.width));
    saved.y = (std::max)(work.y, (std::min)(saved.y, work.y + work.height - saved.height));
    saved.dpi = dpi; return saved;
}
struct SavedPin
{
    AccountKey owner;
    Topic topic = Topic::Nearby;
    std::string destination; // Durable peer/group ID only, never a transient session.
    bool valid() const { return owner.valid() && (topic == Topic::Nearby ? destination.empty() :
        (topic == Topic::Private || topic == Topic::Group) && !destination.empty() && validAccount(destination)); }
    bool operator==(const SavedPin& other) const { return owner == other.owner && topic == other.topic && destination == other.destination; }
};
struct ShortcutChord
{
    unsigned int key = 0, modifiers = 0; // Win32 virtual key; 1 Alt, 2 Ctrl, 4 Shift.
    bool operator==(const ShortcutChord& other) const { return key == other.key && modifiers == other.modifiers; }
    bool valid() const { return key == 0 ? modifiers <= 7 : ((key >= 49 && key <= 53) || key == 37 || key == 39 || (key >= 112 && key <= 123)) &&
        (modifiers == 3 || modifiers == 5 || modifiers == 6 || modifiers == 7); }
};
// Strings are length-prefixed UTF-8. No credentials, conversation text or drafts.
class PresentationStore
{
public:
    static constexpr std::size_t MaxBytes = 65536, MaxProfiles = 32, MaxPins = 24;
    std::map<AccountKey, CharacterAppearance> accounts;
    std::vector<SavedPin> pins;
    std::map<unsigned int, ShellRect> rectangles;
    unsigned int transition = 0, duration = 1100, height = 64, volume = 40;
    bool shortcuts = false;
    std::array<ShortcutChord, 7> bindings{{{49,5},{50,5},{51,5},{52,5},{53,5},{39,5},{37,5}}};
    CharacterAppearance appearance(const AccountKey& key) const
    {
        const auto found = accounts.find(key);
        return found == accounts.end() ? CharacterAppearance{"", defaultAccountColor(key), 0} : found->second;
    }
    bool set(const AccountKey& key, const CharacterAppearance& value)
    {
        if (!key.valid() || !validUtf8(value.alias) || value.alias.size() > 64 || value.color > 0xffffff || value.alerts > 2 ||
            (accounts.size() >= MaxProfiles && !accounts.count(key))) return false;
        accounts[key] = value; return true;
    }
    bool valid() const
    {
        if (accounts.size() > MaxProfiles || pins.size() > MaxPins || rectangles.size() > 7 || transition > 2 ||
            duration < 250 || duration > 2000 || height < 16 || height > 96 || volume > 100) return false;
        for (const auto& entry : accounts) if (!entry.first.valid() || !validUtf8(entry.second.alias) ||
            entry.second.alias.size() > 64 || entry.second.color > 0xffffff || entry.second.alerts > 2) return false;
        for (std::size_t i = 0; i < pins.size(); ++i)
        {
            if (!pins[i].valid()) return false;
            unsigned int count = 0;
            for (std::size_t j = 0; j < pins.size(); ++j)
            { if (j < i && pins[i] == pins[j]) return false; if (pins[i].owner == pins[j].owner) ++count; }
            if (count > 8) return false;
        }
        for (const auto& rect : rectangles) if (rect.first > 6 || !rect.second.valid()) return false;
        for (std::size_t i = 0; i < bindings.size(); ++i)
        {
            if (!bindings[i].valid()) return false;
            for (std::size_t j = 0; j < i; ++j) if (bindings[i].key && bindings[i] == bindings[j]) return false;
        }
        return true;
    }
    std::string encode() const
    {
        if (!valid()) return {};
        std::string out = "FSP1";
        const auto number = [&out](std::uint32_t n) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((n >> (8 * i)) & 255)); };
        const auto string = [&out, &number](const std::string& s) { number(static_cast<std::uint32_t>(s.size())); out += s; };
        number(transition); number(duration); number(height); number(volume); number(shortcuts ? 1u : 0u);
        for (const auto& binding : bindings) { number(binding.key); number(binding.modifiers); }
        number(static_cast<std::uint32_t>(accounts.size()));
        for (const auto& entry : accounts)
        { string(entry.first.account); string(entry.first.grid); string(entry.second.alias); number(entry.second.color); number(entry.second.alerts); }
        number(static_cast<std::uint32_t>(pins.size()));
        for (const auto& pin : pins) { string(pin.owner.account); string(pin.owner.grid); number(static_cast<std::uint32_t>(pin.topic)); string(pin.destination); }
        number(static_cast<std::uint32_t>(rectangles.size()));
        for (const auto& entry : rectangles)
        {
            number(entry.first); number(static_cast<std::uint32_t>(entry.second.x)); number(static_cast<std::uint32_t>(entry.second.y));
            number(static_cast<std::uint32_t>(entry.second.width)); number(static_cast<std::uint32_t>(entry.second.height)); number(entry.second.dpi);
        }
        return out.size() <= MaxBytes ? out : std::string{};
    }
    static bool decode(const std::string& data, PresentationStore& output)
    {
        if (data.size() > MaxBytes || data.substr(0,4) != "FSP1") return false;
        PresentationStore value; std::size_t offset = 4; bool ok = true;
        const auto number = [&]() -> std::uint32_t
        {
            if (offset + 4 > data.size()) { ok = false; return 0; }
            std::uint32_t n = 0; for (int i = 0; i < 4; ++i) n |= static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset++])) << (8 * i);
            return n;
        };
        const auto string = [&]() -> std::string
        {
            const auto size = number();
            if (!ok || size > 127 || size > data.size() - offset) { ok = false; return {}; }
            const auto result = data.substr(offset, size); offset += size; if (!validUtf8(result)) ok = false; return result;
        };
        value.transition = number(); value.duration = number(); value.height = number(); value.volume = number();
        const auto enabled = number(); if (enabled > 1) return false; value.shortcuts = enabled != 0;
        for (auto& binding : value.bindings) { binding.key = number(); binding.modifiers = number(); }
        auto count = number(); if (!ok || count > MaxProfiles) return false;
        for (std::uint32_t i = 0; i < count && ok; ++i)
        {
            AccountKey key; key.account = string(); key.grid = string(); CharacterAppearance appearance;
            appearance.alias = string(); appearance.color = number(); appearance.alerts = number();
            if (!value.accounts.emplace(key, appearance).second) return false;
        }
        count = number(); if (!ok || count > MaxPins) return false;
        for (std::uint32_t i = 0; i < count && ok; ++i)
        { SavedPin pin; pin.owner.account = string(); pin.owner.grid = string(); pin.topic = static_cast<Topic>(number()); pin.destination = string(); value.pins.push_back(pin); }
        count = number(); if (!ok || count > 7) return false;
        const auto signedNumber = [&]() -> int
        { const auto n = number(); return n <= 0x7fffffffu ? static_cast<int>(n) : -1 - static_cast<int>(0xffffffffu - n); };
        for (std::uint32_t i = 0; i < count && ok; ++i)
        {
            const auto id = number(); ShellRect rect; rect.x = signedNumber(); rect.y = signedNumber();
            rect.width = signedNumber(); rect.height = signedNumber(); rect.dpi = number();
            if (!value.rectangles.emplace(id, rect).second) return false;
        }
        if (!ok || offset != data.size() || !value.valid()) return false;
        output = std::move(value); return true;
    }
};
}
#endif
