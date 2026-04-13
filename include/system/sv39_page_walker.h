#pragma once

#include "system/address_translation.h"

#include <memory>

namespace riscv {

class Memory;
class PrivilegeState;

class Sv39PageWalker {
public:
    Sv39PageWalker(std::shared_ptr<Memory> memory, PrivilegeState* privilegeState);

    TranslationResult walk(Address virtualAddress, MemoryAccessType accessType, size_t accessSize) const;

private:
    std::shared_ptr<Memory> memory_;
    PrivilegeState* privilege_state_ = nullptr;
};

} // namespace riscv
