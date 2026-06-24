#include <gtest/gtest.h>

#include "cpu/ooo/cache/non_blocking_cache.h"
#include "cpu/ooo/execute_store_access.h"
#include "core/memory.h"

#include <memory>

namespace riscv {

namespace {

DecodedInstruction makeStoreInstruction() {
    DecodedInstruction decoded;
    decoded.opcode = Opcode::STORE;
    decoded.memory_access_size = 4;
    return decoded;
}

CPUState makeStoreState() {
    CPUState state;
    state.memory = std::make_shared<Memory>(4096);
    state.reorder_buffer = std::make_unique<ReorderBuffer>();
    state.reservation_station = std::make_unique<ReservationStation>();
    return state;
}

DynamicInstPtr dispatchStoreToRs(CPUState& state, uint64_t pc = 0x100, uint64_t instruction_id = 1) {
    auto store = state.reorder_buffer->allocate_entry(makeStoreInstruction(), pc, instruction_id);
    EXPECT_NE(store, nullptr);
    const auto dispatch_result = state.reservation_station->dispatch_instruction(store);
    EXPECT_TRUE(dispatch_result.success);
    return store;
}

ExecutionUnit makeStoreUnit(const DynamicInstPtr& store) {
    ExecutionUnit unit;
    unit.busy = true;
    unit.instruction = store;
    unit.load_address = 0x200;
    unit.load_size = 4;
    return unit;
}

} // namespace

TEST(ExecuteStoreAccessTest, CompletesImmediatelyWithoutCache) {
    auto state = makeStoreState();
    auto store = dispatchStoreToRs(state);
    auto unit = makeStoreUnit(store);

    const auto result = ExecuteStoreAccess::perform(unit, 0, state);

    EXPECT_EQ(result, ExecuteStoreAccess::Result::Completed);
    EXPECT_EQ(unit.result, 0u);
    EXPECT_TRUE(unit.busy);
    EXPECT_EQ(unit.instruction, store);
}

TEST(ExecuteStoreAccessTest, ReplaysYoungerHostCommStoreUntilRobHead) {
    auto state = makeStoreState();
    state.memory->setHostCommAddresses(0x200, 0x240);
    ASSERT_NE(state.reorder_buffer->allocate_entry(makeStoreInstruction(), 0x100, 1), nullptr);
    auto store = dispatchStoreToRs(state, 0x104, 2);
    auto unit = makeStoreUnit(store);

    const auto result = ExecuteStoreAccess::perform(unit, 0, state);

    EXPECT_EQ(result, ExecuteStoreAccess::Result::ReplayedForHostComm);
    EXPECT_EQ(store->get_status(), DynamicInst::Status::DISPATCHED);
    EXPECT_FALSE(unit.busy);
    EXPECT_EQ(unit.instruction, nullptr);
}

TEST(ExecuteStoreAccessTest, MovesIssuedCacheMissToInflightQueue) {
    auto state = makeStoreState();
    NonBlockingCacheConfig config;
    config.size_bytes = 64;
    config.line_size_bytes = 16;
    config.associativity = 1;
    config.hit_latency = 1;
    config.miss_penalty = 4;
    config.max_outstanding_misses = 1;
    state.l1d_cache = std::make_unique<NonBlockingCache>(config);
    auto store = dispatchStoreToRs(state);
    auto unit = makeStoreUnit(store);

    const auto result = ExecuteStoreAccess::perform(unit, 0, state);

    EXPECT_EQ(result, ExecuteStoreAccess::Result::MovedToInflight);
    EXPECT_FALSE(unit.busy);
    EXPECT_EQ(unit.instruction, nullptr);
    EXPECT_TRUE(state.memory_access_inflight[0].valid);
    EXPECT_EQ(state.memory_access_inflight[0].unit_type, ExecutionUnitType::STORE);
    EXPECT_EQ(state.memory_access_inflight[0].state.instruction, store);
}

TEST(ExecuteStoreAccessTest, ReplaysWhenDCacheOutstandingLimitBlocksNewRequest) {
    auto state = makeStoreState();
    NonBlockingCacheConfig config;
    config.size_bytes = 64;
    config.line_size_bytes = 16;
    config.associativity = 1;
    config.hit_latency = 1;
    config.miss_penalty = 4;
    config.max_outstanding_misses = 1;
    state.l1d_cache = std::make_unique<NonBlockingCache>(config);
    static_cast<void>(state.l1d_cache->access(state.memory, 0x100, 4, CacheAccessType::Read));
    auto store = dispatchStoreToRs(state);
    auto unit = makeStoreUnit(store);
    unit.load_address = 0x300;

    const auto result = ExecuteStoreAccess::perform(unit, 0, state);

    EXPECT_EQ(result, ExecuteStoreAccess::Result::BlockedByDCacheOutstanding);
    EXPECT_EQ(store->get_status(), DynamicInst::Status::DISPATCHED);
    EXPECT_FALSE(unit.busy);
    EXPECT_EQ(unit.instruction, nullptr);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_STALL_CYCLES_STORE), 1u);
}

} // namespace riscv
