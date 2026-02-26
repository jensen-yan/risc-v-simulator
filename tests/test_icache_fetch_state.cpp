#include <gtest/gtest.h>

#include "cpu/ooo/cpu_state.h"

namespace riscv {

TEST(ICacheFetchStateTest, MissWaitCycleAdvancesAndResumes) {
    ICacheFetchState state;

    state.startMissWait(/*pc=*/0x100, /*instruction=*/0x00000013, /*latency_cycles=*/4);
    EXPECT_TRUE(state.hasMissWait());
    EXPECT_EQ(state.remainingWaitCycles(), 3);

    EXPECT_TRUE(state.advanceMissWaitCycle());
    EXPECT_EQ(state.remainingWaitCycles(), 2);

    EXPECT_TRUE(state.advanceMissWaitCycle());
    EXPECT_EQ(state.remainingWaitCycles(), 1);

    EXPECT_FALSE(state.advanceMissWaitCycle());
    EXPECT_EQ(state.remainingWaitCycles(), 0);
    EXPECT_FALSE(state.hasMissWait());
}

TEST(ICacheFetchStateTest, PendingInstructionConsumeResetsState) {
    ICacheFetchState state;
    state.startMissWait(/*pc=*/0x80, /*instruction=*/0xABCD0013, /*latency_cycles=*/6);

    Instruction instruction = 0;
    EXPECT_FALSE(state.consumeIfMatch(/*pc=*/0x84, instruction));
    EXPECT_TRUE(state.hasPendingFor(/*pc=*/0x80));

    EXPECT_TRUE(state.consumeIfMatch(/*pc=*/0x80, instruction));
    EXPECT_EQ(instruction, 0xABCD0013u);
    EXPECT_FALSE(state.hasPendingFor(/*pc=*/0x80));
    EXPECT_FALSE(state.hasMissWait());
    EXPECT_EQ(state.remainingWaitCycles(), 0);
}

TEST(ICacheFetchStateTest, LatencyOneMissDoesNotIntroduceWaitCycles) {
    ICacheFetchState state;

    state.startMissWait(/*pc=*/0x40, /*instruction=*/0x00000013, /*latency_cycles=*/1);
    EXPECT_FALSE(state.hasMissWait());
    EXPECT_EQ(state.remainingWaitCycles(), 0);

    Instruction instruction = 0;
    EXPECT_TRUE(state.consumeIfMatch(/*pc=*/0x40, instruction));
    EXPECT_EQ(instruction, 0x00000013u);
}

} // namespace riscv
