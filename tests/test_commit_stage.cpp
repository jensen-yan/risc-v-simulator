#include <gtest/gtest.h>

#include "cpu/ooo/register_rename.h"
#include "cpu/ooo/reorder_buffer.h"
#include "cpu/ooo/stages/commit_stage.h"
#include "cpu/ooo/store_buffer.h"

#include <memory>

namespace riscv {

namespace {

DecodedInstruction makeAddiInstruction(RegNum rd, RegNum rs1, int32_t imm) {
    DecodedInstruction decoded;
    decoded.type = InstructionType::I_TYPE;
    decoded.opcode = Opcode::OP_IMM;
    decoded.rd = rd;
    decoded.rs1 = rs1;
    decoded.rs2 = 0;
    decoded.imm = imm;
    decoded.execution_cycles = 1;
    return decoded;
}

} // namespace

class CommitStageContextTest : public ::testing::Test {
protected:
    CPUState state;
    CommitStage commit_stage;

    void SetUp() override {
        state.reorder_buffer = std::make_unique<ReorderBuffer>();
        state.register_rename = std::make_unique<RegisterRenameUnit>();
        state.store_buffer = std::make_unique<StoreBuffer>();
        state.cycle_count = 17;
    }
};

TEST_F(CommitStageContextTest, EmptyRobSkipsCommitThroughNarrowContext) {
    CommitStage::Context context(state);
    commit_stage.execute(context);

    EXPECT_EQ(state.instruction_count, 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::COMMIT_SLOTS), 2u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::COMMIT_UTILIZED_SLOTS), 0u);
}

TEST_F(CommitStageContextTest, CommitsCompletedIntegerInstructionThroughNarrowContext) {
    auto inst = state.reorder_buffer->allocate_entry(makeAddiInstruction(1, 0, 42), 0x100, 1);
    ASSERT_NE(inst, nullptr);
    inst->set_physical_dest_kind(RegisterFileKind::Integer);
    inst->set_physical_dest(32);
    state.reorder_buffer->update_entry(inst, 42);

    CommitStage::Context context(state);
    commit_stage.execute(context);

    EXPECT_EQ(inst->get_status(), DynamicInst::Status::RETIRED);
    EXPECT_EQ(inst->get_retire_cycle(), 17u);
    EXPECT_EQ(state.arch_registers[1], 42u);
    EXPECT_EQ(state.instruction_count, 1u);
    EXPECT_TRUE(state.reorder_buffer->is_empty());
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::COMMIT_SLOTS), 2u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::INSTRUCTIONS_RETIRED), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::COMMIT_UTILIZED_SLOTS), 1u);
}

} // namespace riscv
