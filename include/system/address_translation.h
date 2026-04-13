#pragma once

#include "common/types.h"

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

enum class TranslationFailureReason : uint8_t {
    None = 0,
    UnsupportedMode,
    InvalidPte,
    PermissionDenied,
    AccessFault,
    UnsupportedPageSize,
};

struct TranslationResult {
    bool success = false;
    Address physical_address = 0;
    TranslationFailureReason failure_reason = TranslationFailureReason::None;
    std::string message;
};

class AddressTranslation {
public:
    AddressTranslation(std::shared_ptr<Memory> memory, PrivilegeState* privilegeState);

    TranslationResult translateInstructionAddress(Address virtualAddress, size_t size) const;
    TranslationResult translateLoadAddress(Address virtualAddress, size_t size) const;
    TranslationResult translateStoreAddress(Address virtualAddress, size_t size) const;

private:
    TranslationResult translate(Address virtualAddress, size_t size, MemoryAccessType accessType) const;

    std::shared_ptr<Memory> memory_;
    PrivilegeState* privilege_state_ = nullptr;
};

} // namespace riscv
