#include <gtest/gtest.h>

#include "cpu/ooo/ooo_recovery.h"
#include "cpu/ooo/register_rename.h"
#include "cpu/ooo/reorder_buffer.h"
#include "cpu/ooo/reservation_station.h"
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

FetchedInstruction makeFetched(uint64_t pc) {
    FetchedInstruction fetched;
    fetched.pc = pc;
    fetched.instruction = 0x13;
    fetched.predicted_next_pc = pc + 4;
    return fetched;
}

} // namespace

class OooRecoveryTest : public ::testing::Test {
protected:
    CPUState state;

    void SetUp() override {
        state.reorder_buffer = std::make_unique<ReorderBuffer>();
        state.register_rename = std::make_unique<RegisterRenameUnit>();
        state.reservation_station = std::make_unique<ReservationStation>();
        state.store_buffer = std::make_unique<StoreBuffer>();
    }
};

TEST_F(OooRecoveryTest, FullPipelineRecoveryClearsSpeculativeStructuresAndRecordsCounters) {
    auto older = state.reorder_buffer->allocate_entry(makeAddiInstruction(1, 0, 1), 0x100, 1);
    auto younger = state.reorder_buffer->allocate_entry(makeAddiInstruction(2, 0, 2), 0x104, 2);
    ASSERT_NE(older, nullptr);
    ASSERT_NE(younger, nullptr);
    state.fetch_buffer.push(makeFetched(0x108));
    state.fetch_buffer.push(makeFetched(0x10c));
    state.cdb_queue.push(CommonDataBusEntry(younger));
    state.rename_checkpoints.emplace(1, state.register_rename->capture_checkpoint());
    state.reservation_valid = true;
    state.reservation_addr = 0x2000;
    state.alu_units[0].busy = true;
    state.alu_units[0].instruction = younger;

    OooRecovery::FullPipelineRequest request;
    request.reason = OooRecovery::Reason::Trap;
    request.has_restart_pc = true;
    request.restart_pc = 0x200;
    request.clear_reservation = true;
    request.reset_execution_units = true;
    const auto result = OooRecovery::recoverFullPipeline(state, request);

    EXPECT_EQ(state.pc, 0x200u);
    EXPECT_EQ(result.flushed_rob_entries, 2u);
    EXPECT_EQ(result.fetch_buffer_dropped, 2u);
    EXPECT_EQ(result.flushed_cdb_entries, 1u);
    EXPECT_TRUE(state.reorder_buffer->is_empty());
    EXPECT_TRUE(state.fetch_buffer.empty());
    EXPECT_TRUE(state.cdb_queue.empty());
    EXPECT_TRUE(state.rename_checkpoints.empty());
    EXPECT_FALSE(state.reservation_valid);
    EXPECT_EQ(state.reservation_addr, 0u);
    EXPECT_FALSE(state.alu_units[0].busy);
    EXPECT_EQ(state.alu_units[0].instruction, nullptr);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::PIPELINE_FLUSHES), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::PIPELINE_FLUSH_TRAP), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::ROB_FLUSHED_ENTRIES), 2u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::ROB_FLUSHED_ENTRIES_TRAP), 2u);
}

TEST_F(OooRecoveryTest, FullPipelineRecoveryCanPreserveReservationAndExecutionUnits) {
    auto inst = state.reorder_buffer->allocate_entry(makeAddiInstruction(1, 0, 1), 0x100, 1);
    ASSERT_NE(inst, nullptr);
    state.reservation_valid = true;
    state.reservation_addr = 0x2000;
    state.alu_units[0].busy = true;
    state.alu_units[0].instruction = inst;

    OooRecovery::FullPipelineRequest request;
    request.reason = OooRecovery::Reason::Exception;
    request.clear_reservation = false;
    request.reset_execution_units = false;
    OooRecovery::recoverFullPipeline(state, request);

    EXPECT_TRUE(state.reservation_valid);
    EXPECT_EQ(state.reservation_addr, 0x2000u);
    EXPECT_TRUE(state.alu_units[0].busy);
    EXPECT_EQ(state.alu_units[0].instruction, inst);
    EXPECT_TRUE(state.reorder_buffer->is_empty());
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::PIPELINE_FLUSH_EXCEPTION), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::ROB_FLUSHED_ENTRIES_EXCEPTION), 1u);
}

TEST_F(OooRecoveryTest, YoungerThanRecoveryFlushesOnlyYoungerWork) {
    auto older = state.reorder_buffer->allocate_entry(makeAddiInstruction(1, 0, 1), 0x100, 1);
    auto current = state.reorder_buffer->allocate_entry(makeAddiInstruction(2, 0, 2), 0x104, 2);
    auto younger = state.reorder_buffer->allocate_entry(makeAddiInstruction(3, 0, 3), 0x108, 3);
    ASSERT_NE(older, nullptr);
    ASSERT_NE(current, nullptr);
    ASSERT_NE(younger, nullptr);

    state.fetch_buffer.push(makeFetched(0x10c));
    state.cdb_queue.push(CommonDataBusEntry(older));
    state.cdb_queue.push(CommonDataBusEntry(younger));
    state.branch_units[0].busy = true;
    state.branch_units[0].instruction = current;
    state.alu_units[0].busy = true;
    state.alu_units[0].instruction = younger;
    state.memory_access_inflight[0].valid = true;
    state.memory_access_inflight[0].state.instruction = younger;
    state.memory_access_inflight[0].unit_type = ExecutionUnitType::LOAD;
    state.rename_checkpoints.emplace(2, state.register_rename->capture_checkpoint());
    state.rename_checkpoints.emplace(3, state.register_rename->capture_checkpoint());

    const auto checkpoint = state.register_rename->capture_checkpoint();
    OooRecovery::YoungerThanRequest request;
    request.instruction_id = current->get_instruction_id();
    request.rob_entry = current->get_rob_entry();
    request.current_unit_type = ExecutionUnitType::BRANCH;
    request.current_unit_index = 0;
    request.has_redirect_pc = true;
    request.redirect_pc = 0x200;
    request.rename_checkpoint = &checkpoint;

    const auto result = OooRecovery::recoverYoungerThan(state, request);

    EXPECT_EQ(state.pc, 0x200u);
    EXPECT_EQ(result.flushed_rob_entries, 1u);
    EXPECT_EQ(result.fetch_buffer_dropped, 1u);
    EXPECT_EQ(result.flushed_cdb_entries, 1u);
    EXPECT_TRUE(result.flushed_l1d_inflight);
    EXPECT_NE(state.reorder_buffer->get_entry(older->get_rob_entry()), nullptr);
    EXPECT_NE(state.reorder_buffer->get_entry(current->get_rob_entry()), nullptr);
    EXPECT_EQ(state.reorder_buffer->get_entry(younger->get_rob_entry()), nullptr);
    ASSERT_FALSE(state.cdb_queue.empty());
    EXPECT_EQ(state.cdb_queue.front().instruction, older);
    state.cdb_queue.pop();
    EXPECT_TRUE(state.cdb_queue.empty());
    EXPECT_TRUE(state.fetch_buffer.empty());
    EXPECT_TRUE(state.branch_units[0].busy);
    EXPECT_EQ(state.branch_units[0].instruction, current);
    EXPECT_FALSE(state.alu_units[0].busy);
    EXPECT_EQ(state.alu_units[0].instruction, nullptr);
    EXPECT_FALSE(state.memory_access_inflight[0].valid);
    EXPECT_TRUE(state.rename_checkpoints.empty());
}

} // namespace riscv
