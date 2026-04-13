#include "system/address_translation.h"

#include "system/privilege_state.h"
#include "system/sv39_page_walker.h"

namespace riscv {

AddressTranslation::AddressTranslation(std::shared_ptr<Memory> memory, PrivilegeState* privilegeState)
    : memory_(std::move(memory)), privilege_state_(privilegeState) {}

TranslationResult AddressTranslation::translateInstructionAddress(Address virtualAddress, size_t size) const {
    return translate(virtualAddress, size, MemoryAccessType::InstructionFetch);
}

TranslationResult AddressTranslation::translateLoadAddress(Address virtualAddress, size_t size) const {
    return translate(virtualAddress, size, MemoryAccessType::Load);
}

TranslationResult AddressTranslation::translateStoreAddress(Address virtualAddress, size_t size) const {
    return translate(virtualAddress, size, MemoryAccessType::Store);
}

TranslationResult AddressTranslation::translate(Address virtualAddress, size_t size, MemoryAccessType accessType) const {
    if (memory_ == nullptr) {
        return TranslationResult{false, 0, TranslationFailureReason::AccessFault, "memory is null"};
    }

    if (privilege_state_ == nullptr) {
        return TranslationResult{false, 0, TranslationFailureReason::AccessFault, "privilege state is null"};
    }

    if (size == 0) {
        return TranslationResult{false, 0, TranslationFailureReason::AccessFault, "access size must be non-zero"};
    }

    if (privilege_state_->getMode() == PrivilegeMode::MACHINE) {
        return TranslationResult{true, virtualAddress, TranslationFailureReason::None, {}};
    }

    const uint64_t satp = privilege_state_->getSatp();
    if (satp == 0) {
        return TranslationResult{true, virtualAddress, TranslationFailureReason::None, {}};
    }

    const uint64_t mode = satp >> 60;
    if (mode != 8) {
        return TranslationResult{false, 0, TranslationFailureReason::UnsupportedMode, "unsupported satp mode"};
    }

    Sv39PageWalker walker(memory_, privilege_state_);
    return walker.walk(virtualAddress, accessType, size);
}

} // namespace riscv
