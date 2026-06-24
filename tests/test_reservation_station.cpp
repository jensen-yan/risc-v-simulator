#include <gtest/gtest.h>

#include "cpu/ooo/dynamic_inst.h"
#include "cpu/ooo/reservation_station.h"

#include <vector>

namespace riscv {

namespace {

DecodedInstruction makeInstruction(InstructionType type = InstructionType::R_TYPE,
                                   Opcode opcode = Opcode::OP,
                                   RegNum rd = 1,
                                   RegNum rs1 = 2,
                                   RegNum rs2 = 3) {
    DecodedInstruction decoded;
    decoded.type = type;
    decoded.opcode = opcode;
    decoded.rd = rd;
    decoded.rs1 = rs1;
    decoded.rs2 = rs2;
    decoded.execution_cycles = 1;
    return decoded;
}

DynamicInstPtr makeDynamicInst(const DecodedInstruction& decoded,
                               uint64_t pc,
                               uint64_t instruction_id,
                               bool src1_ready = true,
                               bool src2_ready = true) {
    auto inst = create_dynamic_inst(decoded, pc, instruction_id);
    inst->set_physical_dest(34);
    inst->set_physical_src1(32);
    inst->set_physical_src2(33);
    inst->set_physical_dest_kind(RegisterFileKind::Integer);
    inst->set_physical_src1_kind(RegisterFileKind::Integer);
    inst->set_physical_src2_kind(RegisterFileKind::Integer);
    inst->set_rob_entry(static_cast<ROBEntry>(instruction_id));
    if (src1_ready) {
        inst->set_src1_ready(true, 0x1234);
    }
    if (src2_ready) {
        inst->set_src2_ready(true, 0x5678);
    }
    inst->set_src3_ready(true, 0);
    return inst;
}

} // namespace

TEST(ReservationStationTest, DispatchStoresInstructionAndAssignsEntry) {
    ReservationStation rs;
    auto inst = makeDynamicInst(makeInstruction(), 0x1000, 1);

    const auto result = rs.dispatch_instruction(inst);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(rs.get_occupied_entry_count(), 1u);
    EXPECT_EQ(inst->get_rs_entry(), result.rs_entry);
    EXPECT_EQ(inst->get_status(), DynamicInst::Status::DISPATCHED);
    EXPECT_EQ(rs.get_entry(result.rs_entry), inst);
}

TEST(ReservationStationTest, CapacityFailureReportsError) {
    ReservationStation rs;
    bool saw_failure = false;

    for (size_t i = 0; i < OOOPipelineConfig::RS_ENTRIES + 1; ++i) {
        auto inst = makeDynamicInst(
            makeInstruction(),
            0x1000 + i * 4,
            i + 1);
        const auto result = rs.dispatch_instruction(inst);
        if (!result.success) {
            saw_failure = true;
            EXPECT_FALSE(result.error_message.empty());
            break;
        }
    }

    EXPECT_TRUE(saw_failure);
    EXPECT_EQ(rs.get_free_entry_count(), 0u);
}

TEST(ReservationStationTest, UpdateOperandsMakesWaitingEntryReady) {
    ReservationStation rs;
    auto waiting = makeDynamicInst(makeInstruction(), 0x1000, 1, /*src1_ready=*/false);
    ASSERT_TRUE(rs.dispatch_instruction(waiting).success);
    EXPECT_TRUE(rs.ready_entries().empty());

    auto producer = makeDynamicInst(makeInstruction(), 0x1004, 99);
    producer->set_physical_dest(32);
    producer->set_physical_dest_kind(RegisterFileKind::Integer);
    producer->set_result(0xAABBCCDD);
    rs.update_operands(CompletionEvent(producer), nullptr);

    const auto ready = rs.ready_entries();
    ASSERT_EQ(ready.size(), 1u);
    EXPECT_EQ(ready[0].instruction, waiting);
    EXPECT_TRUE(waiting->is_src1_ready());
    EXPECT_EQ(waiting->get_src1_value(), 0xAABBCCDDu);
}

TEST(ReservationStationTest, ReadyEntriesReturnProgramOrderAndSkipExecuting) {
    ReservationStation rs;
    auto younger = makeDynamicInst(makeInstruction(), 0x1004, 2);
    auto older = makeDynamicInst(makeInstruction(), 0x1000, 1);
    auto executing = makeDynamicInst(makeInstruction(), 0x1008, 3);

    ASSERT_TRUE(rs.dispatch_instruction(younger).success);
    ASSERT_TRUE(rs.dispatch_instruction(older).success);
    ASSERT_TRUE(rs.dispatch_instruction(executing).success);
    executing->set_status(DynamicInst::Status::EXECUTING);

    const auto ready = rs.ready_entries();

    ASSERT_EQ(ready.size(), 2u);
    EXPECT_EQ(ready[0].instruction, older);
    EXPECT_EQ(ready[1].instruction, younger);
}

TEST(ReservationStationTest, FlushYoungerThanKeepsOlderEntries) {
    ReservationStation rs;
    auto older = makeDynamicInst(makeInstruction(), 0x1000, 1);
    auto younger = makeDynamicInst(makeInstruction(), 0x1004, 2);
    ASSERT_TRUE(rs.dispatch_instruction(older).success);
    ASSERT_TRUE(rs.dispatch_instruction(younger).success);

    rs.flush_younger_than(older->get_instruction_id());

    const auto ready = rs.ready_entries();
    ASSERT_EQ(ready.size(), 1u);
    EXPECT_EQ(ready[0].instruction, older);
}

TEST(ReservationStationTest, PipelineFlushClearsEntries) {
    ReservationStation rs;
    for (uint64_t i = 0; i < 5; ++i) {
        ASSERT_TRUE(rs.dispatch_instruction(
            makeDynamicInst(makeInstruction(), 0x1000 + i * 4, i + 1)).success);
    }

    rs.flush_pipeline();

    EXPECT_EQ(rs.get_occupied_entry_count(), 0u);
    EXPECT_EQ(rs.get_free_entry_count(), OOOPipelineConfig::RS_ENTRIES);
    EXPECT_TRUE(rs.ready_entries().empty());
}

TEST(ReservationStationTest, StatisticsTrackDispatchAndCapacityStallsOnly) {
    ReservationStation rs;
    uint64_t dispatched_before = 0;
    uint64_t stalls_before = 0;
    rs.get_statistics(dispatched_before, stalls_before);

    ASSERT_TRUE(rs.dispatch_instruction(makeDynamicInst(makeInstruction(), 0x1000, 1)).success);
    uint64_t dispatched_after = 0;
    uint64_t stalls_after = 0;
    rs.get_statistics(dispatched_after, stalls_after);

    EXPECT_EQ(dispatched_after, dispatched_before + 1);
    EXPECT_EQ(stalls_after, stalls_before);
}

} // namespace riscv
