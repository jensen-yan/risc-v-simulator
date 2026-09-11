#include <gtest/gtest.h>

#include "cpu/ooo/execute_memory_order.h"
#include "cpu/ooo/load_queue.h"
#include "cpu/ooo/register_rename.h"
#include "cpu/ooo/reorder_buffer.h"
#include "cpu/ooo/reservation_station.h"
#include "cpu/ooo/store_forwarding_buffer.h"

#include <memory>

namespace riscv {

namespace {

DecodedInstruction makeMemoryInstruction(Opcode opcode) {
    DecodedInstruction decoded;
    decoded.opcode = opcode;
    decoded.memory_access_size = 4;
    return decoded;
}

DecodedInstruction makeAluInstruction() {
    DecodedInstruction decoded;
    decoded.type = InstructionType::I_TYPE;
    decoded.opcode = Opcode::OP_IMM;
    decoded.rd = 1;
    decoded.rs1 = 0;
    decoded.imm = 1;
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

TEST(ExecuteMemoryOrderTest, FindsOldestAddrUnknownStoreWithoutAssumingSnapshotOrder) {
    const ExecuteMemoryOrder::AddrUnknownStoreSnapshot snapshot = {
        {80, 0x400},
        {10, 0x300},
        {3, 0x100},
    };

    const auto oldest_pc = ExecuteMemoryOrder::findFirstOlderAddrUnknownStorePc(snapshot, 50);

    ASSERT_TRUE(oldest_pc.has_value());
    EXPECT_EQ(*oldest_pc, 0x100u);
}

TEST(ExecuteMemoryOrderTest, MarksBlockedPairWhenYoungerUnknownStoreAppearsFirst) {
    CPUState state;
    state.reorder_buffer = std::make_unique<ReorderBuffer>();

    auto load = create_dynamic_inst(makeMemoryInstruction(Opcode::LOAD), 0x200, 50);
    state.recordAddrUnknownPairViolation(load->get_pc(), 0x100);

    const ExecuteMemoryOrder::AddrUnknownStoreSnapshot snapshot = {
        {10, 0x300},
        {80, 0x400},
        {3, 0x100},
    };

    EXPECT_TRUE(ExecuteMemoryOrder::markBlockedAddrUnknownPairIfNeeded(state, load, snapshot));
    EXPECT_TRUE(load->get_memory_info().blocked_by_addr_unknown_pair);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOADS_BLOCKED_ADDR_UNKNOWN_PAIR), 1u);
}

TEST(ExecuteMemoryOrderTest, SnapshotAndBlockedPairSurviveRobWraparound) {
    CPUState state;
    state.reorder_buffer = std::make_unique<ReorderBuffer>();

    const auto alu_decoded = makeAluInstruction();
    const auto store_decoded = makeMemoryInstruction(Opcode::STORE);
    auto& rob = *state.reorder_buffer;

    ASSERT_NE(rob.allocate_entry(alu_decoded, 0x10, 1), nullptr);
    ASSERT_NE(rob.allocate_entry(alu_decoded, 0x14, 2), nullptr);

    auto oldest_store = rob.allocate_entry(store_decoded, 0x100, 3);
    ASSERT_NE(oldest_store, nullptr);
    oldest_store->get_memory_info().address_ready = false;
    oldest_store->get_memory_info().memory_size = 0;

    uint64_t filler_id = 1000;
    uint64_t filler_pc = 0x1000;
    while (!rob.is_full()) {
        ASSERT_NE(rob.allocate_entry(alu_decoded, filler_pc, filler_id), nullptr);
        filler_id++;
        filler_pc += 4;
    }

    for (int retire = 0; retire < 2; ++retire) {
        const auto head = rob.get_entry(rob.get_head_entry());
        ASSERT_NE(head, nullptr);
        rob.update_entry(head, 0);
        const auto commit = rob.commit_instruction();
        ASSERT_TRUE(commit.success);
    }

    auto mid_store = rob.allocate_entry(store_decoded, 0x300, 10);
    ASSERT_NE(mid_store, nullptr);
    mid_store->get_memory_info().address_ready = false;
    mid_store->get_memory_info().memory_size = 0;

    auto young_store = rob.allocate_entry(store_decoded, 0x400, 80);
    ASSERT_NE(young_store, nullptr);
    young_store->get_memory_info().address_ready = false;
    young_store->get_memory_info().memory_size = 0;

    ASSERT_EQ(rob.get_head_entry(), oldest_store->get_rob_entry());
    ASSERT_LT(mid_store->get_rob_entry(), oldest_store->get_rob_entry());
    ASSERT_LT(young_store->get_rob_entry(), oldest_store->get_rob_entry());

    const auto snapshot = ExecuteMemoryOrder::captureAddrUnknownStoreSnapshot(state);
    ASSERT_EQ(snapshot.size(), 3u);
    EXPECT_EQ(snapshot[0].instruction_id, 3u);
    EXPECT_EQ(snapshot[0].pc, 0x100u);
    EXPECT_EQ(snapshot[1].instruction_id, 10u);
    EXPECT_EQ(snapshot[1].pc, 0x300u);
    EXPECT_EQ(snapshot[2].instruction_id, 80u);
    EXPECT_EQ(snapshot[2].pc, 0x400u);

    auto load = create_dynamic_inst(makeMemoryInstruction(Opcode::LOAD), 0x200, 50);
    state.recordAddrUnknownPairViolation(load->get_pc(), 0x100);

    const auto oldest_pc = ExecuteMemoryOrder::findFirstOlderAddrUnknownStorePc(snapshot, load->get_instruction_id());
    ASSERT_TRUE(oldest_pc.has_value());
    EXPECT_EQ(*oldest_pc, 0x100u);

    EXPECT_TRUE(ExecuteMemoryOrder::markBlockedAddrUnknownPairIfNeeded(state, load, snapshot));
    EXPECT_TRUE(load->get_memory_info().blocked_by_addr_unknown_pair);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOADS_BLOCKED_ADDR_UNKNOWN_PAIR), 1u);
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

TEST(ExecuteMemoryOrderTest, RecordsLoadReplayReasonAndBucket) {
    CPUState state;
    auto load = create_dynamic_inst(makeMemoryInstruction(Opcode::LOAD), 0x200, 2);
    auto& memory_info = load->get_memory_info();
    memory_info.replay_count = 2;

    ExecuteMemoryOrder::recordLoadReplayReason(
        load, state, PerfCounterId::LOAD_REPLAYS_ROB_STORE_ADDR_UNKNOWN);
    ExecuteMemoryOrder::recordLoadReplayBucket(load, state);

    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOAD_REPLAYS), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOAD_REPLAYS_ROB_STORE_ADDR_UNKNOWN), 1u);
    EXPECT_EQ(memory_info.replay_rob_store_addr_unknown_count, 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOAD_REPLAY_BUCKET_2), 1u);
}

TEST(ExecuteMemoryOrderTest, RecoversOverlappingAddrUnknownSpeculationViolation) {
    CPUState state;
    state.reorder_buffer = std::make_unique<ReorderBuffer>();
    state.reservation_station = std::make_unique<ReservationStation>();
    state.register_rename = std::make_unique<RegisterRenameUnit>();
    state.load_queue = std::make_unique<LoadQueue>();
    state.store_queue = std::make_unique<StoreQueue>();
    state.store_forwarding_buffer = std::make_unique<StoreForwardingBuffer>();
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
    ASSERT_TRUE(state.load_queue->updateAddress(load, 0x2002, 4));
    ASSERT_TRUE(state.load_queue->markIssued(load));

    FetchedInstruction fetched;
    fetched.pc = 0x300;
    state.fetch_buffer.push(fetched);
    ASSERT_TRUE(state.completion_fabric.trySubmit(CompletionEvent(load)));

    EXPECT_TRUE(ExecuteMemoryOrder::tryRecoverViolation(store, state));

    EXPECT_EQ(state.pc, 0x100u);
    EXPECT_TRUE(state.fetch_buffer.empty());
    EXPECT_TRUE(state.completion_fabric.empty());
    EXPECT_TRUE(state.reorder_buffer->is_empty());
    EXPECT_EQ(state.load_queue->getOccupiedEntryCount(), 0u);
    EXPECT_TRUE(state.isBlockedAddrUnknownPair(load->get_pc(), store->get_pc()));
    EXPECT_EQ(state.load_profiles[load->get_pc()].speculated_addr_unknown_violation, 1u);
    EXPECT_EQ(state.store_profiles[store->get_pc()].caused_order_violation, 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::MEMORY_ORDER_VIOLATION_RECOVERIES), 1u);
}

TEST(ExecuteMemoryOrderTest, DoesNotRecoverWhenLqSqScanFindsNoOverlap) {
    CPUState state;
    state.reorder_buffer = std::make_unique<ReorderBuffer>();
    state.reservation_station = std::make_unique<ReservationStation>();
    state.register_rename = std::make_unique<RegisterRenameUnit>();
    state.load_queue = std::make_unique<LoadQueue>();
    state.store_queue = std::make_unique<StoreQueue>();
    state.store_forwarding_buffer = std::make_unique<StoreForwardingBuffer>();
    state.pc = 0xDEAD;

    auto store = state.reorder_buffer->allocate_entry(makeMemoryInstruction(Opcode::STORE), 0x100, 1);
    ASSERT_NE(store, nullptr);
    ASSERT_TRUE(state.store_queue->updateAddress(store, 0x2000, 4));

    auto load = state.reorder_buffer->allocate_entry(makeMemoryInstruction(Opcode::LOAD), 0x104, 2);
    ASSERT_NE(load, nullptr);
    load->get_memory_info().speculated_past_addr_unknown_store = true;
    load->set_status(DynamicInst::Status::EXECUTING);
    ASSERT_TRUE(state.load_queue->updateAddress(load, 0x3000, 4));
    ASSERT_TRUE(state.load_queue->markIssued(load));

    EXPECT_FALSE(ExecuteMemoryOrder::tryRecoverViolation(store, state));
    EXPECT_EQ(state.pc, 0xDEADu);
    EXPECT_FALSE(state.reorder_buffer->is_empty());
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::MEMORY_ORDER_VIOLATION_RECOVERIES), 0u);
}

} // namespace riscv
