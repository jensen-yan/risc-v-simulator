#include "cpu/ooo/store_queue.h"

#include "cpu/ooo/store_forwarding_buffer.h"

namespace riscv {

bool StoreQueue::allocateStore(const DynamicInstPtr& instruction) {
    if (!instruction || !instruction->is_store_instruction()) {
        return false;
    }
    if (findEntryIndex(instruction) >= 0) {
        return true;
    }

    const int index = findFreeEntryIndex();
    if (index < 0) {
        throw SimulatorException("store queue is full while allocating a store");
    }

    auto& entry = entries_[index];
    entry = StoreQueueEntry{};
    entry.valid = true;
    entry.instruction = instruction;
    entry.instruction_id = instruction->get_instruction_id();
    entry.pc = instruction->get_pc();
    return true;
}

bool StoreQueue::syncFromInstruction(const DynamicInstPtr& instruction) {
    auto* entry = ensureEntry(instruction);
    if (!entry) {
        return false;
    }

    const auto& decoded = instruction->get_decoded_info();
    auto& memory_info = instruction->get_memory_info();
    if (memory_info.memory_size == 0) {
        memory_info.memory_size = decoded.memory_access_size;
    }
    entry->size = memory_info.memory_size;

    if (memory_info.address_ready && memory_info.memory_size != 0) {
        entry->address_ready = true;
        entry->address = memory_info.memory_address;
        entry->size = memory_info.memory_size;
    }

    return true;
}

bool StoreQueue::updateAddress(const DynamicInstPtr& instruction,
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
    memory_info.is_store = true;
    memory_info.memory_address = address;
    memory_info.memory_size = size;
    memory_info.address_ready = true;
    return true;
}

bool StoreQueue::updateData(const DynamicInstPtr& instruction, uint64_t value) {
    auto* entry = ensureEntry(instruction);
    if (!entry) {
        return false;
    }

    entry->data_ready = true;
    entry->value = value;

    auto& memory_info = instruction->get_memory_info();
    memory_info.is_memory_op = true;
    memory_info.is_store = true;
    memory_info.memory_value = value;
    return true;
}

bool StoreQueue::markCompleted(const DynamicInstPtr& instruction) {
    auto* entry = ensureEntry(instruction);
    if (!entry) {
        return false;
    }
    entry->completed = true;
    return true;
}

bool StoreQueue::markCommitted(const DynamicInstPtr& instruction) {
    auto* entry = ensureEntry(instruction);
    if (!entry) {
        return false;
    }
    entry->committed = true;
    return true;
}

bool StoreQueue::publishReadyStore(const DynamicInstPtr& instruction,
                                   StoreForwardingBuffer& store_forwarding_buffer) {
    auto* entry = ensureEntry(instruction);
    if (!entry || !entry->address_ready || !entry->data_ready || entry->size == 0 ||
        entry->forwarding_published) {
        return false;
    }

    store_forwarding_buffer.add_store(
        instruction, entry->address, entry->value, entry->size);
    entry->forwarding_published = true;
    return true;
}

void StoreQueue::retireStoresBefore(uint64_t instruction_id) {
    for (auto& entry : entries_) {
        if (entry.valid && entry.instruction_id <= instruction_id) {
            entry = StoreQueueEntry{};
        }
    }
}

void StoreQueue::flushAfter(uint64_t instruction_id) {
    for (auto& entry : entries_) {
        if (entry.valid && entry.instruction_id > instruction_id) {
            entry = StoreQueueEntry{};
        }
    }
}

void StoreQueue::flush() {
    for (auto& entry : entries_) {
        entry = StoreQueueEntry{};
    }
}

size_t StoreQueue::getOccupiedEntryCount() const {
    size_t occupied = 0;
    for (const auto& entry : entries_) {
        if (entry.valid) {
            ++occupied;
        }
    }
    return occupied;
}

const StoreQueueEntry* StoreQueue::findEntryForInstruction(
    const DynamicInstPtr& instruction) const {
    const int index = findEntryIndex(instruction);
    return index >= 0 ? &entries_[index] : nullptr;
}

bool StoreQueue::isAddressReady(const DynamicInstPtr& instruction) const {
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

bool StoreQueue::isDataReady(const DynamicInstPtr& instruction) const {
    const auto* entry = findEntryForInstruction(instruction);
    return entry && entry->data_ready;
}

bool StoreQueue::isReadyForStoreAccess(const DynamicInstPtr& instruction) const {
    return isAddressReady(instruction) && isDataReady(instruction);
}

int StoreQueue::findEntryIndex(const DynamicInstPtr& instruction) const {
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

int StoreQueue::findFreeEntryIndex() const {
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (!entries_[i].valid) {
            return i;
        }
    }
    return -1;
}

StoreQueueEntry* StoreQueue::ensureEntry(const DynamicInstPtr& instruction) {
    if (!instruction || !instruction->is_store_instruction()) {
        return nullptr;
    }
    if (!allocateStore(instruction)) {
        return nullptr;
    }

    const int index = findEntryIndex(instruction);
    return index >= 0 ? &entries_[index] : nullptr;
}

} // namespace riscv
