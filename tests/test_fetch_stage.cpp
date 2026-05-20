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
    for (size_t i = 0; i < OOOPipelineConfig::FETCH_WIDTH; ++i) {
        memory->writeWord(static_cast<Address>(i * 4),
                          createITypeInstruction(static_cast<int16_t>(i + 1),
                                                 0,
                                                 0x0,
                                                 static_cast<uint8_t>(i + 1),
                                                 0x13));
    }

    FetchStage::Context context(state);
    fetch_stage.execute(context);

    ASSERT_EQ(state.fetch_buffer.size(), OOOPipelineConfig::FETCH_WIDTH);

    for (size_t i = 0; i < OOOPipelineConfig::FETCH_WIDTH; ++i) {
        const auto fetched = state.fetch_buffer.front();
        state.fetch_buffer.pop();

        EXPECT_EQ(fetched.pc, static_cast<Address>(i * 4));
        EXPECT_EQ(fetched.predicted_next_pc, static_cast<Address>((i + 1) * 4));
        EXPECT_FALSE(fetched.is_compressed);
    }

    EXPECT_EQ(state.pc, static_cast<Address>(OOOPipelineConfig::FETCH_WIDTH * 4));
    EXPECT_FALSE(state.halted);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::FETCHED_INSTRUCTIONS),
              OOOPipelineConfig::FETCH_WIDTH);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::FETCH_UTILIZED_SLOTS),
              OOOPipelineConfig::FETCH_WIDTH);
}

TEST_F(FetchStageContextTest, HaltsWhenZeroInstructionAndPipelineDrained) {
    FetchStage::Context context(state);
    fetch_stage.execute(context);

    EXPECT_TRUE(state.halted);
    EXPECT_TRUE(state.fetch_buffer.empty());
    EXPECT_EQ(state.pc, 0x0u);
}

} // namespace riscv
