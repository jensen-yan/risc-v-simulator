#include "cpu/ooo/commit_retire_effects.h"

namespace riscv {

void CommitRetireEffects::retireStoreBufferAndRenameCheckpoint(
    CPUState& state,
    const DynamicInstPtr& instruction) {
    if (!instruction) {
        return;
    }

    if (state.store_buffer) {
        state.store_buffer->retire_stores_before(instruction->get_instruction_id());
    }
    state.rename_checkpoints.erase(instruction->get_instruction_id());
}

void CommitRetireEffects::recordLoadProfile(CPUState& state,
                                            const DynamicInstPtr& instruction) {
    if (!instruction || !instruction->is_load_instruction()) {
        return;
    }

    const auto& memory_info = instruction->get_memory_info();
    auto& profile = state.load_profiles[instruction->get_pc()];
    profile.executions++;
    profile.replay_total += memory_info.replay_count;
    if (memory_info.replay_count != 0) {
        profile.replayed_loads++;
    }
    profile.replay_host_comm += memory_info.replay_host_comm_count;
    profile.replay_rob_store_amo += memory_info.replay_rob_store_amo_count;
    profile.replay_rob_store_addr_unknown += memory_info.replay_rob_store_addr_unknown_count;
    profile.replay_rob_store_overlap += memory_info.replay_rob_store_overlap_count;
    profile.replay_store_buffer_overlap += memory_info.replay_store_buffer_overlap_count;
    if (memory_info.speculated_past_addr_unknown_store) {
        profile.speculated_addr_unknown++;
        state.trainLoadAddrUnknownPredictor(instruction->get_pc(), true);
    }
    if (memory_info.blocked_by_addr_unknown_pair) {
        profile.blocked_addr_unknown_pair++;
    }

    switch (memory_info.load_final_source) {
        case DynamicInst::MemoryInfo::LoadFinalSource::ForwardedFull:
            profile.forwarded_full++;
            break;
        case DynamicInst::MemoryInfo::LoadFinalSource::ForwardedPartial:
            profile.forwarded_partial++;
            break;
        case DynamicInst::MemoryInfo::LoadFinalSource::FromMemory:
            profile.from_memory++;
            break;
        case DynamicInst::MemoryInfo::LoadFinalSource::None:
            break;
    }
}

void CommitRetireEffects::recordStoreProfile(CPUState& state,
                                             const DynamicInstPtr& instruction) {
    if (!instruction || !instruction->is_store_instruction()) {
        return;
    }

    const auto& memory_info = instruction->get_memory_info();
    auto& profile = state.store_profiles[instruction->get_pc()];
    profile.executions++;
    profile.forwarded_full += memory_info.caused_forwarded_full_count;
    profile.forwarded_partial += memory_info.caused_forwarded_partial_count;
    profile.blocked_rob_addr_unknown += memory_info.caused_rob_addr_unknown_block_count;
    profile.blocked_rob_overlap += memory_info.caused_rob_overlap_block_count;
    profile.blocked_store_buffer_overlap +=
        memory_info.caused_store_buffer_overlap_block_count;
}

void CommitRetireEffects::afterInstructionRetired(CPUState& state,
                                                  const DynamicInstPtr& instruction) {
    retireStoreBufferAndRenameCheckpoint(state, instruction);
    recordLoadProfile(state, instruction);
    recordStoreProfile(state, instruction);
}

} // namespace riscv
