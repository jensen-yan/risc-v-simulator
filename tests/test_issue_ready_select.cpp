#include <gtest/gtest.h>

#include "cpu/ooo/issue_ready_select.h"
#include "cpu/ooo/reorder_buffer.h"
#include "cpu/ooo/reservation_station.h"
#include "cpu/ooo/store_buffer.h"

#include <memory>

namespace riscv {

namespace {

DecodedInstruction makeAluInstruction(RegNum rd = 1) {
    DecodedInstruction decoded;
    decoded.type = InstructionType::R_TYPE;
    decoded.opcode = Opcode::OP;
    decoded.rd = rd;
    decoded.rs1 = 2;
    decoded.rs2 = 3;
    decoded.execution_cycles = 1;
    return decoded;
}

DecodedInstruction makeLoadInstruction(RegNum rd = 1) {
    DecodedInstruction decoded;
    decoded.type = InstructionType::I_TYPE;
    decoded.opcode = Opcode::LOAD;
    decoded.rd = rd;
    decoded.rs1 = 2;
    decoded.memory_access_size = 4;
    decoded.execution_cycles = 1;
    return decoded;
}

DecodedInstruction makeStoreInstruction() {
    DecodedInstruction decoded;
    decoded.type = InstructionType::S_TYPE;
    decoded.opcode = Opcode::STORE;
    decoded.rs1 = 2;
    decoded.rs2 = 3;
    decoded.memory_access_size = 4;
    decoded.execution_cycles = 1;
    return decoded;
}

DecodedInstruction makeAmoInstruction(RegNum rd = 1) {
    DecodedInstruction decoded;
    decoded.type = InstructionType::R_TYPE;
    decoded.opcode = Opcode::AMO;
    decoded.rd = rd;
    decoded.rs1 = 2;
    decoded.rs2 = 3;
    decoded.memory_access_size = 4;
    decoded.execution_cycles = 1;
    return decoded;
}

CPUState makeIssueState() {
    CPUState state;
    state.reorder_buffer = std::make_unique<ReorderBuffer>();
    state.reservation_station = std::make_unique<ReservationStation>();
    state.store_buffer = std::make_unique<StoreBuffer>();
    return state;
}

DynamicInstPtr allocateReady(CPUState& state,
                             const DecodedInstruction& decoded,
                             uint64_t pc,
                             uint64_t instruction_id) {
    auto inst = state.reorder_buffer->allocate_entry(decoded, pc, instruction_id);
    EXPECT_NE(inst, nullptr);
    inst->set_src1_ready(true, 0x10);
    inst->set_src2_ready(true, 0x20);
    inst->set_src3_ready(true, 0);
    const auto dispatch_result = state.reservation_station->dispatch_instruction(inst);
    EXPECT_TRUE(dispatch_result.success);
    return inst;
}

} // namespace

TEST(IssueReadySelectTest, SelectsReadyWorkUsingCpuExecutionUnitAvailability) {
    auto state = makeIssueState();
    state.alu_units[0].busy = true;
    auto first = allocateReady(state, makeAluInstruction(1), 0x100, 1);
    auto second = allocateReady(state, makeAluInstruction(2), 0x104, 2);

    const auto result = IssueReadySelect::select(state, 2);

    ASSERT_EQ(result.selected.size(), 2u);
    EXPECT_EQ(result.selected[0].instruction, first);
    EXPECT_EQ(result.selected[0].unit_type, ExecutionUnitType::ALU);
    EXPECT_EQ(result.selected[0].unit_index, 1u);
    EXPECT_EQ(result.selected[1].instruction, second);
    EXPECT_EQ(result.selected[1].unit_index, 2u);
    EXPECT_EQ(result.selected[0].unit, &state.alu_units[1]);
    EXPECT_TRUE(state.alu_units[1].busy);
    EXPECT_EQ(state.alu_units[1].instruction, first);
    EXPECT_TRUE(state.alu_units[2].busy);
    EXPECT_EQ(state.alu_units[2].instruction, second);
    EXPECT_EQ(first->get_status(), DynamicInst::Status::EXECUTING);
    EXPECT_EQ(second->get_status(), DynamicInst::Status::EXECUTING);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::ISSUE_UTILIZED_SLOTS), 2u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::TOPDOWN_SLOTS_EXECUTED), 2u);
}

TEST(IssueReadySelectTest, ResourceBlockedWhenReadyWorkHasNoFreeUnit) {
    auto state = makeIssueState();
    for (auto& unit : state.alu_units) {
        unit.busy = true;
    }
    auto inst = allocateReady(state, makeAluInstruction(), 0x100, 1);

    const auto result = IssueReadySelect::select(state, 4);

    EXPECT_TRUE(result.selected.empty());
    EXPECT_EQ(inst->get_status(), DynamicInst::Status::DISPATCHED);
    EXPECT_EQ(result.resource_blocked_slots, 4u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::STALL_EXECUTE_RESOURCE_BLOCKED), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::TOPDOWN_SLOTS_RESOURCE_BLOCKED), 4u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::TOPDOWN_SLOTS_NO_UNIT), 0u);
}

TEST(IssueReadySelectTest, AmoWaitConsumesSlotWithoutStartingExecution) {
    auto state = makeIssueState();
    ASSERT_NE(state.reorder_buffer->allocate_entry(makeStoreInstruction(), 0x100, 1), nullptr);
    auto amo = allocateReady(state, makeAmoInstruction(), 0x104, 2);

    const auto result = IssueReadySelect::select(state, 4);

    EXPECT_TRUE(result.selected.empty());
    EXPECT_EQ(result.amo_wait_slots, 1u);
    EXPECT_EQ(amo->get_status(), DynamicInst::Status::DISPATCHED);
    EXPECT_FALSE(state.alu_units[0].busy);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::STALL_EXECUTE_AMO_WAIT), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::TOPDOWN_SLOTS_AMO_WAIT), 1u);
}

TEST(IssueReadySelectTest, BlocksKnownBadAddrUnknownPairAndSelectsYoungerReadyWork) {
    auto state = makeIssueState();
    const uint64_t store_pc = 0x100;
    const uint64_t load_pc = 0x104;
    ASSERT_NE(state.reorder_buffer->allocate_entry(makeStoreInstruction(), store_pc, 1), nullptr);
    auto load = allocateReady(state, makeLoadInstruction(), load_pc, 2);
    auto alu = allocateReady(state, makeAluInstruction(), 0x108, 3);
    state.blocked_addr_unknown_pairs.insert(CPUState::makeAddrUnknownPairKey(load_pc, store_pc));

    const auto result = IssueReadySelect::select(state, 2);

    ASSERT_EQ(result.selected.size(), 1u);
    EXPECT_EQ(result.selected[0].instruction, alu);
    EXPECT_EQ(result.selected[0].unit_type, ExecutionUnitType::ALU);
    EXPECT_EQ(load->get_status(), DynamicInst::Status::DISPATCHED);
    EXPECT_TRUE(load->get_memory_info().blocked_by_addr_unknown_pair);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOADS_BLOCKED_ADDR_UNKNOWN_PAIR), 1u);
}

TEST(IssueReadySelectTest, BusyLoadUnitsDoNotRecordBadAddrUnknownPairBlock) {
    auto state = makeIssueState();
    const uint64_t store_pc = 0x100;
    const uint64_t load_pc = 0x104;
    ASSERT_NE(state.reorder_buffer->allocate_entry(makeStoreInstruction(), store_pc, 1), nullptr);
    auto load = allocateReady(state, makeLoadInstruction(), load_pc, 2);
    for (auto& unit : state.load_units) {
        unit.busy = true;
    }
    state.blocked_addr_unknown_pairs.insert(CPUState::makeAddrUnknownPairKey(load_pc, store_pc));

    const auto result = IssueReadySelect::select(state, 1);

    EXPECT_TRUE(result.selected.empty());
    EXPECT_EQ(result.resource_blocked_slots, 1u);
    EXPECT_FALSE(load->get_memory_info().blocked_by_addr_unknown_pair);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOADS_BLOCKED_ADDR_UNKNOWN_PAIR), 0u);
}

TEST(IssueReadySelectTest, PartialIssueDoesNotLetInflightMissOverrideEmptySlotAttribution) {
    auto state = makeIssueState();
    auto inst = allocateReady(state, makeAluInstruction(), 0x100, 1);
    state.memory_access_inflight[0].valid = true;

    const auto result = IssueReadySelect::select(state, 2);

    ASSERT_EQ(result.selected.size(), 1u);
    EXPECT_EQ(result.selected[0].instruction, inst);
    EXPECT_EQ(result.resource_blocked_slots, 0u);
    EXPECT_EQ(result.dependency_blocked_slots, 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::TOPDOWN_SLOTS_RESOURCE_BLOCKED), 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::TOPDOWN_SLOTS_DEP_BLOCKED), 1u);
}

} // namespace riscv
