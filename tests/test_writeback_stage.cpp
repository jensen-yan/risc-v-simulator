#include <gtest/gtest.h>

#include "cpu/ooo/register_rename.h"
#include "cpu/ooo/reorder_buffer.h"
#include "cpu/ooo/reservation_station.h"
#include "cpu/ooo/stages/writeback_stage.h"
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

class WritebackStageContextTest : public ::testing::Test {
protected:
    CPUState state;
    WritebackStage writeback_stage;

    void SetUp() override {
        state.reorder_buffer = std::make_unique<ReorderBuffer>();
        state.register_rename = std::make_unique<RegisterRenameUnit>();
        state.reservation_station = std::make_unique<ReservationStation>();
        state.store_buffer = std::make_unique<StoreBuffer>();
    }
};

TEST_F(WritebackStageContextTest, EmptyCdbSkipsWritebackThroughNarrowContext) {
    WritebackStage::Context context(state);
    writeback_stage.execute(context);

    EXPECT_TRUE(state.cdb_queue.empty());
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::WRITEBACKS), 0u);
}

TEST_F(WritebackStageContextTest, CompletesMatchingRobEntryThroughNarrowContext) {
    auto inst = state.reorder_buffer->allocate_entry(makeAddiInstruction(1, 0, 7), 0x100, 1);
    ASSERT_NE(inst, nullptr);
    inst->set_physical_dest_kind(RegisterFileKind::Integer);
    inst->set_physical_dest(32);
    inst->set_result(7);
    state.cdb_queue.push(CommonDataBusEntry(inst));

    WritebackStage::Context context(state);
    writeback_stage.execute(context);

    EXPECT_TRUE(state.cdb_queue.empty());
    EXPECT_EQ(inst->get_status(), DynamicInst::Status::COMPLETED);
    EXPECT_TRUE(inst->is_result_ready());
    EXPECT_EQ(state.register_rename->get_physical_register_value(RegisterFileKind::Integer, 32), 7u);
    EXPECT_TRUE(state.register_rename->is_physical_register_ready(RegisterFileKind::Integer, 32));
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::WRITEBACKS), 1u);
}

} // namespace riscv
