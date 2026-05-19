#include <gtest/gtest.h>

#include "cpu/ooo/register_rename.h"
#include "cpu/ooo/reorder_buffer.h"
#include "cpu/ooo/reservation_station.h"
#include "cpu/ooo/stages/issue_stage.h"
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

class IssueStageContextTest : public ::testing::Test {
protected:
    CPUState state;
    IssueStage issue_stage;

    void SetUp() override {
        state.reorder_buffer = std::make_unique<ReorderBuffer>();
        state.register_rename = std::make_unique<RegisterRenameUnit>();
        state.reservation_station = std::make_unique<ReservationStation>();
        state.store_buffer = std::make_unique<StoreBuffer>();
        state.cycle_count = 11;
    }
};

TEST_F(IssueStageContextTest, IssuesAllocatedRobEntryThroughNarrowContext) {
    auto inst = state.reorder_buffer->allocate_entry(makeAddiInstruction(1, 0, 7), 0x100, 1);
    ASSERT_NE(inst, nullptr);
    ASSERT_EQ(inst->get_status(), DynamicInst::Status::ALLOCATED);

    IssueStage::Context context(state);
    issue_stage.execute(context);

    EXPECT_EQ(inst->get_status(), DynamicInst::Status::ISSUED);
    EXPECT_EQ(inst->get_issue_cycle(), 11u);
    EXPECT_EQ(state.reservation_station->get_occupied_entry_count(), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::ISSUE_SLOTS), 2u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::ISSUED_INSTRUCTIONS), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::ISSUE_UTILIZED_SLOTS), 1u);
}

TEST_F(IssueStageContextTest, EmptyRobSkipsIssueThroughNarrowContext) {
    IssueStage::Context context(state);
    issue_stage.execute(context);

    EXPECT_EQ(state.reservation_station->get_occupied_entry_count(), 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::ISSUE_SLOTS), 2u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::ISSUE_UTILIZED_SLOTS), 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::STALL_ISSUE_NO_DISPATCHABLE), 0u);
}

} // namespace riscv
