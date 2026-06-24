#include "cpu/ooo/load_queue.h"

#include <algorithm>

namespace riscv {

namespace {

bool rangesOverlap(uint64_t lhs_addr, uint64_t lhs_size, uint64_t rhs_addr, uint64_t rhs_size) {
    if (lhs_size == 0 || rhs_size == 0) {
        return false;
    }
    const uint64_t lhs_end = lhs_addr + lhs_size - 1;
    const uint64_t rhs_end = rhs_addr + rhs_size - 1;
    return lhs_addr <= rhs_end && rhs_addr <= lhs_end;
}

bool loadHasExecutedOrCompleted(const LoadQueueEntry& entry) {
    if (entry.issued || entry.completed) {
        return true;
    }
    if (!entry.instruction) {
        return false;
    }
    return entry.instruction->is_executing() || entry.instruction->is_completed();
}

} // namespace

bool LoadQueue::allocateLoad(const DynamicInstPtr& instruction) {
    if (!instruction || !instruction->is_load_instruction()) {
        return false;
    }
    if (findEntryIndex(instruction) >= 0) {
        return true;
    }

    const int index = findFreeEntryIndex();
    if (index < 0) {
        throw SimulatorException("load queue is full while allocating a load");
    }

    auto& entry = entries_[index];
    entry = LoadQueueEntry{};
    entry.valid = true;
    entry.instruction = instruction;
    entry.instruction_id = instruction->get_instruction_id();
    entry.pc = instruction->get_pc();
    return true;
}

bool LoadQueue::syncFromInstruction(const DynamicInstPtr& instruction) {
    auto* entry = ensureEntry(instruction);
    if (!entry) {
        return false;
    }

    const auto& decoded = instruction->get_decoded_info();
    auto& memory_info = instruction->get_memory_info();
    memory_info.is_memory_op = true;
    memory_info.is_load = true;
    if (memory_info.memory_size == 0) {
        memory_info.memory_size = decoded.memory_access_size;
    }
    entry->size = memory_info.memory_size;

    if (memory_info.address_ready && memory_info.memory_size != 0) {
        entry->address_ready = true;
        entry->address = memory_info.memory_address;
        entry->size = memory_info.memory_size;
    }

    entry->replay_count = std::max(entry->replay_count, memory_info.replay_count);
    return true;
}

bool LoadQueue::updateAddress(const DynamicInstPtr& instruction,
                              uint64_t address,
                              uint8_t size) {
    auto* entry = ensureEntry(instruction);
    if (!entry) {
        return false;
    }

    entry->address_ready = true;
    entry->address = address;
    entry->size = size;

    auto& memory_info = instruction->get_memory_info();
    memory_info.is_memory_op = true;
    memory_info.is_load = true;
    memory_info.memory_address = address;
    memory_info.memory_size = size;
    memory_info.address_ready = true;
    return true;
}

bool LoadQueue::markIssued(const DynamicInstPtr& instruction) {
    auto* entry = ensureEntry(instruction);
    if (!entry) {
        return false;
    }
    entry->issued = true;
    return true;
}

bool LoadQueue::markReplayed(const DynamicInstPtr& instruction) {
    auto* entry = ensureEntry(instruction);
    if (!entry) {
        return false;
    }

    const auto& memory_info = instruction->get_memory_info();
    entry->replay_count = std::max(entry->replay_count + 1, memory_info.replay_count);
    entry->issued = false;
    entry->completed = false;
    return true;
}

bool LoadQueue::markCompleted(const DynamicInstPtr& instruction) {
    auto* entry = ensureEntry(instruction);
    if (!entry) {
        return false;
    }
    entry->issued = true;
    entry->completed = true;
    return true;
}

bool LoadQueue::markCommitted(const DynamicInstPtr& instruction) {
    auto* entry = ensureEntry(instruction);
    if (!entry) {
        return false;
    }
    entry->committed = true;
    return true;
}

void LoadQueue::retireLoadsBefore(uint64_t instruction_id) {
    for (auto& entry : entries_) {
        if (entry.valid && entry.instruction_id <= instruction_id) {
            entry = LoadQueueEntry{};
        }
    }
}

void LoadQueue::flushAfter(uint64_t instruction_id) {
    for (auto& entry : entries_) {
        if (entry.valid && entry.instruction_id > instruction_id) {
            entry = LoadQueueEntry{};
        }
    }
}

void LoadQueue::flush() {
    for (auto& entry : entries_) {
        entry = LoadQueueEntry{};
    }
}

const LoadQueueEntry* LoadQueue::findFirstViolatingLoadAfterStore(
    uint64_t store_instruction_id,
    uint64_t store_address,
    uint8_t store_size) const {
    const LoadQueueEntry* first = nullptr;
    for (const auto& entry : entries_) {
        if (!entry.valid || !entry.instruction || entry.instruction_id <= store_instruction_id) {
            continue;
        }

        const auto& memory_info = entry.instruction->get_memory_info();
        if (!memory_info.speculated_past_addr_unknown_store || !entry.address_ready ||
            entry.size == 0 || !loadHasExecutedOrCompleted(entry)) {
            continue;
        }
        if (!rangesOverlap(store_address, store_size, entry.address, entry.size)) {
            continue;
        }
        if (!first || entry.instruction_id < first->instruction_id) {
            first = &entry;
        }
    }
    return first;
}

size_t LoadQueue::getOccupiedEntryCount() const {
    size_t occupied = 0;
    for (const auto& entry : entries_) {
        if (entry.valid) {
            ++occupied;
        }
    }
    return occupied;
}

const LoadQueueEntry* LoadQueue::findEntryForInstruction(
    const DynamicInstPtr& instruction) const {
    const int index = findEntryIndex(instruction);
    return index >= 0 ? &entries_[index] : nullptr;
}

bool LoadQueue::isAddressReady(const DynamicInstPtr& instruction) const {
    const auto* entry = findEntryForInstruction(instruction);
    if (entry) {
        return entry->address_ready && entry->size != 0;
    }
    if (!instruction) {
        return false;
    }
    const auto& memory_info = instruction->get_memory_info();
    return memory_info.address_ready && memory_info.memory_size != 0;
}

bool LoadQueue::isCompleted(const DynamicInstPtr& instruction) const {
    const auto* entry = findEntryForInstruction(instruction);
    return entry && entry->completed;
}

int LoadQueue::findEntryIndex(const DynamicInstPtr& instruction) const {
    if (!instruction) {
        return -1;
    }
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (entries_[i].valid && entries_[i].instruction == instruction) {
            return i;
        }
    }
    return -1;
}

int LoadQueue::findFreeEntryIndex() const {
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (!entries_[i].valid) {
            return i;
        }
    }
    return -1;
}

LoadQueueEntry* LoadQueue::ensureEntry(const DynamicInstPtr& instruction) {
    if (!instruction || !instruction->is_load_instruction()) {
        return nullptr;
    }
    if (!allocateLoad(instruction)) {
        return nullptr;
    }

    const int index = findEntryIndex(instruction);
    return index >= 0 ? &entries_[index] : nullptr;
}

} // namespace riscv
