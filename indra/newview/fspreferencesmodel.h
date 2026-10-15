/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_PREFERENCES_MODEL_H
#define FS_PREFERENCES_MODEL_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

// No viewer dependencies. The native adapter supplies the only control lookup
// and performs all runtime/permission checks before entering this transaction.
namespace fs_preferences
{
constexpr int SCHEMA_VERSION = 1;
constexpr std::size_t MAX_CHANGES = 16;
constexpr std::size_t MAX_METADATA = 3;
constexpr int MAX_TTL = 300;
enum class Type { Boolean, Real, Integer };
enum class Effect { None, UI, Camera, Graphics, Audio, Quality, LookAt };
struct Descriptor
{
    const char* id;
    const char* control;
    Type type;
    double minimum, maximum;
    bool account, writable, active;
    Effect effect;
};
inline constexpr std::array<Descriptor, 16> catalog {{
    {"letter_keys_move", "LetterKeysAffectsMovementNotFocusChatBar", Type::Boolean, 0, 1, false, true, false, Effect::None},
    {"arrow_keys_move", "ArrowKeysAlwaysMove", Type::Boolean, 0, 1, false, true, false, Effect::None},
    {"tap_hold_run", "AllowTapTapHoldRun", Type::Boolean, 0, 1, false, true, false, Effect::None},
    {"quick_jump", "FSIgnoreFinishAnimation", Type::Boolean, 0, 1, false, true, false, Effect::None},
    {"mouse_sensitivity", "MouseSensitivity", Type::Real, 0, 15, false, true, false, Effect::None},
    {"invert_mouse", "InvertMouse", Type::Boolean, 0, 1, false, true, false, Effect::None},
    {"selection_beam", "ShowSelectionBeam", Type::Boolean, 0, 1, false, true, false, Effect::None},
    {"look_at_targets", "DebugLookAt", Type::Integer, 0, 1, true, true, false, Effect::LookAt},
    {"ui_scale", "UIScaleFactor", Type::Real, .75, 2, false, true, true, Effect::UI},
    {"camera_fov", "CameraAngle", Type::Real, .17, 2.97, false, true, true, Effect::Camera},
    {"camera_offset_scale", "CameraOffsetScale", Type::Real, .5, 3, false, true, true, Effect::Camera},
    {"draw_distance", "RenderFarClip", Type::Real, 32, 1024, false, true, true, Effect::Graphics},
    {"graphics_quality", "RenderQualityPerformance", Type::Integer, 0, 6, false, false, true, Effect::Quality},
    {"master_volume", "AudioLevelMaster", Type::Real, 0, 1, false, false, false, Effect::Audio},
    {"effects_volume", "AudioLevelSFX", Type::Real, 0, 1, false, false, false, Effect::Audio},
    {"mute_audio", "MuteAudio", Type::Boolean, 0, 1, false, false, false, Effect::Audio},
}};
inline const Descriptor* descriptor(const std::string& id)
{ for (const auto& entry : catalog) if (id == entry.id) return &entry; return nullptr; }
inline bool operation(const std::string& op)
{ return op == "schema" || op == "get" || op == "status" || op == "preview" || op == "accept" || op == "cancel"; }
inline bool mutation(const std::string& op)
{ return op == "preview" || op == "accept" || op == "cancel"; }
inline bool numberAllowed(const Descriptor& entry, double value)
{ return std::isfinite(value) && value >= entry.minimum && value <= entry.maximum && (entry.type != Type::Integer || std::floor(value) == value); }

struct Identity
{
    std::string account, login, grid, viewerSession, profile, worker, generation;
    bool managed = false;
    bool valid() const
    { return !account.empty() && !login.empty() && !grid.empty() && !viewerSession.empty() && (!managed || (!profile.empty() && !worker.empty() && !generation.empty())); }
    bool operator==(const Identity& other) const
    { return account == other.account && login == other.login && grid == other.grid && viewerSession == other.viewerSession && profile == other.profile && worker == other.worker && generation == other.generation && managed == other.managed; }
};

template<class Value> struct Layers
{
    Value saved, effective, defaultValue;
    bool unsaved = false, persisted = false;
};
struct Cleanup
{
    std::string state = "idle";
    std::vector<std::string> restored, skipped;
};
struct WorkspaceOwnership
{
    bool transaction = false, ready = false, sameIdentity = false;
    bool nativeVisible = false, nativeOpening = false;
    bool definitionsUnchanged = false, activeUnchanged = false;
    bool edited = false, deferred = false;
};
inline bool dormantWorkspaceBaseline(const WorkspaceOwnership& state)
{
    return state.transaction && state.ready && state.sameIdentity && !state.nativeVisible &&
        !state.nativeOpening && state.definitionsUnchanged && state.activeUnchanged && !state.edited && !state.deferred;
}
// A synchronous native callback may update dependent metadata during our
// runtime write. Handle it even when ownership was lost, so cleanup cannot
// overwrite a later native/manual edit through that callback.
template<class Value, class Write> bool dependentPreviewWrite(bool writing, bool owned, const Value& value, Write write)
{
    if (!writing) return false;
    if (owned) write(value,false);
    return true;
}

template<class Value, class Equal> class Transaction
{
public:
    using State = Layers<Value>;
    struct Entry { std::string key; State before, expected; bool external = false; };
    explicit Transaction(Equal equal = Equal()) : mEqual(std::move(equal)) {}
    bool same(const State& a, const State& b) const
    { return mEqual(a.saved,b.saved) && mEqual(a.effective,b.effective) && mEqual(a.defaultValue,b.defaultValue) && a.unsaved == b.unsaved && a.persisted == b.persisted; }
    std::uint64_t revision() const { return mRevision; }
    void changed(const std::string& key)
    {
        ++mRevision;
        for (auto& entry : mEntries) if (entry.key == key) entry.external = true;
    }
    bool busy() const { return !mID.empty(); }
    const std::string& id() const { return mID; }
    const Identity& source() const { return mSource; }
    double deadline() const { return mDeadline; }
    const std::vector<Entry>& entries() const { return mEntries; }
    template<class Read> bool begin(const Identity& source, const std::string& id, std::uint64_t revision,
        double now, int ttl, const std::vector<std::string>& keys, Read read)
    {
        if (busy() || !source.valid() || id.empty() || revision != mRevision || !std::isfinite(now) || ttl < 1 || ttl > MAX_TTL || keys.empty() || keys.size() > MAX_CHANGES + MAX_METADATA) return false;
        std::vector<std::string> unique = keys;
        std::sort(unique.begin(), unique.end());
        if (std::adjacent_find(unique.begin(),unique.end()) != unique.end()) return false;
        mSource = source; mID = id; mDeadline = now + ttl;
        for (const auto& key : keys) { const auto state = read(key); mEntries.push_back({key,state,state,false}); }
        ++mRevision;
        return true;
    }
    template<class Read> void remember(Read read)
    { for (auto& entry : mEntries) entry.expected = read(entry.key); }
    template<class Read> bool unchanged(Read read) const
    { for (const auto& entry : mEntries) if (entry.external || !same(read(entry.key),entry.expected)) return false; return true; }
    template<class Read> bool canAccept(const Identity& source, const std::string& id, std::uint64_t revision, double now, Read read) const
    { return busy() && source == mSource && id == mID && revision == mRevision && now < mDeadline && unchanged(read); }
    template<class Read> std::vector<std::string> owned(Read read) const
    {
        std::vector<std::string> result;
        for (const auto& entry : mEntries) if (!entry.external && same(read(entry.key),entry.expected)) result.push_back(entry.key);
        return result;
    }
    // Restore primary controls before callback-owned preset metadata. Determine
    // ownership for every entry first: an earlier restore may invoke callbacks.
    template<class Read, class Write, class Allowed> Cleanup cancel(const Identity& source, Read read, Write write, Allowed allowed)
    {
        Cleanup result;
        if (!busy()) return result;
        if (!(source == mSource)) { result.state = "abandoned_identity_changed"; clear(); return result; }
        const auto ours = owned(read);
        for (const auto& entry : mEntries)
        {
            if (std::find(ours.begin(),ours.end(),entry.key) == ours.end() || !allowed(entry.key)) { result.skipped.push_back(entry.key); continue; }
            restore(entry.key,entry.before,read,write);
            (same(read(entry.key),entry.before) ? result.restored : result.skipped).push_back(entry.key);
        }
        result.state = result.skipped.empty() ? "cancelled" : "partially_restored";
        clear(); return result;
    }
    template<class Read, class Write> Cleanup accept(const Identity& source, const std::string& id, std::uint64_t revision, double now, Read read, Write write)
    {
        Cleanup result;
        if (!canAccept(source,id,revision,now,read)) { result.state = "conflict"; return result; }
        for (const auto& entry : mEntries)
        {
            write(entry.key,entry.expected.effective,true);
            const auto state = read(entry.key);
            if (!mEqual(state.saved,entry.expected.effective) || !mEqual(state.effective,entry.expected.effective) || state.unsaved || state.persisted != entry.before.persisted || !mEqual(state.defaultValue,entry.before.defaultValue))
            { result.skipped.push_back(entry.key); result.state = "partially_accepted"; clear(); return result; }
            result.restored.push_back(entry.key);
        }
        result.state = "accepted"; clear(); return result;
    }
    void clear() { mID.clear(); mEntries.clear(); mDeadline = 0; ++mRevision; }
private:
    template<class Read, class Write> void restore(const std::string& key, const State& state, Read read, Write write)
    {
        if (same(read(key),state)) return;
        if (!state.unsaved) { write(key,state.effective,true); return; }
        const auto current = read(key);
        if (!mEqual(current.saved,state.saved) || !current.unsaved) write(key,state.saved,true);
        write(key,state.effective,false);
    }
    Equal mEqual;
    Identity mSource;
    std::string mID;
    std::vector<Entry> mEntries;
    std::uint64_t mRevision = 1;
    double mDeadline = 0;
};
}
#endif
