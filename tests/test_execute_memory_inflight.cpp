#include <gtest/gtest.h>

#include "cpu/ooo/execute_memory_inflight.h"

#include <limits>
#include <memory>

namespace riscv {

namespace {

DecodedInstruction makeInstruction(Opcode opcode = Opcode::LOAD) {
    DecodedInstruction decoded;
    decoded.opcode = opcode;
    return decoded;
}

DynamicInstPtr issueInstruction(CPUState& state, Opcode opcode = Opcode::LOAD) {
    auto inst = create_dynamic_inst(makeInstruction(opcode), 0x100, 1);
    const auto issue_result = state.reservation_station->issue_instruction(inst);
    EXPECT_TRUE(issue_result.success);
    return inst;
}

CPUState makeStateWithReservationStation() {
    CPUState state;
    state.reservation_station = std::make_unique<ReservationStation>();
    return state;
}

} // namespace

TEST(ExecuteMemoryInflightTest, HasAnyReflectsValidEntries) {
    CPUState state;

    EXPECT_FALSE(ExecuteMemoryInflight::hasAny(state));

    state.memory_access_inflight[0].valid = true;

    EXPECT_TRUE(ExecuteMemoryInflight::hasAny(state));
}

TEST(ExecuteMemoryInflightTest, TryMoveReleasesReservationStationAndExecutionUnit) {
    auto state = makeStateWithReservationStation();
    auto inst = issueInstruction(state);
    const int unit_id = state.reservation_station->allocate_execution_unit(ExecutionUnitType::LOAD);
    ASSERT_EQ(unit_id, 0);

    ExecutionUnit unit;
    unit.busy = true;
    unit.instruction = inst;
    unit.remaining_cycles = 3;
    unit.result = 0x1234;
    unit.dcache.request_sent = true;

    const bool moved = ExecuteMemoryInflight::tryMove(
        unit, ExecutionUnitType::LOAD, static_cast<size_t>(unit_id), state);

    EXPECT_TRUE(moved);
    EXPECT_FALSE(unit.busy);
    EXPECT_EQ(unit.instruction, nullptr);
    EXPECT_TRUE(state.memory_access_inflight[0].valid);
    EXPECT_EQ(state.memory_access_inflight[0].unit_type, ExecutionUnitType::LOAD);
    EXPECT_EQ(state.memory_access_inflight[0].state.instruction, inst);
    EXPECT_EQ(state.memory_access_inflight[0].state.remaining_cycles, 3);
    EXPECT_EQ(inst->get_rs_entry(), std::numeric_limits<RSEntry>::max());
    EXPECT_EQ(state.reservation_station->get_occupied_entry_count(), 0u);
    EXPECT_TRUE(state.reservation_station->is_execution_unit_available(ExecutionUnitType::LOAD));
}

TEST(ExecuteMemoryInflightTest, TryMoveFailsWhenQueueIsFull) {
    auto state = makeStateWithReservationStation();
    for (auto& entry : state.memory_access_inflight) {
        entry.valid = true;
    }
    auto inst = issueInstruction(state);
    ExecutionUnit unit;
    unit.busy = true;
    unit.instruction = inst;

    const bool moved = ExecuteMemoryInflight::tryMove(unit, ExecutionUnitType::LOAD, 0, state);

    EXPECT_FALSE(moved);
    EXPECT_TRUE(unit.busy);
    EXPECT_EQ(unit.instruction, inst);
}

TEST(ExecuteMemoryInflightTest, AdvanceKeepsEntryWhileCyclesRemain) {
    CPUState state;
    auto inst = create_dynamic_inst(makeInstruction(), 0x100, 1);
    auto& entry = state.memory_access_inflight[0];
    entry.valid = true;
    entry.unit_type = ExecutionUnitType::LOAD;
    entry.state.instruction = inst;
    entry.state.remaining_cycles = 2;

    int completed = 0;
    ExecuteMemoryInflight::advance(
        state,
        [&](ExecutionUnit&, ExecutionUnitType) { ++completed; });

    EXPECT_TRUE(entry.valid);
    EXPECT_EQ(entry.state.remaining_cycles, 1);
    EXPECT_EQ(completed, 0);
}

TEST(ExecuteMemoryInflightTest, AdvanceCompletesLoadAndClearsEntry) {
    CPUState state;
    auto inst = create_dynamic_inst(makeInstruction(Opcode::LOAD), 0x100, 1);
    auto& entry = state.memory_access_inflight[0];
    entry.valid = true;
    entry.unit_type = ExecutionUnitType::LOAD;
    entry.state.instruction = inst;
    entry.state.remaining_cycles = 1;
    entry.state.result = 0xCAFE;

    int completed = 0;
    ExecuteMemoryInflight::advance(
        state,
        [&](ExecutionUnit& unit, ExecutionUnitType unit_type) {
            ++completed;
            EXPECT_EQ(unit_type, ExecutionUnitType::LOAD);
            EXPECT_EQ(unit.result, 0xCAFEu);
        });

    EXPECT_EQ(completed, 1);
    EXPECT_FALSE(entry.valid);
    EXPECT_EQ(entry.state.instruction, nullptr);
}

TEST(ExecuteMemoryInflightTest, AdvanceCompletesStoreWithZeroResult) {
    CPUState state;
    auto inst = create_dynamic_inst(makeInstruction(Opcode::STORE), 0x100, 1);
    auto& entry = state.memory_access_inflight[0];
    entry.valid = true;
    entry.unit_type = ExecutionUnitType::STORE;
    entry.state.instruction = inst;
    entry.state.remaining_cycles = 1;
    entry.state.result = 0xCAFE;

    int completed = 0;
    ExecuteMemoryInflight::advance(
        state,
        [&](ExecutionUnit& unit, ExecutionUnitType unit_type) {
            ++completed;
            EXPECT_EQ(unit_type, ExecutionUnitType::STORE);
            EXPECT_EQ(unit.result, 0u);
        });

    EXPECT_EQ(completed, 1);
    EXPECT_FALSE(entry.valid);
    EXPECT_EQ(entry.state.instruction, nullptr);
}

} // namespace riscv
