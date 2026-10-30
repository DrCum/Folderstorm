// Actual inline read methods and producer updates, without viewer initialization.
#include "linden_common.h"
#include "llperfstats.h"
#include "../../llcharacter/tests/sampling_allocation_probe.h"
#include <cstdio>
#include <iostream>
#include <vector>

namespace LLPerfStats
{
// The same static storage definitions as llperfstats.cpp. No singleton/window.
std::atomic<int> StatsRecorder::writeBuffer{0};
std::array<StatsRecorder::StatsTypeMatrix, 2> StatsRecorder::statsDoubleBuffer{};
std::array<StatsRecorder::StatsSummaryArray, 2> StatsRecorder::max{};
std::array<StatsRecorder::StatsSummaryArray, 2> StatsRecorder::sum{};

struct StatsReadTestAccess
{
    static void reset()
    {
        for (auto& buffer : StatsRecorder::statsDoubleBuffer)
            for (auto& map : buffer) map.clear();
        StatsRecorder::max = {}; StatsRecorder::sum = {};
        StatsRecorder::writeBuffer.store(0);
    }
    static void readBuffer(unsigned index) { StatsRecorder::writeBuffer.store(index ^ 1); }
    static void add(unsigned buffer, ObjType_t type, const LLUUID& id, StatType_t stat, U64 value)
    {
        StatsRecorder::writeBuffer.store(buffer);
        StatsRecorder::doUpd(id, type, stat, value);
    }
    static void erase(unsigned buffer, ObjType_t type, const LLUUID& id)
    { StatsRecorder::statsDoubleBuffer[buffer][size_t(type)].erase(id); }
    static auto buffers() { return StatsRecorder::statsDoubleBuffer; }
    static auto summaries() { return std::make_pair(StatsRecorder::max, StatsRecorder::sum); }
    static size_t nodes()
    {
        size_t count = 0;
        for (const auto& buffer : StatsRecorder::statsDoubleBuffer)
            for (const auto& map : buffer) count += map.size();
        return count;
    }
};
}
namespace
{
using namespace LLPerfStats;
using Access = StatsReadTestAccess;
unsigned checks = 0, failures = 0;
void check(bool passed, const char* label)
{
    ++checks;
    if (!passed) { ++failures; std::cerr << "FAILED: " << label << '\n'; }
}
LLUUID id(unsigned number)
{
    char text[37];
    std::snprintf(text, sizeof(text), "f0123456-1234-5678-9012-%012x", number);
    return LLUUID(text);
}
void values()
{
    Access::reset();
    for (unsigned buffer = 0; buffer < 2; ++buffer)
        for (unsigned type = 0; type < unsigned(ObjType_t::OT_COUNT); ++type)
            for (unsigned stat = 0; stat < unsigned(StatType_t::STATS_COUNT); ++stat)
            {
                Access::add(buffer, ObjType_t(type), id(1), StatType_t(stat), 1000 + buffer * 100 + stat);
                Access::add(buffer, ObjType_t(type), LLUUID::null, StatType_t(stat), 500 + buffer * 200 + stat);
            }
    Access::erase(0, ObjType_t::OT_AVATAR, id(1));
    Access::readBuffer(0);
    const auto erasedCount = Access::nodes();
    check(StatsRecorder::get(ObjType_t::OT_AVATAR, id(1), StatType_t::RENDER_IDLE) == 0,
          "erased avatar must read zero");
    check(Access::nodes() == erasedCount, "read recreated an erased avatar");
    Access::add(0, ObjType_t::OT_AVATAR, id(1), StatType_t::RENDER_IDLE, 991);
    auto before = Access::buffers(); const auto summaries = Access::summaries();
    auto frozen = before;
    for (unsigned buffer = 0; buffer < 2; ++buffer)
    {
        Access::readBuffer(buffer);
        for (unsigned type = 0; type < unsigned(ObjType_t::OT_COUNT); ++type)
            for (const auto& key : {LLUUID::null, id(1), id(2), id(3)})
                for (unsigned stat = 0; stat < unsigned(StatType_t::STATS_COUNT); ++stat)
                {
                    // Frozen old expression on separate storage, including zero insertion.
                    const auto expected = frozen[buffer][type][key][stat];
                    check(StatsRecorder::get(ObjType_t(type), key, StatType_t(stat)) == expected,
                          "public read differs from frozen populated/absent value");
                    if (type == unsigned(ObjType_t::OT_GENERAL) && key.isNull())
                        check(StatsRecorder::getSceneStat(StatType_t(stat)) == expected, "scene null-key value changed");
                }
    }
    check(Access::buffers() == before, "read methods inserted or changed map entries");
    check(Access::summaries() == summaries, "read changed summary arrays");
    Access::reset();
    for (unsigned buffer = 0; buffer < 2; ++buffer)
    {
        Access::readBuffer(buffer);
        for (unsigned stat = 0; stat < unsigned(StatType_t::STATS_COUNT); ++stat)
            check(StatsRecorder::getSceneStat(StatType_t(stat)) == 0, "reset scene must read zero");
    }
    check(Access::nodes() == 0, "reading reset scene created a node");
}
void allocations()
{
    Access::reset();
    std::vector<LLUUID> keys;
    for (unsigned n = 0; n < 128; ++n) keys.push_back(id(10 + n));
    U64 total = 0;
    FSSamplingAlloc::Stats observed;
    {
        FSSamplingAlloc::Scope scope;
        for (unsigned buffer = 0; buffer < 2; ++buffer)
        {
            Access::readBuffer(buffer);
            for (unsigned type = 0; type < unsigned(ObjType_t::OT_COUNT); ++type)
                for (const auto& key : keys)
                    for (unsigned n = 0; n < 3; ++n)
                        total += StatsRecorder::get(ObjType_t(type), key, StatType_t(n));
            total += StatsRecorder::getSceneStat(StatType_t::RENDER_FRAME);
        }
        observed = FSSamplingAlloc::snapshot();
    }
    check(total == 0, "cold absent values changed");
#if FS_SAMPLING_ALLOCATION_PROBE
    check(observed.calls == 0 && observed.requested == 0, "cold reads allocated");
#endif
    check(Access::nodes() == 0, "cold reads populated missing keys");
    std::cout << "cold_reads=1538,new_calls=" << observed.calls << ",bytes=" << observed.requested
              << ",nodes=" << Access::nodes() << '\n';

#if FS_SAMPLING_ALLOCATION_PROBE
    Access::reset(); bool readThrew = false;
    const auto missingKey = id(1000), writtenKey = id(1001);
    FSSamplingAlloc::failureCountdown.store(0);
    try { check(StatsRecorder::get(ObjType_t::OT_AVATAR, missingKey, StatType_t::RENDER_IDLE) == 0,
                "fault-injected missing read value changed"); }
    catch (const std::bad_alloc&) { readThrew = true; }
    FSSamplingAlloc::failureCountdown.store(-1);
    check(!readThrew, "missing read failed under allocation pressure");
    bool writeThrew = false;
    FSSamplingAlloc::failureCountdown.store(0);
    try { Access::add(0, ObjType_t::OT_AVATAR, writtenKey, StatType_t::RENDER_IDLE, 77); }
    catch (const std::bad_alloc&) { writeThrew = true; }
    FSSamplingAlloc::failureCountdown.store(-1);
    check(writeThrew, "failure injection did not cover actual producer insertion");
#endif
    Access::add(0, ObjType_t::OT_AVATAR, id(1001), StatType_t::RENDER_IDLE, 77);
    Access::readBuffer(0);
    check(StatsRecorder::get(ObjType_t::OT_AVATAR, id(1001), StatType_t::RENDER_IDLE) == 77,
          "later producer update did not publish");
    Access::reset();
}
}
int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--describe")
    {
        std::cout << "Actual performance-stat read values, map effects and optional replacement-new/failure checks\n";
        return 0;
    }
    if (argc != 1) return 2;
    values(); allocations();
    std::cout << checks << " stats read checks; " << failures << " failures\n";
    return failures ? 1 : 0;
}
