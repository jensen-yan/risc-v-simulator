#pragma once

#include "common/types.h"
#include "cpu/ooo/dynamic_inst.h"
#include "cpu/ooo/ooo_types.h"

#include <array>
#include <cstdint>

namespace riscv {

struct LoadQueueEntry {
    bool valid = false;
    DynamicInstPtr instruction = nullptr;
    uint64_t instruction_id = 0;
    uint64_t pc = 0;

    bool address_ready = false;
    uint64_t address = 0;
    uint8_t size = 0;

    bool issued = false;
    bool completed = false;
    bool committed = false;
    uint32_t replay_count = 0;
};

class LoadQueue {
public:
    static constexpr int MAX_ENTRIES =
        static_cast<int>(OOOPipelineConfig::LOAD_QUEUE_ENTRIES);

    bool allocateLoad(const DynamicInstPtr& instruction);
    bool syncFromInstruction(const DynamicInstPtr& instruction);
    bool updateAddress(const DynamicInstPtr& instruction, uint64_t address, uint8_t size);
    bool markIssued(const DynamicInstPtr& instruction);
    bool markReplayed(const DynamicInstPtr& instruction);
    bool markCompleted(const DynamicInstPtr& instruction);
    bool markCommitted(const DynamicInstPtr& instruction);

    void retireLoadsBefore(uint64_t instruction_id);
    void flushAfter(uint64_t instruction_id);
    void flush();

    size_t getOccupiedEntryCount() const;
    const LoadQueueEntry* findEntryForInstruction(const DynamicInstPtr& instruction) const;
    bool isAddressReady(const DynamicInstPtr& instruction) const;
    bool isCompleted(const DynamicInstPtr& instruction) const;

private:
    std::array<LoadQueueEntry, MAX_ENTRIES> entries_{};

    int findEntryIndex(const DynamicInstPtr& instruction) const;
    int findFreeEntryIndex() const;
    LoadQueueEntry* ensureEntry(const DynamicInstPtr& instruction);
};

} // namespace riscv
