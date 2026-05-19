#include <gtest/gtest.h>

#include "cpu/ooo/reorder_buffer.h"
#include "cpu/ooo/stages/fetch_stage.h"
#include "core/memory.h"
#include "system/address_translation.h"
#include "system/privilege_state.h"

#include <memory>

namespace riscv {

class FetchStageContextTest : public ::testing::Test {
protected:
    std::shared_ptr<Memory> memory;
    CPUState state;
    FetchStage fetch_stage;

    void SetUp() override {
        memory = std::make_shared<Memory>(8192);
        state.memory = memory;
        state.privilege_state = std::make_unique<PrivilegeState>();
        state.address_translation = std::make_unique<AddressTranslation>(
            memory, state.privilege_state.get());
        state.reorder_buffer = std::make_unique<ReorderBuffer>();
        state.pc = 0;
        state.halted = false;
    }

    static uint32_t createITypeInstruction(int16_t imm,
                                           uint8_t rs1,
                                           uint8_t funct3,
                                           uint8_t rd,
                                           uint8_t opcode) {
        return (static_cast<uint32_t>(static_cast<uint16_t>(imm) & 0x0FFFu) << 20) |
               (static_cast<uint32_t>(rs1) << 15) |
               (static_cast<uint32_t>(funct3) << 12) |
               (static_cast<uint32_t>(rd) << 7) |
               opcode;
    }
};

TEST_F(FetchStageContextTest, FetchesSequentialInstructionsThroughNarrowContext) {
    memory->writeWord(0x0, createITypeInstruction(1, 0, 0x0, 1, 0x13));
    memory->writeWord(0x4, createITypeInstruction(2, 0, 0x0, 2, 0x13));

    FetchStage::Context context(state);
    fetch_stage.execute(context);

    ASSERT_EQ(state.fetch_buffer.size(), 2u);

    const auto first = state.fetch_buffer.front();
    state.fetch_buffer.pop();
    const auto second = state.fetch_buffer.front();

    EXPECT_EQ(first.pc, 0x0u);
    EXPECT_EQ(first.predicted_next_pc, 0x4u);
    EXPECT_FALSE(first.is_compressed);

    EXPECT_EQ(second.pc, 0x4u);
    EXPECT_EQ(second.predicted_next_pc, 0x8u);
    EXPECT_FALSE(second.is_compressed);

    EXPECT_EQ(state.pc, 0x8u);
    EXPECT_FALSE(state.halted);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::FETCHED_INSTRUCTIONS), 2u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::FETCH_UTILIZED_SLOTS), 2u);
}

TEST_F(FetchStageContextTest, HaltsWhenZeroInstructionAndPipelineDrained) {
    FetchStage::Context context(state);
    fetch_stage.execute(context);

    EXPECT_TRUE(state.halted);
    EXPECT_TRUE(state.fetch_buffer.empty());
    EXPECT_EQ(state.pc, 0x0u);
}

} // namespace riscv
