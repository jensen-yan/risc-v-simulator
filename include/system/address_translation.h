#pragma once

#include "common/types.h"
#include "system/checkpoint_types.h"

#include <cstddef>
#include <memory>
#include <string>

namespace riscv {

class Memory;
class PrivilegeState;

enum class MemoryAccessType : uint8_t {
    InstructionFetch,
    Load,
    Store,
};

struct TranslationResult {
    bool success;
    Address physical_address;
    CheckpointFailureReason failure_reason;
    std::string message;
};

class AddressTranslation {
public:
    AddressTranslation(std::shared_ptr<Memory> memory, PrivilegeState* privilegeState);

    TranslationResult translateInstructionAddress(Address virtualAddress) const;
    TranslationResult translateLoadAddress(Address virtualAddress, size_t size) const;
    TranslationResult translateStoreAddress(Address virtualAddress, size_t size) const;

private:
    TranslationResult translate(Address virtualAddress, size_t size, MemoryAccessType accessType) const;

    std::shared_ptr<Memory> memory_;
    PrivilegeState* privilege_state_;
};

} // namespace riscv
