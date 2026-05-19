#include <gtest/gtest.h>

#include "cpu/ooo/execute_load_access.h"

#include <memory>

namespace riscv {

namespace {

DecodedInstruction makeLoadInstruction(bool is_signed_load = false, Opcode opcode = Opcode::LOAD) {
    DecodedInstruction decoded;
    decoded.opcode = opcode;
    decoded.memory_access_size = 4;
    decoded.is_signed_load = is_signed_load;
    return decoded;
}

DecodedInstruction makeStoreInstruction() {
    DecodedInstruction decoded;
    decoded.opcode = Opcode::STORE;
    decoded.memory_access_size = 4;
    return decoded;
}

CPUState makeLoadState() {
    CPUState state;
    state.memory = std::make_shared<Memory>(4096);
    state.store_buffer = std::make_unique<StoreBuffer>();
    return state;
}

ExecutionUnit makeLoadUnit(const DynamicInstPtr& load, uint64_t address, uint8_t size) {
    ExecutionUnit unit;
    unit.busy = true;
    unit.instruction = load;
    unit.load_address = address;
    unit.load_size = size;
    return unit;
}

} // namespace

TEST(ExecuteLoadAccessTest, LoadsFromMemoryWithoutCache) {
    auto state = makeLoadState();
    state.memory->writeWord(0x100, 0x80000000);
    auto load = create_dynamic_inst(makeLoadInstruction(/*is_signed_load=*/true), 0x200, 2);
    auto unit = makeLoadUnit(load, 0x100, 4);

    const auto result = ExecuteLoadAccess::perform(unit, state);

    EXPECT_EQ(result, ExecuteLoadAccess::Result::LoadedFromMemory);
    EXPECT_EQ(unit.result, 0xFFFFFFFF80000000ULL);
    EXPECT_TRUE(unit.dcache.request_sent);
    EXPECT_FALSE(unit.dcache.waiting);
    EXPECT_EQ(load->get_memory_info().memory_value, 0xFFFFFFFF80000000ULL);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOADS_FROM_MEMORY), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_READ_ACCESSES), 1u);
}

TEST(ExecuteLoadAccessTest, UsesFullStoreForwarding) {
    auto state = makeLoadState();
    auto store = create_dynamic_inst(makeStoreInstruction(), 0x100, 1);
    state.store_buffer->add_store(store, 0x200, 0x12345678, 4);
    auto load = create_dynamic_inst(makeLoadInstruction(), 0x104, 2);
    auto unit = makeLoadUnit(load, 0x200, 4);

    const auto result = ExecuteLoadAccess::perform(unit, state);

    EXPECT_EQ(result, ExecuteLoadAccess::Result::Forwarded);
    EXPECT_EQ(unit.result, 0x12345678u);
    EXPECT_TRUE(load->get_memory_info().store_forwarded);
    EXPECT_EQ(load->get_memory_info().load_final_source,
              DynamicInst::MemoryInfo::LoadFinalSource::ForwardedFull);
    EXPECT_EQ(store->get_memory_info().caused_forwarded_full_count, 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOADS_FORWARDED_FULL_MATCH), 1u);
}

TEST(ExecuteLoadAccessTest, MergesPartialStoreForwardingWithMemoryValue) {
    auto state = makeLoadState();
    state.memory->writeWord(0x200, 0x12345678);
    auto store = create_dynamic_inst(makeStoreInstruction(), 0x100, 1);
    state.store_buffer->add_store(store, 0x201, 0xAB, 1);
    auto load = create_dynamic_inst(makeLoadInstruction(), 0x104, 2);
    auto unit = makeLoadUnit(load, 0x200, 4);

    const auto result = ExecuteLoadAccess::perform(unit, state);

    EXPECT_EQ(result, ExecuteLoadAccess::Result::LoadedFromMemory);
    EXPECT_EQ(unit.result, 0x1234AB78u);
    EXPECT_TRUE(load->get_memory_info().store_forwarded);
    EXPECT_EQ(load->get_memory_info().load_final_source,
              DynamicInst::MemoryInfo::LoadFinalSource::ForwardedPartial);
    EXPECT_EQ(store->get_memory_info().caused_forwarded_partial_count, 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOADS_FORWARDED_PARTIAL_MATCH), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::LOADS_FROM_MEMORY), 1u);
}

TEST(ExecuteLoadAccessTest, ReportsUnsupportedLoadSizeAsException) {
    auto state = makeLoadState();
    auto load = create_dynamic_inst(makeLoadInstruction(), 0x200, 2);
    auto unit = makeLoadUnit(load, 0x100, 3);

    const auto result = ExecuteLoadAccess::perform(unit, state);

    EXPECT_EQ(result, ExecuteLoadAccess::Result::Exception);
    EXPECT_TRUE(unit.has_exception);
    EXPECT_NE(unit.exception_msg.find("unsupported load size"), std::string::npos);
    EXPECT_EQ(unit.result, 0u);
}

} // namespace riscv
