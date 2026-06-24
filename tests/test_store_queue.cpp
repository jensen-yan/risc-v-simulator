#include <gtest/gtest.h>

#include "cpu/ooo/store_forwarding_buffer.h"
#include "cpu/ooo/store_queue.h"

namespace riscv {
namespace {

DecodedInstruction makeStoreInstruction() {
    DecodedInstruction decoded;
    decoded.type = InstructionType::S_TYPE;
    decoded.opcode = Opcode::STORE;
    decoded.rs1 = 1;
    decoded.rs2 = 2;
    decoded.memory_access_size = 4;
    return decoded;
}

DynamicInstPtr makeStore(uint64_t instruction_id, uint64_t pc = 0x100) {
    return create_dynamic_inst(makeStoreInstruction(), pc, instruction_id);
}

} // namespace

TEST(StoreQueueTest, TracksStoreAddressAndDataReadinessSeparately) {
    StoreQueue store_queue;
    StoreForwardingBuffer forwarding_buffer;
    auto store = makeStore(1);

    ASSERT_TRUE(store_queue.allocateStore(store));
    ASSERT_TRUE(store_queue.updateData(store, 0x12345678));

    const auto* data_ready_entry = store_queue.findEntryForInstruction(store);
    ASSERT_NE(data_ready_entry, nullptr);
    EXPECT_TRUE(data_ready_entry->data_ready);
    EXPECT_FALSE(data_ready_entry->address_ready);
    EXPECT_FALSE(store_queue.publishReadyStore(store, forwarding_buffer));
    EXPECT_EQ(forwarding_buffer.get_occupied_entry_count(), 0u);

    ASSERT_TRUE(store_queue.updateAddress(store, 0x2000, 4));
    EXPECT_TRUE(store_queue.publishReadyStore(store, forwarding_buffer));

    const auto* ready_entry = store_queue.findEntryForInstruction(store);
    ASSERT_NE(ready_entry, nullptr);
    EXPECT_TRUE(ready_entry->address_ready);
    EXPECT_TRUE(ready_entry->data_ready);
    EXPECT_TRUE(ready_entry->forwarding_published);
    EXPECT_EQ(forwarding_buffer.get_occupied_entry_count(), 1u);
    EXPECT_TRUE(store->get_memory_info().store_forwarding_buffer_published);
}

TEST(StoreQueueTest, SyncFromInstructionDoesNotCaptureStoreDataBeforeStoreDataOp) {
    StoreQueue store_queue;
    auto store = makeStore(1);
    store->set_src2_ready(true, 0xdeadbeef);

    ASSERT_TRUE(store_queue.syncFromInstruction(store));

    const auto* entry = store_queue.findEntryForInstruction(store);
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(entry->data_ready);
    EXPECT_FALSE(entry->address_ready);
    EXPECT_EQ(entry->size, 4u);

    ASSERT_TRUE(store_queue.updateData(store, 0xdeadbeef));
    entry = store_queue.findEntryForInstruction(store);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->data_ready);
    EXPECT_EQ(entry->value, 0xdeadbeefu);
    EXPECT_EQ(store->get_memory_info().memory_value, 0xdeadbeefu);
}

TEST(StoreQueueTest, MarksCompletionAndCommitWithoutDroppingEntry) {
    StoreQueue store_queue;
    auto store = makeStore(1);

    ASSERT_TRUE(store_queue.allocateStore(store));
    EXPECT_TRUE(store_queue.markCompleted(store));
    EXPECT_TRUE(store_queue.markCommitted(store));

    const auto* entry = store_queue.findEntryForInstruction(store);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->completed);
    EXPECT_TRUE(entry->committed);
    EXPECT_EQ(store_queue.getOccupiedEntryCount(), 1u);
}

TEST(StoreQueueTest, FlushAfterDropsOnlyYoungerStores) {
    StoreQueue store_queue;
    auto older = makeStore(1, 0x100);
    auto survivor = makeStore(2, 0x104);
    auto younger = makeStore(3, 0x108);

    ASSERT_TRUE(store_queue.allocateStore(older));
    ASSERT_TRUE(store_queue.allocateStore(survivor));
    ASSERT_TRUE(store_queue.allocateStore(younger));

    store_queue.flushAfter(2);

    EXPECT_NE(store_queue.findEntryForInstruction(older), nullptr);
    EXPECT_NE(store_queue.findEntryForInstruction(survivor), nullptr);
    EXPECT_EQ(store_queue.findEntryForInstruction(younger), nullptr);
    EXPECT_EQ(store_queue.getOccupiedEntryCount(), 2u);
}

TEST(StoreQueueTest, RetireStoresBeforeClearsCommittedPrefix) {
    StoreQueue store_queue;
    auto older = makeStore(1, 0x100);
    auto retired = makeStore(2, 0x104);
    auto younger = makeStore(3, 0x108);

    ASSERT_TRUE(store_queue.allocateStore(older));
    ASSERT_TRUE(store_queue.allocateStore(retired));
    ASSERT_TRUE(store_queue.allocateStore(younger));

    store_queue.retireStoresBefore(2);

    EXPECT_EQ(store_queue.findEntryForInstruction(older), nullptr);
    EXPECT_EQ(store_queue.findEntryForInstruction(retired), nullptr);
    EXPECT_NE(store_queue.findEntryForInstruction(younger), nullptr);
    EXPECT_EQ(store_queue.getOccupiedEntryCount(), 1u);
}

TEST(StoreQueueTest, ReturnsResolvedAddressViewFromQueueEntry) {
    StoreQueue store_queue;
    auto store = makeStore(1, 0x100);

    ASSERT_TRUE(store_queue.updateAddress(store, 0x2000, 4));

    const auto resolved = store_queue.getResolvedAddress(store);

    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->instruction, store);
    EXPECT_EQ(resolved->instruction_id, store->get_instruction_id());
    EXPECT_EQ(resolved->pc, store->get_pc());
    EXPECT_EQ(resolved->address, 0x2000u);
    EXPECT_EQ(resolved->size, 4u);
}

} // namespace riscv
