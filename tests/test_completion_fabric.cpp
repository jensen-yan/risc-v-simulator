#include <gtest/gtest.h>

#include "cpu/ooo/completion_fabric.h"
#include "cpu/ooo/dynamic_inst.h"

namespace riscv {

namespace {

DecodedInstruction makeAddiInstruction() {
    DecodedInstruction decoded;
    decoded.type = InstructionType::I_TYPE;
    decoded.opcode = Opcode::OP_IMM;
    decoded.rd = 1;
    decoded.rs1 = 0;
    decoded.rs2 = 0;
    decoded.imm = 1;
    decoded.execution_cycles = 1;
    return decoded;
}

DynamicInstPtr makeInst(uint64_t instruction_id) {
    return std::make_shared<DynamicInst>(
        makeAddiInstruction(), 0x100 + instruction_id * 4, instruction_id);
}

} // namespace

TEST(CompletionFabricTest, LimitsAcceptedEventsPerCycle) {
    CompletionFabric fabric(/*completion_width=*/2);

    auto first = makeInst(1);
    auto second = makeInst(2);
    auto third = makeInst(3);

    EXPECT_TRUE(fabric.trySubmit(CompletionEvent(first)));
    EXPECT_TRUE(fabric.trySubmit(CompletionEvent(second)));
    EXPECT_FALSE(fabric.trySubmit(CompletionEvent(third)));

    EXPECT_EQ(fabric.size(), 2u);
    EXPECT_EQ(fabric.usedCompletionSlots(), 2u);
    EXPECT_EQ(fabric.availableCompletionSlots(), 0u);

    EXPECT_EQ(fabric.popReadyEvent().instruction, first);
    EXPECT_EQ(fabric.popReadyEvent().instruction, second);
    EXPECT_TRUE(fabric.empty());
}

TEST(CompletionFabricTest, BeginCycleRestoresCompletionSlotsWithoutDroppingEvents) {
    CompletionFabric fabric(/*completion_width=*/1);
    auto first = makeInst(1);
    auto second = makeInst(2);

    EXPECT_TRUE(fabric.trySubmit(CompletionEvent(first)));
    EXPECT_FALSE(fabric.trySubmit(CompletionEvent(second)));

    fabric.beginCycle();

    EXPECT_TRUE(fabric.trySubmit(CompletionEvent(second)));
    EXPECT_EQ(fabric.size(), 2u);
    EXPECT_EQ(fabric.popReadyEvent().instruction, first);
    EXPECT_EQ(fabric.popReadyEvent().instruction, second);
}

TEST(CompletionFabricTest, FlushYoungerThanKeepsOlderEvents) {
    CompletionFabric fabric(/*completion_width=*/4);
    auto older = makeInst(1);
    auto current = makeInst(2);
    auto younger = makeInst(3);

    ASSERT_TRUE(fabric.trySubmit(CompletionEvent(older)));
    ASSERT_TRUE(fabric.trySubmit(CompletionEvent(current)));
    ASSERT_TRUE(fabric.trySubmit(CompletionEvent(younger)));

    EXPECT_EQ(fabric.flushYoungerThan(current->get_instruction_id()), 1u);

    ASSERT_FALSE(fabric.empty());
    EXPECT_EQ(fabric.popReadyEvent().instruction, older);
    ASSERT_FALSE(fabric.empty());
    EXPECT_EQ(fabric.popReadyEvent().instruction, current);
    EXPECT_TRUE(fabric.empty());
}

} // namespace riscv
