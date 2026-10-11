/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_INPUT_GUARD_H
#define FS_SESSION_INPUT_GUARD_H
#include <array>
#include <cstddef>
namespace fs_session
{
// Physical Win32 virtual keys, never the viewer's lock-key toggle levels.
class InputQuarantine
{
public:
    template<typename PhysicalDown> void capture(PhysicalDown down)
    {
        reset();
        for (unsigned int key = 7; key < held.size(); ++key) held[key] = down(key);
    }
    template<typename PhysicalDown> void refresh(PhysicalDown down)
    {
        for (unsigned int key = 7; key < held.size(); ++key)
            if (held[key] && !down(key)) held[key] = false;
    }
    bool allows(unsigned int nativeKey) const { return nativeKey >= held.size() || !held[nativeKey]; }
    void released(unsigned int nativeKey) { if (nativeKey < held.size()) held[nativeKey] = false; }
    void reset() { held.fill(false); }
    std::size_t size() const
    { std::size_t count = 0; for (bool value : held) if (value) ++count; return count; }
private:
    std::array<bool,256> held{};
};
}
#endif
