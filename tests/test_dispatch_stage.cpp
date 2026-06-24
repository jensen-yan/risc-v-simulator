#include <gtest/gtest.h>

#include "cpu/ooo/register_rename.h"
#include "cpu/ooo/reorder_buffer.h"
#include "cpu/ooo/reservation_station.h"
#include "cpu/ooo/stages/dispatch_stage.h"
#include "cpu/ooo/store_forwarding_buffer.h"

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

DecodedInstruction makeFenceInstruction() {
    DecodedInstruction decoded;
    decoded.type = InstructionType::I_TYPE;
    decoded.opcode = Opcode::MISC_MEM;
    decoded.funct3 = static_cast<Funct3>(0b000);
    decoded.execution_cycles = 1;
    return decoded;
}

} // namespace

class DispatchStageContextTest : public ::testing::Test {
protected:
    CPUState state;
    DispatchStage dispatch_stage;

    void SetUp() override {
        state.reorder_buffer = std::make_unique<ReorderBuffer>();
        state.register_rename = std::make_unique<RegisterRenameUnit>();
        state.reservation_station = std::make_unique<ReservationStation>();
        state.load_queue = std::make_unique<LoadQueue>();
        state.store_queue = std::make_unique<StoreQueue>();
        state.store_forwarding_buffer = std::make_unique<StoreForwardingBuffer>();
        state.cycle_count = 11;
    }
};

TEST_F(DispatchStageContextTest, DispatchesAllocatedRobEntryThroughNarrowContext) {
    auto inst = state.reorder_buffer->allocate_entry(makeAddiInstruction(1, 0, 7), 0x100, 1);
    ASSERT_NE(inst, nullptr);
    ASSERT_EQ(inst->get_status(), DynamicInst::Status::ALLOCATED);

    DispatchStage::Context context(state);
    dispatch_stage.execute(context);

    EXPECT_EQ(inst->get_status(), DynamicInst::Status::DISPATCHED);
    EXPECT_EQ(inst->get_dispatch_cycle(), 11u);
    EXPECT_EQ(state.reservation_station->get_occupied_entry_count(), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::DISPATCH_SLOTS),
              OOOPipelineConfig::DISPATCH_WIDTH);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::DISPATCHED_INSTRUCTIONS), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::DISPATCH_UTILIZED_SLOTS), 1u);
}

TEST_F(DispatchStageContextTest, EmptyRobSkipsDispatchThroughNarrowContext) {
    DispatchStage::Context context(state);
    dispatch_stage.execute(context);

    EXPECT_EQ(state.reservation_station->get_occupied_entry_count(), 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::DISPATCH_SLOTS),
              OOOPipelineConfig::DISPATCH_WIDTH);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::DISPATCH_UTILIZED_SLOTS), 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::STALL_DISPATCH_NO_DISPATCHABLE), 0u);
}

TEST_F(DispatchStageContextTest, FenceDispatchesAloneAndBlocksYoungerWork) {
    auto fence = state.reorder_buffer->allocate_entry(makeFenceInstruction(), 0x100, 1);
    auto younger = state.reorder_buffer->allocate_entry(makeAddiInstruction(1, 0, 7), 0x104, 2);
    ASSERT_NE(fence, nullptr);
    ASSERT_NE(younger, nullptr);

    DispatchStage::Context context(state);
    dispatch_stage.execute(context);

    EXPECT_EQ(fence->get_status(), DynamicInst::Status::DISPATCHED);
    EXPECT_EQ(younger->get_status(), DynamicInst::Status::ALLOCATED);
    EXPECT_EQ(state.reservation_station->get_occupied_entry_count(), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::DISPATCHED_INSTRUCTIONS), 1u);
}

} // namespace riscv
