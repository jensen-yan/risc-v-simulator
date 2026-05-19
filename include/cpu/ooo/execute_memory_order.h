#pragma once

#include "cpu/ooo/cpu_state.h"

#include <optional>
#include <vector>

namespace riscv {

class ExecuteMemoryOrder {
public:
    struct AddrUnknownStoreSnapshotEntry {
        uint64_t instruction_id = 0;
        uint64_t pc = 0;
    };

    using AddrUnknownStoreSnapshot = std::vector<AddrUnknownStoreSnapshotEntry>;

    static AddrUnknownStoreSnapshot captureAddrUnknownStoreSnapshot(const CPUState& state);
    static std::optional<uint64_t> findFirstOlderAddrUnknownStorePc(
        const AddrUnknownStoreSnapshot& snapshot, uint64_t instruction_id);
    static bool markBlockedAddrUnknownPairIfNeeded(
        CPUState& state,
        const DynamicInstPtr& instruction,
        const AddrUnknownStoreSnapshot& snapshot);
    static bool tryRecoverViolation(const DynamicInstPtr& store_instruction,
                                    CPUState& state);
    static void recordLoadReplayBucket(const DynamicInstPtr& instruction,
                                       CPUState& state);
    static void recordLoadReplayReason(const DynamicInstPtr& instruction,
                                       CPUState& state,
                                       PerfCounterId reason_counter_id);
};

} // namespace riscv
