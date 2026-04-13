#include "system/address_translation.h"

#include "core/memory.h"
#include "system/privilege_state.h"

namespace riscv {

AddressTranslation::AddressTranslation(std::shared_ptr<Memory> memory, PrivilegeState* privilegeState)
    : memory_(std::move(memory)), privilege_state_(privilegeState) {}

TranslationResult AddressTranslation::translateInstructionAddress(Address virtualAddress) const {
    return translate(virtualAddress, 4, MemoryAccessType::InstructionFetch);
}

TranslationResult AddressTranslation::translateLoadAddress(Address virtualAddress, size_t size) const {
    return translate(virtualAddress, size, MemoryAccessType::Load);
}

TranslationResult AddressTranslation::translateStoreAddress(Address virtualAddress, size_t size) const {
    return translate(virtualAddress, size, MemoryAccessType::Store);
}

TranslationResult AddressTranslation::translate(Address virtualAddress, size_t size, MemoryAccessType accessType) const {
    (void)size;
    (void)accessType;
    (void)memory_;
    (void)privilege_state_;
    return TranslationResult{true, virtualAddress, CheckpointFailureReason::NONE, {}};
}

} // namespace riscv
