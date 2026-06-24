#pragma once

#include "common/types.h"
#include "cpu/ooo/dynamic_inst.h"
#include "cpu/ooo/ooo_types.h"

#include <array>
#include <cstdint>

namespace riscv {

class StoreForwardingBuffer;

struct StoreQueueEntry {
    bool valid = false;
    DynamicInstPtr instruction = nullptr;
    uint64_t instruction_id = 0;
    uint64_t pc = 0;

    bool address_ready = false;
    uint64_t address = 0;
    uint8_t size = 0;

    bool data_ready = false;
    uint64_t value = 0;

    bool completed = false;
    bool committed = false;
    bool forwarding_published = false;
};

class StoreQueue {
public:
    static constexpr int MAX_ENTRIES =
        static_cast<int>(OOOPipelineConfig::STORE_QUEUE_ENTRIES);

    bool allocateStore(const DynamicInstPtr& instruction);
    bool syncFromInstruction(const DynamicInstPtr& instruction);
    bool updateAddress(const DynamicInstPtr& instruction, uint64_t address, uint8_t size);
    bool updateData(const DynamicInstPtr& instruction, uint64_t value);
    bool markCompleted(const DynamicInstPtr& instruction);
    bool markCommitted(const DynamicInstPtr& instruction);
    bool publishReadyStore(const DynamicInstPtr& instruction,
                           StoreForwardingBuffer& store_forwarding_buffer);

    void retireStoresBefore(uint64_t instruction_id);
    void flushAfter(uint64_t instruction_id);
    void flush();

    size_t getOccupiedEntryCount() const;
    const StoreQueueEntry* findEntryForInstruction(const DynamicInstPtr& instruction) const;
    bool isAddressReady(const DynamicInstPtr& instruction) const;
    bool isDataReady(const DynamicInstPtr& instruction) const;
    bool isReadyForStoreAccess(const DynamicInstPtr& instruction) const;

private:
    std::array<StoreQueueEntry, MAX_ENTRIES> entries_{};

    int findEntryIndex(const DynamicInstPtr& instruction) const;
    int findFreeEntryIndex() const;
    StoreQueueEntry* ensureEntry(const DynamicInstPtr& instruction);
};

} // namespace riscv
