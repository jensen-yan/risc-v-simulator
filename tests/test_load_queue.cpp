#include <gtest/gtest.h>

#include "cpu/ooo/load_queue.h"

namespace riscv {
namespace {

DecodedInstruction makeLoadInstruction() {
    DecodedInstruction decoded;
    decoded.type = InstructionType::I_TYPE;
    decoded.opcode = Opcode::LOAD;
    decoded.rs1 = 1;
    decoded.rd = 2;
    decoded.memory_access_size = 8;
    return decoded;
}

DynamicInstPtr makeLoad(uint64_t instruction_id, uint64_t pc = 0x100) {
    return create_dynamic_inst(makeLoadInstruction(), pc, instruction_id);
}

} // namespace

TEST(LoadQueueTest, TracksLoadAddressIssueAndCompletion) {
    LoadQueue load_queue;
    auto load = makeLoad(1);

    ASSERT_TRUE(load_queue.allocateLoad(load));
    ASSERT_TRUE(load_queue.updateAddress(load, 0x2000, 8));
    ASSERT_TRUE(load_queue.markIssued(load));
    ASSERT_TRUE(load_queue.markCompleted(load));

    const auto* entry = load_queue.findEntryForInstruction(load);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->address_ready);
    EXPECT_EQ(entry->address, 0x2000u);
    EXPECT_EQ(entry->size, 8u);
    EXPECT_TRUE(entry->issued);
    EXPECT_TRUE(entry->completed);
    EXPECT_TRUE(load->get_memory_info().is_load);
    EXPECT_TRUE(load->get_memory_info().address_ready);
    EXPECT_EQ(load->get_memory_info().memory_address, 0x2000u);
}

TEST(LoadQueueTest, ReplayClearsIssuedAndCompletionState) {
    LoadQueue load_queue;
    auto load = makeLoad(1);

    ASSERT_TRUE(load_queue.updateAddress(load, 0x2000, 8));
    ASSERT_TRUE(load_queue.markIssued(load));
    ASSERT_TRUE(load_queue.markCompleted(load));
    load->get_memory_info().replay_count = 1;

    ASSERT_TRUE(load_queue.markReplayed(load));

    const auto* entry = load_queue.findEntryForInstruction(load);
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(entry->issued);
    EXPECT_FALSE(entry->completed);
    EXPECT_EQ(entry->replay_count, 1u);
}

TEST(LoadQueueTest, CommitAndRetireAreSeparateLifecycleSteps) {
    LoadQueue load_queue;
    auto load = makeLoad(1);

    ASSERT_TRUE(load_queue.allocateLoad(load));
    ASSERT_TRUE(load_queue.markCommitted(load));

    const auto* entry = load_queue.findEntryForInstruction(load);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->committed);
    EXPECT_EQ(load_queue.getOccupiedEntryCount(), 1u);

    load_queue.retireLoadsBefore(load->get_instruction_id());
    EXPECT_EQ(load_queue.findEntryForInstruction(load), nullptr);
    EXPECT_EQ(load_queue.getOccupiedEntryCount(), 0u);
}

TEST(LoadQueueTest, FlushAfterDropsOnlyYoungerLoads) {
    LoadQueue load_queue;
    auto older = makeLoad(1, 0x100);
    auto survivor = makeLoad(2, 0x104);
    auto younger = makeLoad(3, 0x108);

    ASSERT_TRUE(load_queue.allocateLoad(older));
    ASSERT_TRUE(load_queue.allocateLoad(survivor));
    ASSERT_TRUE(load_queue.allocateLoad(younger));

    load_queue.flushAfter(2);

    EXPECT_NE(load_queue.findEntryForInstruction(older), nullptr);
    EXPECT_NE(load_queue.findEntryForInstruction(survivor), nullptr);
    EXPECT_EQ(load_queue.findEntryForInstruction(younger), nullptr);
    EXPECT_EQ(load_queue.getOccupiedEntryCount(), 2u);
}

} // namespace riscv
