#include <gtest/gtest.h>

#include "cpu/ooo/commit_retire_effects.h"
#include "cpu/ooo/register_rename.h"
#include "cpu/ooo/store_buffer.h"

#include <memory>

namespace riscv {

namespace {

DecodedInstruction makeInstruction(Opcode opcode) {
    DecodedInstruction decoded;
    decoded.opcode = opcode;
    decoded.memory_access_size = 4;
    return decoded;
}

} // namespace

TEST(CommitRetireEffectsTest, RetiresOlderStoreBufferEntriesAndRenameCheckpoint) {
    CPUState state;
    state.register_rename = std::make_unique<RegisterRenameUnit>();
    state.store_buffer = std::make_unique<StoreBuffer>();

    auto older_store = create_dynamic_inst(makeInstruction(Opcode::STORE), 0x100, 4);
    auto current = create_dynamic_inst(makeInstruction(Opcode::OP_IMM), 0x104, 7);
    auto younger_store = create_dynamic_inst(makeInstruction(Opcode::STORE), 0x108, 9);
    ASSERT_NE(older_store, nullptr);
    ASSERT_NE(current, nullptr);
    ASSERT_NE(younger_store, nullptr);

    state.store_buffer->add_store(older_store, 0x2000, 0x11, 4);
    state.store_buffer->add_store(younger_store, 0x2008, 0x22, 4);
    state.rename_checkpoints.emplace(current->get_instruction_id(),
                                     state.register_rename->capture_checkpoint());

    ASSERT_EQ(state.store_buffer->get_occupied_entry_count(), 2u);

    CommitRetireEffects::afterInstructionRetired(state, current);

    EXPECT_EQ(state.store_buffer->get_occupied_entry_count(), 1u);
    EXPECT_FALSE(older_store->get_memory_info().store_buffer_published);
    EXPECT_TRUE(state.rename_checkpoints.empty());
}

TEST(CommitRetireEffectsTest, RecordsLoadRetireProfileAndTrainsAddrUnknownSuccess) {
    CPUState state;
    auto load = create_dynamic_inst(makeInstruction(Opcode::LOAD), 0x200, 2);
    ASSERT_NE(load, nullptr);

    auto& memory_info = load->get_memory_info();
    memory_info.replay_count = 3;
    memory_info.replay_host_comm_count = 1;
    memory_info.replay_rob_store_amo_count = 2;
    memory_info.replay_rob_store_addr_unknown_count = 3;
    memory_info.replay_rob_store_overlap_count = 4;
    memory_info.replay_store_buffer_overlap_count = 5;
    memory_info.speculated_past_addr_unknown_store = true;
    memory_info.blocked_by_addr_unknown_pair = true;
    memory_info.load_final_source = DynamicInst::MemoryInfo::LoadFinalSource::ForwardedPartial;
    state.load_addr_unknown_predictor[load->get_pc()] = 1;

    CommitRetireEffects::afterInstructionRetired(state, load);

    const auto& profile = state.load_profiles.at(load->get_pc());
    EXPECT_EQ(profile.executions, 1u);
    EXPECT_EQ(profile.replayed_loads, 1u);
    EXPECT_EQ(profile.replay_total, 3u);
    EXPECT_EQ(profile.replay_host_comm, 1u);
    EXPECT_EQ(profile.replay_rob_store_amo, 2u);
    EXPECT_EQ(profile.replay_rob_store_addr_unknown, 3u);
    EXPECT_EQ(profile.replay_rob_store_overlap, 4u);
    EXPECT_EQ(profile.replay_store_buffer_overlap, 5u);
    EXPECT_EQ(profile.speculated_addr_unknown, 1u);
    EXPECT_EQ(profile.blocked_addr_unknown_pair, 1u);
    EXPECT_EQ(profile.forwarded_partial, 1u);
    EXPECT_EQ(state.load_addr_unknown_predictor.at(load->get_pc()), 2u);
}

TEST(CommitRetireEffectsTest, RecordsStoreRetireProfile) {
    CPUState state;
    auto store = create_dynamic_inst(makeInstruction(Opcode::STORE), 0x300, 5);
    ASSERT_NE(store, nullptr);

    auto& memory_info = store->get_memory_info();
    memory_info.caused_forwarded_full_count = 1;
    memory_info.caused_forwarded_partial_count = 2;
    memory_info.caused_rob_addr_unknown_block_count = 3;
    memory_info.caused_rob_overlap_block_count = 4;
    memory_info.caused_store_buffer_overlap_block_count = 5;

    CommitRetireEffects::afterInstructionRetired(state, store);

    const auto& profile = state.store_profiles.at(store->get_pc());
    EXPECT_EQ(profile.executions, 1u);
    EXPECT_EQ(profile.forwarded_full, 1u);
    EXPECT_EQ(profile.forwarded_partial, 2u);
    EXPECT_EQ(profile.blocked_rob_addr_unknown, 3u);
    EXPECT_EQ(profile.blocked_rob_overlap, 4u);
    EXPECT_EQ(profile.blocked_store_buffer_overlap, 5u);
}

} // namespace riscv
