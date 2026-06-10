#include <gtest/gtest.h>

#include "cpu/ooo/commit_memory_effects.h"
#include "core/memory.h"

#include <memory>

namespace riscv {

namespace {

DecodedInstruction makeMemoryInstruction(Opcode opcode, Funct3 funct3 = Funct3::SW) {
    DecodedInstruction decoded;
    decoded.opcode = opcode;
    decoded.funct3 = funct3;
    decoded.memory_access_size = 4;
    return decoded;
}

} // namespace

TEST(CommitMemoryEffectsTest, CommitsStoreToMemoryAndClearsReservation) {
    CPUState state;
    state.memory = std::make_shared<Memory>(4096);
    state.reservation_valid = true;
    state.reservation_addr = 0x80;

    auto store = create_dynamic_inst(makeMemoryInstruction(Opcode::STORE), 0x100, 1);
    ASSERT_NE(store, nullptr);
    auto& memory_info = store->get_memory_info();
    memory_info.address_ready = true;
    memory_info.is_store = true;
    memory_info.memory_address = 0x80;
    memory_info.memory_value = 0x12345678;
    memory_info.memory_size = 4;

    const auto result = CommitMemoryEffects::apply(state, store);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.applied);
    EXPECT_TRUE(result.used_store_memory_port);
    EXPECT_EQ(state.memory->readWord(0x80), 0x12345678u);
    EXPECT_FALSE(state.reservation_valid);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::STORES_COMMITTED), 1u);
}

TEST(CommitMemoryEffectsTest, ReportsMissingStoreMemoryInfo) {
    CPUState state;
    state.memory = std::make_shared<Memory>(4096);
    auto store = create_dynamic_inst(makeMemoryInstruction(Opcode::STORE), 0x100, 1);
    ASSERT_NE(store, nullptr);

    const auto result = CommitMemoryEffects::apply(state, store);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.applied);
    EXPECT_FALSE(result.used_store_memory_port);
    EXPECT_EQ(result.error_message, "store commit missing memory info");
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::STORES_COMMITTED), 0u);
}

TEST(CommitMemoryEffectsTest, CommitsAmoStoreAndReservationState) {
    CPUState state;
    state.memory = std::make_shared<Memory>(4096);
    auto amo = create_dynamic_inst(makeMemoryInstruction(Opcode::AMO, Funct3::LW), 0x100, 1);
    ASSERT_NE(amo, nullptr);

    DynamicInst::AtomicExecuteInfo atomic_info;
    atomic_info.acquire_reservation = true;
    atomic_info.virtual_address = 0x90;
    atomic_info.do_store = true;
    atomic_info.physical_address = 0x90;
    atomic_info.store_value = 0xCAFEBABE;
    atomic_info.width = Funct3::LW;
    amo->set_atomic_execute_info(atomic_info);

    const auto result = CommitMemoryEffects::apply(state, amo);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.applied);
    EXPECT_TRUE(result.used_store_memory_port);
    EXPECT_TRUE(state.reservation_valid);
    EXPECT_EQ(state.reservation_addr, 0x90u);
    EXPECT_EQ(state.memory->readWord(0x90), 0xCAFEBABEu);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::AMOS_COMMITTED), 1u);
}

TEST(CommitMemoryEffectsTest, IgnoresNonMemoryInstruction) {
    CPUState state;
    auto inst = create_dynamic_inst(makeMemoryInstruction(Opcode::OP_IMM), 0x100, 1);
    ASSERT_NE(inst, nullptr);

    const auto result = CommitMemoryEffects::apply(state, inst);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.applied);
    EXPECT_FALSE(result.used_store_memory_port);
}

} // namespace riscv
