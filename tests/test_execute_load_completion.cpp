#include <gtest/gtest.h>

#include "cpu/ooo/cache/blocking_cache.h"
#include "cpu/ooo/execute_load_completion.h"
#include "core/memory.h"

#include <memory>

namespace riscv {

namespace {

DecodedInstruction makeMemoryInstruction(Opcode opcode = Opcode::LOAD) {
    DecodedInstruction decoded;
    decoded.opcode = opcode;
    decoded.memory_access_size = 4;
    return decoded;
}

CPUState makeLoadState() {
    CPUState state;
    state.memory = std::make_shared<Memory>(4096);
    state.reorder_buffer = std::make_unique<ReorderBuffer>();
    state.reservation_station = std::make_unique<ReservationStation>();
    state.store_buffer = std::make_unique<StoreBuffer>();
    return state;
}

DynamicInstPtr issueLoad(CPUState& state, uint64_t pc = 0x100, uint64_t instruction_id = 1) {
    auto load = state.reorder_buffer->allocate_entry(makeMemoryInstruction(), pc, instruction_id);
    EXPECT_NE(load, nullptr);
    const auto issue_result = state.reservation_station->issue_instruction(load);
    EXPECT_TRUE(issue_result.success);
    return load;
}

ExecutionUnit makeLoadUnit(const DynamicInstPtr& load) {
    ExecutionUnit unit;
    unit.busy = true;
    unit.instruction = load;
    unit.load_address = 0x200;
    unit.load_size = 4;
    return unit;
}

} // namespace

TEST(ExecuteLoadCompletionTest, CompletesMemoryLoadWithoutCache) {
    auto state = makeLoadState();
    state.memory->writeWord(0x200, 0x12345678);
    auto load = issueLoad(state);
    auto unit = makeLoadUnit(load);

    const auto result = ExecuteLoadCompletion::perform(unit, 0, state);

    EXPECT_EQ(result, ExecuteLoadCompletion::Result::Completed);
    EXPECT_TRUE(unit.busy);
    EXPECT_EQ(unit.instruction, load);
    EXPECT_EQ(unit.result, 0x12345678u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOADS_FROM_MEMORY), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOAD_REPLAY_BUCKET_0), 1u);
}

TEST(ExecuteLoadCompletionTest, ReplaysYoungerHostCommLoadUntilRobHead) {
    auto state = makeLoadState();
    state.memory->setHostCommAddresses(0x200, 0x240);
    ASSERT_NE(state.reorder_buffer->allocate_entry(makeMemoryInstruction(Opcode::STORE), 0x100, 1),
              nullptr);
    auto load = issueLoad(state, 0x104, 2);
    ASSERT_EQ(state.reservation_station->allocate_execution_unit(ExecutionUnitType::LOAD), 0);
    auto unit = makeLoadUnit(load);

    const auto result = ExecuteLoadCompletion::perform(unit, 0, state);

    EXPECT_EQ(result, ExecuteLoadCompletion::Result::Deferred);
    EXPECT_EQ(load->get_status(), DynamicInst::Status::ISSUED);
    EXPECT_FALSE(unit.busy);
    EXPECT_EQ(unit.instruction, nullptr);
    EXPECT_EQ(load->get_memory_info().replay_count, 1u);
    EXPECT_EQ(load->get_memory_info().replay_host_comm_count, 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOAD_REPLAYS_HOST_COMM), 1u);
    EXPECT_TRUE(state.reservation_station->is_execution_unit_available(ExecutionUnitType::LOAD));
}

TEST(ExecuteLoadCompletionTest, MovesIssuedCacheMissToInflightQueue) {
    auto state = makeLoadState();
    BlockingCacheConfig config;
    config.size_bytes = 64;
    config.line_size_bytes = 16;
    config.associativity = 1;
    config.hit_latency = 1;
    config.miss_penalty = 4;
    config.max_outstanding_misses = 1;
    state.l1d_cache = std::make_unique<BlockingCache>(config);
    auto load = issueLoad(state);
    ASSERT_EQ(state.reservation_station->allocate_execution_unit(ExecutionUnitType::LOAD), 0);
    auto unit = makeLoadUnit(load);

    const auto result = ExecuteLoadCompletion::perform(unit, 0, state);

    EXPECT_EQ(result, ExecuteLoadCompletion::Result::Deferred);
    EXPECT_FALSE(unit.busy);
    EXPECT_EQ(unit.instruction, nullptr);
    EXPECT_TRUE(state.memory_access_inflight[0].valid);
    EXPECT_EQ(state.memory_access_inflight[0].unit_type, ExecutionUnitType::LOAD);
    EXPECT_EQ(state.memory_access_inflight[0].state.instruction, load);
    EXPECT_TRUE(state.reservation_station->is_execution_unit_available(ExecutionUnitType::LOAD));
}

TEST(ExecuteLoadCompletionTest, CompletesUnsupportedLoadSizeAsException) {
    auto state = makeLoadState();
    auto load = issueLoad(state);
    auto unit = makeLoadUnit(load);
    unit.load_size = 3;

    const auto result = ExecuteLoadCompletion::perform(unit, 0, state);

    EXPECT_EQ(result, ExecuteLoadCompletion::Result::Completed);
    EXPECT_TRUE(unit.has_exception);
    EXPECT_NE(unit.exception_msg.find("unsupported load size"), std::string::npos);
    EXPECT_EQ(unit.result, 0u);
}

} // namespace riscv
