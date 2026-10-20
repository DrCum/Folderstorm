/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_ASSISTANT_WORLD_MODEL_H
#define FS_ASSISTANT_WORLD_MODEL_H
#include <array>
#include <cmath>
#include <map>
#include <string>
namespace fs_assistant_world
{
template<std::size_t N> bool finite(const std::array<double,N>& values, double low, double high)
{
    for (double value : values) if (!std::isfinite(value) || value < low || value > high) return false;
    return true;
}
inline bool quaternion(const std::array<double,4>& value)
{
    if (!finite(value,-1.,1.)) return false;
    double norm = 0.; for (double component : value) norm += component * component;
    return std::abs(norm - 1.) <= .001;
}
inline bool nextOwner(bool copy, bool transfer) { return copy || transfer; }
// Viewer-owned opaque tickets; source includes the private native login ID.
// Nothing from this cache is serialized automatically.
template<class Value> class Tickets
{
public:
    struct Entry { std::string source; double deadline; Value value; bool executed = false; };
    void put(const std::string& id, const std::string& source, double now, Value value)
    {
        prune(now);
        if (entries.size() >= 64) return; // Never evict a live asynchronous guard.
        entries.emplace(id,Entry{source,now+120.,std::move(value),false});
    }
    Entry* get(const std::string& id, const std::string& source, double now)
    {
        auto found = entries.find(id);
        return found != entries.end() && found->second.source == source && now < found->second.deadline ? &found->second : nullptr;
    }
    void prune(double now)
    {
        for (auto it=entries.begin(); it!=entries.end();) if (now >= it->second.deadline) it=entries.erase(it); else ++it;
    }
    void clear() { entries.clear(); }
private:
    std::map<std::string,Entry> entries;
};
}
#endif
