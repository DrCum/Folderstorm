#ifndef FS_SAMPLING_SHADOW_H
#define FS_SAMPLING_SHADOW_H
#include <array>
#include <cstdint>
#include <memory>

class LLMotion;
class LLMotionController;

namespace FSSampling
{
// Main-thread diagnostics only. No scene pointers or shadow buffers go to a worker.
struct ShadowTotals
{
    std::uint64_t scopes = 0;
    std::uint64_t batches = 0, predicted = 0, matched = 0, mismatched = 0, unused = 0;
    std::uint64_t unpredicted = 0, excluded = 0, rejected = 0, nested = 0, destroyed = 0;
    std::uint64_t prepareNs = 0, validationNs = 0, observationNs = 0, cleanupNs = 0, firstMatched = 0;
    std::uint64_t bufferBytesPeak = 0, retainedKeysPeak = 0;
    // Bucket i covers gaps <= 2^i ns; the final bucket also includes larger gaps.
    std::array<std::uint64_t, 32> matchedGapNs{};
    std::array<std::uint64_t, 32> firstMatchedGapNs{};
};

class ShadowScope;
extern thread_local ShadowScope* activeShadow;

class ShadowScope
{
public:
    static constexpr unsigned MAX_MOTIONS = 32;
    static constexpr unsigned MAX_BUFFER_BYTES = 64 * 1024;
    static constexpr unsigned MAX_RETAINED_KEYS = 32768;
    ShadowScope(LLMotionController& controller, bool enabled, bool force_update = false, bool will_unpause = false);
    ~ShadowScope();
    ShadowScope(const ShadowScope&) = delete;
    ShadowScope& operator=(const ShadowScope&) = delete;
    void observe(LLMotionController& controller, LLMotion& motion, float time);
    static void controllerDestroyed(LLMotionController& controller);
    static ShadowTotals totals();
    static bool resetTotals(); // A live scope must finish before counters can be reset.
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
    ShadowScope* mPrevious = nullptr;
    LLMotionController* mController = nullptr; // Owner-only scoped reference; destruction explicitly cancels it.
    bool mRegistered = false, mCancelled = false;
    void finish();
};
}
#endif
