#include <gtest/gtest.h>

#include "cpu/ooo/execute_load_hazard.h"

#include <memory>

namespace riscv {

namespace {

DecodedInstruction makeMemoryInstruction(Opcode opcode) {
    DecodedInstruction decoded;
    decoded.opcode = opcode;
    decoded.memory_access_size = 4;
    return decoded;
}

CPUState makeHazardState() {
    CPUState state;
    state.reorder_buffer = std::make_unique<ReorderBuffer>();
    state.reservation_station = std::make_unique<ReservationStation>();
    return state;
}

ExecutionUnit makeLoadUnit(const DynamicInstPtr& load) {
    ExecutionUnit unit;
    unit.busy = true;
    unit.instruction = load;
    unit.load_address = 0x240;
    unit.load_size = 4;
    return unit;
}

} // namespace

TEST(ExecuteLoadHazardTest, ContinuesWhenNoEarlierStoreHazardExists) {
    auto state = makeHazardState();
    auto load = state.reorder_buffer->allocate_entry(makeMemoryInstruction(Opcode::LOAD), 0x100, 1);
    ASSERT_NE(load, nullptr);
    auto unit = makeLoadUnit(load);

    const auto decision = ExecuteLoadHazard::handleEarlierStoreHazard(unit, 0, state);

    EXPECT_EQ(decision, ExecuteLoadHazard::Decision::ContinueExecution);
    EXPECT_TRUE(unit.busy);
    EXPECT_EQ(unit.instruction, load);
    EXPECT_EQ(load->get_memory_info().replay_count, 0u);
}

TEST(ExecuteLoadHazardTest, ReplaysOnOlderAddressUnknownStore) {
    auto state = makeHazardState();
    auto store = state.reorder_buffer->allocate_entry(makeMemoryInstruction(Opcode::STORE), 0x100, 1);
    ASSERT_NE(store, nullptr);
    store->get_memory_info().address_ready = false;
    store->get_memory_info().memory_size = 0;
    auto load = state.reorder_buffer->allocate_entry(makeMemoryInstruction(Opcode::LOAD), 0x104, 2);
    ASSERT_NE(load, nullptr);
    ASSERT_EQ(state.reservation_station->allocate_execution_unit(ExecutionUnitType::LOAD), 0);
    auto unit = makeLoadUnit(load);

    const auto decision = ExecuteLoadHazard::handleEarlierStoreHazard(unit, 0, state);

    EXPECT_EQ(decision, ExecuteLoadHazard::Decision::Replayed);
    EXPECT_FALSE(unit.busy);
    EXPECT_EQ(unit.instruction, nullptr);
    EXPECT_EQ(load->get_status(), DynamicInst::Status::ISSUED);
    EXPECT_EQ(load->get_memory_info().replay_count, 1u);
    EXPECT_EQ(load->get_memory_info().replay_rob_store_addr_unknown_count, 1u);
    EXPECT_EQ(store->get_memory_info().caused_rob_addr_unknown_block_count, 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOAD_REPLAYS_ROB_STORE_ADDR_UNKNOWN), 1u);
    EXPECT_TRUE(state.reservation_station->is_execution_unit_available(ExecutionUnitType::LOAD));
}

TEST(ExecuteLoadHazardTest, AllowsSpeculatedAddressUnknownLoadToContinue) {
    auto state = makeHazardState();
    auto store = state.reorder_buffer->allocate_entry(makeMemoryInstruction(Opcode::STORE), 0x100, 1);
    ASSERT_NE(store, nullptr);
    store->get_memory_info().address_ready = false;
    store->get_memory_info().memory_size = 0;
    auto load = state.reorder_buffer->allocate_entry(makeMemoryInstruction(Opcode::LOAD), 0x104, 2);
    ASSERT_NE(load, nullptr);
    load->get_memory_info().speculated_past_addr_unknown_store = true;
    auto unit = makeLoadUnit(load);

    const auto decision = ExecuteLoadHazard::handleEarlierStoreHazard(unit, 0, state);

    EXPECT_EQ(decision, ExecuteLoadHazard::Decision::ContinueExecution);
    EXPECT_TRUE(unit.busy);
    EXPECT_EQ(unit.instruction, load);
    EXPECT_EQ(load->get_memory_info().replay_count, 0u);
    EXPECT_EQ(store->get_memory_info().caused_rob_addr_unknown_block_count, 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOAD_REPLAYS_ROB_STORE_ADDR_UNKNOWN), 0u);
}

TEST(ExecuteLoadHazardTest, ReplaysOnOlderOverlappingStore) {
    auto state = makeHazardState();
    auto store = state.reorder_buffer->allocate_entry(makeMemoryInstruction(Opcode::STORE), 0x100, 1);
    ASSERT_NE(store, nullptr);
    auto& store_memory = store->get_memory_info();
    store_memory.address_ready = true;
    store_memory.memory_address = 0x240;
    store_memory.memory_size = 4;
    auto load = state.reorder_buffer->allocate_entry(makeMemoryInstruction(Opcode::LOAD), 0x104, 2);
    ASSERT_NE(load, nullptr);
    auto unit = makeLoadUnit(load);

    const auto decision = ExecuteLoadHazard::handleEarlierStoreHazard(unit, 0, state);

    EXPECT_EQ(decision, ExecuteLoadHazard::Decision::Replayed);
    EXPECT_EQ(load->get_memory_info().replay_count, 1u);
    EXPECT_EQ(load->get_memory_info().replay_rob_store_overlap_count, 1u);
    EXPECT_EQ(store->get_memory_info().caused_rob_overlap_block_count, 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOAD_REPLAYS_ROB_STORE_OVERLAP), 1u);
}

TEST(ExecuteLoadHazardTest, ReplaysOnOlderAmo) {
    auto state = makeHazardState();
    ASSERT_NE(state.reorder_buffer->allocate_entry(makeMemoryInstruction(Opcode::AMO), 0x100, 1), nullptr);
    auto load = state.reorder_buffer->allocate_entry(makeMemoryInstruction(Opcode::LOAD), 0x104, 2);
    ASSERT_NE(load, nullptr);
    auto unit = makeLoadUnit(load);

    const auto decision = ExecuteLoadHazard::handleEarlierStoreHazard(unit, 0, state);

    EXPECT_EQ(decision, ExecuteLoadHazard::Decision::Replayed);
    EXPECT_EQ(load->get_memory_info().replay_count, 1u);
    EXPECT_EQ(load->get_memory_info().replay_rob_store_amo_count, 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOAD_REPLAYS_ROB_STORE_AMO), 1u);
}

} // namespace riscv
