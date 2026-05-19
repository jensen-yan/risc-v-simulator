#include <gtest/gtest.h>

#include "cpu/ooo/reorder_buffer.h"
#include "cpu/ooo/stages/decode_stage.h"

#include <memory>

namespace riscv {

class DecodeStageContextTest : public ::testing::Test {
protected:
    CPUState state;
    DecodeStage decode_stage;

    void SetUp() override {
        state.reorder_buffer = std::make_unique<ReorderBuffer>();
        state.cycle_count = 42;
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

    void pushFetched(uint64_t pc, Instruction instruction, uint64_t fetch_cycle) {
        FetchedInstruction fetched;
        fetched.pc = pc;
        fetched.instruction = instruction;
        fetched.predicted_next_pc = pc + 4;
        fetched.fetch_cycle = fetch_cycle;
        state.fetch_buffer.push(fetched);
    }
};

TEST_F(DecodeStageContextTest, DecodesFetchedInstructionsThroughNarrowContext) {
    pushFetched(0x0, createITypeInstruction(1, 0, 0x0, 1, 0x13), 7);
    pushFetched(0x4, createITypeInstruction(2, 0, 0x0, 2, 0x13), 8);

    DecodeStage::Context context(state);
    decode_stage.execute(context);

    EXPECT_TRUE(state.fetch_buffer.empty());
    EXPECT_EQ(state.global_instruction_id, 2u);
    EXPECT_EQ(state.reorder_buffer->get_free_entry_count(),
              static_cast<size_t>(ReorderBuffer::MAX_ROB_ENTRIES - 2));

    const auto first = state.reorder_buffer->get_entry(0);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->get_instruction_id(), 1u);
    EXPECT_EQ(first->get_pc(), 0x0u);
    EXPECT_EQ(first->get_decoded_info().opcode, Opcode::OP_IMM);
    EXPECT_EQ(first->get_decoded_info().rd, 1u);
    EXPECT_EQ(first->get_decoded_info().imm, 1);
    EXPECT_EQ(first->get_fetch_cycle(), 7u);
    EXPECT_EQ(first->get_decode_cycle(), 42u);
    EXPECT_TRUE(first->has_predicted_next_pc());
    EXPECT_EQ(first->get_predicted_next_pc(), 0x4u);

    const auto second = state.reorder_buffer->get_entry(1);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->get_instruction_id(), 2u);
    EXPECT_EQ(second->get_pc(), 0x4u);
    EXPECT_EQ(second->get_decoded_info().rd, 2u);
    EXPECT_EQ(second->get_decoded_info().imm, 2);
    EXPECT_EQ(second->get_fetch_cycle(), 8u);
    EXPECT_EQ(second->get_decode_cycle(), 42u);

    EXPECT_EQ(state.perf_counters.value(PerfCounterId::DECODE_SLOTS), 2u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::DECODED_INSTRUCTIONS), 2u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::DECODE_UTILIZED_SLOTS), 2u);
}

TEST_F(DecodeStageContextTest, KeepsFetchBufferWhenRobIsFull) {
    DecodedInstruction decoded;
    decoded.opcode = Opcode::OP_IMM;
    for (int i = 0; i < ReorderBuffer::MAX_ROB_ENTRIES; ++i) {
        ASSERT_NE(state.reorder_buffer->allocate_entry(decoded, static_cast<uint64_t>(i * 4), i + 1),
                  nullptr);
    }

    pushFetched(0x100, createITypeInstruction(3, 0, 0x0, 3, 0x13), 12);

    DecodeStage::Context context(state);
    decode_stage.execute(context);

    EXPECT_EQ(state.fetch_buffer.size(), 1u);
    EXPECT_EQ(state.global_instruction_id, 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::DECODE_SLOTS), 2u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::DECODE_UTILIZED_SLOTS), 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::STALL_DECODE_ROB_FULL), 1u);
}

} // namespace riscv
