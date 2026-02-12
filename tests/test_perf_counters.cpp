#include <gtest/gtest.h>

#include "cpu/ooo/perf_counters.h"

namespace riscv {

TEST(PerfCounterBankTest, InitializeAndIncrement) {
    PerfCounterBank counters;

    EXPECT_EQ(counters.value(PerfCounterId::CYCLES), 0U);
    EXPECT_EQ(counters.value(PerfCounterId::INSTRUCTIONS_RETIRED), 0U);

    counters.increment(PerfCounterId::CYCLES);
    counters.increment(PerfCounterId::INSTRUCTIONS_RETIRED, 3);

    EXPECT_EQ(counters.value(PerfCounterId::CYCLES), 1U);
    EXPECT_EQ(counters.value(PerfCounterId::INSTRUCTIONS_RETIRED), 3U);
}

TEST(PerfCounterBankTest, MetadataSizeMatchesCounterCount) {
    const auto& meta = PerfCounterBank::metadata();
    EXPECT_EQ(meta.size(), PerfCounterBank::NUM_COUNTERS);

    const auto& cycle_meta = PerfCounterBank::meta(PerfCounterId::CYCLES);
    EXPECT_STREQ(cycle_meta.name, "cpu.cycles");
    EXPECT_STRNE(cycle_meta.description, "");
}

} // namespace riscv
