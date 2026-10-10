/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_MONITOR_MODEL_H
#define FS_SESSION_MONITOR_MODEL_H
#include "fssessionregistry.h"
#include "fssessionchatmodel.h"
namespace fs_session
{
constexpr int MaxMonitors = MaxCharacters - 1;
constexpr std::uint32_t PreviewAggregateRate = 20; // Half-FPS units: 10 FPS total.
inline std::uint32_t previewTarget(unsigned int index)
{ const std::uint32_t rates[] = {1, 2, 4, 10}; return index < 4 ? rates[index] : 0; }
inline std::uint32_t previewBudget(unsigned int target, unsigned int visible)
{
    if (!visible || visible > static_cast<unsigned int>(MaxMonitors)) return 0;
    const auto cap = PreviewAggregateRate / visible;
    for (unsigned int i = 4; i > 0; --i)
        if (previewTarget(i - 1) <= cap && previewTarget(i - 1) <= previewTarget(target)) return previewTarget(i - 1);
    return 0;
}
// Reserve acknowledged and in-flight rates until a lowering acknowledgement.
// Increasing a new lane cannot temporarily exceed the aggregate budget.
template<class RateAt> bool previewAdmission(int slot, std::uint32_t desired, RateAt rateAt)
{
    if (!validSlot(slot)) return false;
    std::uint64_t total = desired;
    for (int i = 0; i < MaxCharacters; ++i) if (i != slot) total += rateAt(i);
    return total <= PreviewAggregateRate;
}
inline ChatKey monitorIdentity(const Message& source)
{ return ChatKey{source.worker, source.generation, source.account, source.grid, {}}; }
inline bool monitorSource(const Message& source)
{ return source.pid && source.generation && source.state == State::Ready && !source.account.empty() && !source.grid.empty() && source.worker != WorkerId{}; }
struct MonitorBinding
{
    bool enabled = false;
    int slot = -1;
    ChatKey key;
    unsigned int size = 0, rate = 1;
};
class MonitorSet
{
public:
    enum class Exchange { None, Kept, Exchanged, Stopped };
    std::array<MonitorBinding, MaxMonitors> bindings{};
    int find(int slot) const
    { for (int i = 0; i < MaxMonitors; ++i) if (bindings[i].enabled && bindings[i].slot == slot) return i; return -1; }
    int open(int slot, const Message& source, unsigned int size, unsigned int rate)
    {
        if (!validSlot(slot) || !monitorSource(source) || size > 2 || rate > 3) return -1;
        int index = find(slot);
        if (index >= 0) return bindings[index].key.owns(source) ? index : -1;
        for (int i = 0; i < MaxMonitors; ++i) if (!bindings[i].enabled)
        {
            bindings[i] = MonitorBinding{true, slot, monitorIdentity(source), size, rate}; return i;
        }
        return -1;
    }
    void stop(int index)
    { if (index >= 0 && index < MaxMonitors) { bindings[index].enabled = false; if (pending == index) pending = -1; } }
    void stopSlot(int slot) { stop(find(slot)); }
    void stopAll() { for (int i = 0; i < MaxMonitors; ++i) stop(i); }
    void beginExchange(int current, int target, const Message* original, const Message& promoted)
    {
        pending = find(target); old = current; next = target;
        if (pending < 0 || !bindings[pending].key.owns(promoted)) { pending = -1; return; }
        previous = original && monitorSource(*original) ? monitorIdentity(*original) : ChatKey{};
    }
    int pendingIndex() const { return pending; }
    template<class SourceAt> Exchange finish(bool success, int active, SourceAt sourceAt)
    {
        const int index = pending; pending = -1;
        if (index < 0) return Exchange::None;
        auto& binding = bindings[index];
        if (!success) return Exchange::Kept;
        const auto* promoted = sourceAt(next);
        const auto* original = validSlot(old) ? sourceAt(old) : nullptr;
        if (active != next || !binding.enabled || !promoted || !binding.key.owns(*promoted) ||
            !original || !monitorSource(*original) || !previous.owns(*original) || find(old) >= 0)
        { binding.enabled = false; return Exchange::Stopped; }
        binding.slot = old; binding.key = previous;
        return Exchange::Exchanged;
    }
private:
    int pending = -1, old = -1, next = -1;
    ChatKey previous;
};
}
#endif
