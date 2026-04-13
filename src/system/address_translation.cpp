#include "system/address_translation.h"

#include "core/csr_utils.h"
#include "system/privilege_state.h"
#include "system/sv39_page_walker.h"

namespace riscv {

uint64_t TranslationException::trapCause() const {
    const bool page_fault = failure_reason_ != TranslationFailureReason::AccessFault;
    switch (access_type_) {
        case MemoryAccessType::InstructionFetch:
            return page_fault ? csr::kInstructionPageFaultCause : csr::kInstructionAccessFaultCause;
        case MemoryAccessType::Load:
            return page_fault ? csr::kLoadPageFaultCause : csr::kLoadAccessFaultCause;
        case MemoryAccessType::Store:
        default:
            return page_fault ? csr::kStorePageFaultCause : csr::kStoreAccessFaultCause;
    }
}

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

    PrivilegeMode effectiveMode = privilege_state_->getMode();
    if (accessType != MemoryAccessType::InstructionFetch) {
        const auto effectiveDataMode = privilege_state_->getEffectiveDataMode();
        if (!effectiveDataMode.has_value()) {
            return TranslationResult{false, 0, TranslationFailureReason::AccessFault, "invalid mstatus.MPP"};
        }
        effectiveMode = *effectiveDataMode;
    }

    if (effectiveMode == PrivilegeMode::MACHINE) {
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
    return walker.walk(virtualAddress, accessType, size, effectiveMode);
}

} // namespace riscv
