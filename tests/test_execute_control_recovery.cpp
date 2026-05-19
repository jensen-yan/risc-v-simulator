#include <gtest/gtest.h>

#include "cpu/ooo/execute_control_recovery.h"

namespace riscv {

namespace {

DecodedInstruction makeInstruction(Opcode opcode) {
    DecodedInstruction decoded;
    decoded.opcode = opcode;
    return decoded;
}

} // namespace

TEST(ExecuteControlRecoveryTest, IgnoresNonControlInstruction) {
    CPUState state;
    ExecutionUnit unit;
    unit.instruction = create_dynamic_inst(makeInstruction(Opcode::OP_IMM), 0x100, 1);
    ASSERT_NE(unit.instruction, nullptr);

    const bool recovered = ExecuteControlRecovery::tryRecoverEarly(
        unit, ExecutionUnitType::ALU, 0, state);

    EXPECT_FALSE(recovered);
    EXPECT_FALSE(unit.instruction->is_control_recovered_early());
}

TEST(ExecuteControlRecoveryTest, IgnoresCorrectControlPrediction) {
    CPUState state;
    ExecutionUnit unit;
    unit.instruction = create_dynamic_inst(makeInstruction(Opcode::BRANCH), 0x100, 1);
    ASSERT_NE(unit.instruction, nullptr);
    unit.instruction->set_jump_info(true, 0x140);
    unit.instruction->set_predicted_next_pc(0x140);

    const bool recovered = ExecuteControlRecovery::tryRecoverEarly(
        unit, ExecutionUnitType::BRANCH, 0, state);

    EXPECT_FALSE(recovered);
    EXPECT_FALSE(unit.instruction->is_control_recovered_early());
}

TEST(ExecuteControlRecoveryTest, MispredictWithoutRenameCheckpointDoesNotRecover) {
    CPUState state;
    ExecutionUnit unit;
    unit.instruction = create_dynamic_inst(makeInstruction(Opcode::BRANCH), 0x100, 7);
    ASSERT_NE(unit.instruction, nullptr);
    unit.instruction->set_jump_info(true, 0x180);
    unit.instruction->set_predicted_next_pc(0x104);

    const bool recovered = ExecuteControlRecovery::tryRecoverEarly(
        unit, ExecutionUnitType::BRANCH, 0, state);

    EXPECT_FALSE(recovered);
    EXPECT_FALSE(unit.instruction->is_control_recovered_early());
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::PIPELINE_FLUSHES), 0u);
}

} // namespace riscv
