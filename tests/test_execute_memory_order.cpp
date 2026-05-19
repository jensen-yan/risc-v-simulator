#include <gtest/gtest.h>

#include "cpu/ooo/execute_memory_order.h"
#include "cpu/ooo/register_rename.h"
#include "cpu/ooo/reorder_buffer.h"
#include "cpu/ooo/reservation_station.h"
#include "cpu/ooo/store_buffer.h"

#include <memory>

namespace riscv {

namespace {

DecodedInstruction makeMemoryInstruction(Opcode opcode) {
    DecodedInstruction decoded;
    decoded.opcode = opcode;
    decoded.memory_access_size = 4;
    return decoded;
}

} // namespace

TEST(ExecuteMemoryOrderTest, CapturesOnlyUnresolvedOlderStores) {
    CPUState state;
    state.reorder_buffer = std::make_unique<ReorderBuffer>();

    const auto store_decoded = makeMemoryInstruction(Opcode::STORE);
    const auto load_decoded = makeMemoryInstruction(Opcode::LOAD);

    auto unresolved_store = state.reorder_buffer->allocate_entry(store_decoded, 0x100, 1);
    ASSERT_NE(unresolved_store, nullptr);
    auto& unresolved_memory = unresolved_store->get_memory_info();
    unresolved_memory.address_ready = false;
    unresolved_memory.memory_size = 0;

    auto resolved_store = state.reorder_buffer->allocate_entry(store_decoded, 0x104, 2);
    ASSERT_NE(resolved_store, nullptr);
    auto& resolved_memory = resolved_store->get_memory_info();
    resolved_memory.address_ready = true;
    resolved_memory.memory_size = 4;

    ASSERT_NE(state.reorder_buffer->allocate_entry(load_decoded, 0x108, 3), nullptr);

    const auto snapshot = ExecuteMemoryOrder::captureAddrUnknownStoreSnapshot(state);

    ASSERT_EQ(snapshot.size(), 1u);
    EXPECT_EQ(snapshot[0].instruction_id, 1u);
    EXPECT_EQ(snapshot[0].pc, 0x100u);
}

TEST(ExecuteMemoryOrderTest, MarksBlockedAddrUnknownPairOnce) {
    CPUState state;
    state.reorder_buffer = std::make_unique<ReorderBuffer>();

    auto load = create_dynamic_inst(makeMemoryInstruction(Opcode::LOAD), 0x200, 2);
    state.recordAddrUnknownPairViolation(load->get_pc(), 0x100);

    const ExecuteMemoryOrder::AddrUnknownStoreSnapshot snapshot = {{1, 0x100}};

    EXPECT_TRUE(ExecuteMemoryOrder::markBlockedAddrUnknownPairIfNeeded(state, load, snapshot));
    EXPECT_TRUE(load->get_memory_info().blocked_by_addr_unknown_pair);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOADS_BLOCKED_ADDR_UNKNOWN_PAIR), 1u);

    EXPECT_TRUE(ExecuteMemoryOrder::markBlockedAddrUnknownPairIfNeeded(state, load, snapshot));
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOADS_BLOCKED_ADDR_UNKNOWN_PAIR), 1u);
}

TEST(ExecuteMemoryOrderTest, RecoversOverlappingAddrUnknownSpeculationViolation) {
    CPUState state;
    state.reorder_buffer = std::make_unique<ReorderBuffer>();
    state.reservation_station = std::make_unique<ReservationStation>();
    state.register_rename = std::make_unique<RegisterRenameUnit>();
    state.store_buffer = std::make_unique<StoreBuffer>();
    state.pc = 0xDEAD;

    auto store = state.reorder_buffer->allocate_entry(makeMemoryInstruction(Opcode::STORE), 0x100, 1);
    ASSERT_NE(store, nullptr);
    auto& store_memory = store->get_memory_info();
    store_memory.address_ready = true;
    store_memory.memory_address = 0x2000;
    store_memory.memory_size = 4;

    auto load = state.reorder_buffer->allocate_entry(makeMemoryInstruction(Opcode::LOAD), 0x104, 2);
    ASSERT_NE(load, nullptr);
    auto& load_memory = load->get_memory_info();
    load_memory.speculated_past_addr_unknown_store = true;
    load_memory.address_ready = true;
    load_memory.memory_address = 0x2002;
    load_memory.memory_size = 4;
    load->set_status(DynamicInst::Status::EXECUTING);

    FetchedInstruction fetched;
    fetched.pc = 0x300;
    state.fetch_buffer.push(fetched);
    state.cdb_queue.push(CommonDataBusEntry(load));

    EXPECT_TRUE(ExecuteMemoryOrder::tryRecoverViolation(store, state));

    EXPECT_EQ(state.pc, 0x100u);
    EXPECT_TRUE(state.fetch_buffer.empty());
    EXPECT_TRUE(state.cdb_queue.empty());
    EXPECT_TRUE(state.reorder_buffer->is_empty());
    EXPECT_TRUE(state.isBlockedAddrUnknownPair(load->get_pc(), store->get_pc()));
    EXPECT_EQ(state.load_profiles[load->get_pc()].speculated_addr_unknown_violation, 1u);
    EXPECT_EQ(state.store_profiles[store->get_pc()].caused_order_violation, 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::MEMORY_ORDER_VIOLATION_RECOVERIES), 1u);
}

} // namespace riscv
