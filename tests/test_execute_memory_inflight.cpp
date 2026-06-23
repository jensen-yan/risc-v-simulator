#include <gtest/gtest.h>

#include "cpu/ooo/execute_memory_inflight.h"

#include <limits>
#include <memory>
#include <vector>

namespace riscv {

namespace {

DecodedInstruction makeInstruction(Opcode opcode = Opcode::LOAD) {
    DecodedInstruction decoded;
    decoded.opcode = opcode;
    return decoded;
}

DynamicInstPtr dispatchInstructionToRs(CPUState& state, Opcode opcode = Opcode::LOAD) {
    auto inst = create_dynamic_inst(makeInstruction(opcode), 0x100, 1);
    const auto dispatch_result = state.reservation_station->dispatch_instruction(inst);
    EXPECT_TRUE(dispatch_result.success);
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
    auto inst = dispatchInstructionToRs(state);
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
    EXPECT_EQ(state.memory_access_inflight[0].wait_latency_cycles, 3u);
    EXPECT_EQ(inst->get_rs_entry(), std::numeric_limits<RSEntry>::max());
    EXPECT_EQ(state.reservation_station->get_occupied_entry_count(), 0u);
    EXPECT_TRUE(state.reservation_station->is_execution_unit_available(ExecutionUnitType::LOAD));
}

TEST(ExecuteMemoryInflightTest, TryMoveFailsWhenQueueIsFull) {
    auto state = makeStateWithReservationStation();
    for (auto& entry : state.memory_access_inflight) {
        entry.valid = true;
    }
    auto inst = dispatchInstructionToRs(state);
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
        [&](ExecutionUnit&, ExecutionUnitType) {
            ++completed;
            return true;
        });

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
    entry.wait_latency_cycles = 7;

    int completed = 0;
    ExecuteMemoryInflight::advance(
        state,
        [&](ExecutionUnit& unit, ExecutionUnitType unit_type) {
            ++completed;
            EXPECT_EQ(unit_type, ExecutionUnitType::LOAD);
            EXPECT_EQ(unit.result, 0xCAFEu);
            return true;
        });

    EXPECT_EQ(completed, 1);
    EXPECT_FALSE(entry.valid);
    EXPECT_EQ(entry.state.instruction, nullptr);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::MEMORY_INFLIGHT_LOAD_MISS_LATENCY_COUNT), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::MEMORY_INFLIGHT_LOAD_MISS_LATENCY_TOTAL), 7u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::MEMORY_INFLIGHT_LOAD_MISS_LATENCY_MAX), 7u);
}

TEST(ExecuteMemoryInflightTest, AdvanceLimitsReadyCompletionsByReplayWidth) {
    CPUState state;
    std::vector<DynamicInstPtr> instructions;
    for (size_t i = 0; i < OOOPipelineConfig::MEMORY_REPLAY_WIDTH + 1; ++i) {
        auto inst = create_dynamic_inst(makeInstruction(Opcode::LOAD), 0x100 + i * 4, i + 1);
        auto& entry = state.memory_access_inflight[i];
        entry.valid = true;
        entry.unit_type = ExecutionUnitType::LOAD;
        entry.state.instruction = inst;
        entry.state.remaining_cycles = 0;
        entry.state.result = 0x1000 + i;
        entry.wait_latency_cycles = 5;
        instructions.push_back(inst);
    }

    std::vector<uint64_t> completed_ids;
    ExecuteMemoryInflight::advance(
        state,
        [&](ExecutionUnit& unit, ExecutionUnitType unit_type) {
            EXPECT_EQ(unit_type, ExecutionUnitType::LOAD);
            completed_ids.push_back(unit.instruction->get_instruction_id());
            return true;
        });

    ASSERT_EQ(completed_ids.size(), OOOPipelineConfig::MEMORY_REPLAY_WIDTH);
    for (size_t i = 0; i < OOOPipelineConfig::MEMORY_REPLAY_WIDTH; ++i) {
        EXPECT_EQ(completed_ids[i], instructions[i]->get_instruction_id());
        EXPECT_FALSE(state.memory_access_inflight[i].valid);
    }

    auto& delayed_entry = state.memory_access_inflight[OOOPipelineConfig::MEMORY_REPLAY_WIDTH];
    EXPECT_TRUE(delayed_entry.valid);
    EXPECT_EQ(delayed_entry.state.remaining_cycles, 0);
    EXPECT_EQ(delayed_entry.state.instruction, instructions.back());
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::STALL_MEMORY_REPLAY_PORT_BUSY), 1u);

    ExecuteMemoryInflight::advance(
        state,
        [&](ExecutionUnit& unit, ExecutionUnitType unit_type) {
            EXPECT_EQ(unit_type, ExecutionUnitType::LOAD);
            completed_ids.push_back(unit.instruction->get_instruction_id());
            return true;
        });

    EXPECT_FALSE(delayed_entry.valid);
    ASSERT_EQ(completed_ids.size(), OOOPipelineConfig::MEMORY_REPLAY_WIDTH + 1);
    EXPECT_EQ(completed_ids.back(), instructions.back()->get_instruction_id());
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
            return true;
        });

    EXPECT_EQ(completed, 1);
    EXPECT_FALSE(entry.valid);
    EXPECT_EQ(entry.state.instruction, nullptr);
}

TEST(ExecuteMemoryInflightTest, AdvanceKeepsLoadEntryWhenCompletionBackpressures) {
    CPUState state;
    auto inst = create_dynamic_inst(makeInstruction(Opcode::LOAD), 0x100, 1);
    auto& entry = state.memory_access_inflight[0];
    entry.valid = true;
    entry.unit_type = ExecutionUnitType::LOAD;
    entry.state.instruction = inst;
    entry.state.remaining_cycles = 0;
    entry.state.result = 0xCAFE;
    entry.wait_latency_cycles = 7;

    int attempts = 0;
    ExecuteMemoryInflight::advance(
        state,
        [&](ExecutionUnit& unit, ExecutionUnitType unit_type) {
            ++attempts;
            EXPECT_EQ(unit_type, ExecutionUnitType::LOAD);
            EXPECT_EQ(unit.result, 0xCAFEu);
            return false;
        });

    EXPECT_EQ(attempts, 1);
    EXPECT_TRUE(entry.valid);
    EXPECT_TRUE(entry.state.completion_pending);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::MEMORY_INFLIGHT_LOAD_MISS_LATENCY_COUNT), 1u);

    ExecuteMemoryInflight::advance(
        state,
        [&](ExecutionUnit&, ExecutionUnitType unit_type) {
            ++attempts;
            EXPECT_EQ(unit_type, ExecutionUnitType::LOAD);
            return true;
        });

    EXPECT_EQ(attempts, 2);
    EXPECT_FALSE(entry.valid);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::MEMORY_INFLIGHT_LOAD_MISS_LATENCY_COUNT), 1u);
}

} // namespace riscv
