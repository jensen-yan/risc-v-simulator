#include "system/sv39_page_walker.h"

#include "common/types.h"
#include "core/memory.h"
#include "system/privilege_state.h"

#include <array>
#include <utility>

namespace riscv {

namespace {

constexpr uint64_t kPageShift = 12;
constexpr uint64_t kPageSize = 1ULL << kPageShift;
constexpr uint64_t kPageOffsetMask = kPageSize - 1;
constexpr uint64_t kVpnMask = 0x1FF;
constexpr uint64_t kSatpPpnMask = (1ULL << 44) - 1;
constexpr uint64_t kPteV = 1ULL << 0;
constexpr uint64_t kPteR = 1ULL << 1;
constexpr uint64_t kPteW = 1ULL << 2;
constexpr uint64_t kPteX = 1ULL << 3;
constexpr uint64_t kPteU = 1ULL << 4;
constexpr uint64_t kPteA = 1ULL << 6;
constexpr uint64_t kPteD = 1ULL << 7;

bool isLeafPte(uint64_t pte) {
    return (pte & (kPteR | kPteX)) != 0;
}

TranslationResult failure(TranslationFailureReason reason, std::string message) {
    return TranslationResult{false, 0, reason, std::move(message)};
}

TranslationResult accessFault(std::string message) {
    return failure(TranslationFailureReason::AccessFault, std::move(message));
}

TranslationResult readPte(std::shared_ptr<Memory> memory, Address pteAddress, uint64_t& pteValue) {
    try {
        pteValue = memory->read64(pteAddress);
        return TranslationResult{true, 0, TranslationFailureReason::None, {}};
    } catch (const MemoryException& e) {
        return accessFault(e.what());
    }
}

TranslationResult writePte(std::shared_ptr<Memory> memory, Address pteAddress, uint64_t pteValue) {
    try {
        memory->write64(pteAddress, pteValue);
        return TranslationResult{true, 0, TranslationFailureReason::None, {}};
    } catch (const MemoryException& e) {
        return accessFault(e.what());
    }
}

} // namespace

Sv39PageWalker::Sv39PageWalker(std::shared_ptr<Memory> memory, PrivilegeState* privilegeState)
    : memory_(std::move(memory)), privilege_state_(privilegeState) {}

TranslationResult Sv39PageWalker::walk(Address virtualAddress, MemoryAccessType accessType, size_t accessSize) const {
    if (memory_ == nullptr || privilege_state_ == nullptr) {
        return failure(TranslationFailureReason::AccessFault, "missing translation dependencies");
    }

    const uint64_t pageOffset = virtualAddress & kPageOffsetMask;
    if (pageOffset + accessSize > kPageSize) {
        return failure(TranslationFailureReason::AccessFault, "cross-page access is not supported");
    }

    const std::array<uint64_t, 3> vpn = {
        (virtualAddress >> 12) & kVpnMask,
        (virtualAddress >> 21) & kVpnMask,
        (virtualAddress >> 30) & kVpnMask,
    };

    uint64_t tableAddress = (privilege_state_->getSatp() & kSatpPpnMask) << kPageShift;

    for (int level = 2; level >= 0; --level) {
        const Address pteAddress = tableAddress + vpn[static_cast<size_t>(level)] * sizeof(uint64_t);
        uint64_t pte = 0;
        const TranslationResult readResult = readPte(memory_, pteAddress, pte);
        if (!readResult.success) {
            return readResult;
        }

        if ((pte & kPteV) == 0 || ((pte & kPteR) == 0 && (pte & kPteW) != 0)) {
            return failure(TranslationFailureReason::InvalidPte, "invalid Sv39 PTE");
        }

        if (!isLeafPte(pte)) {
            if (level == 0) {
                return failure(TranslationFailureReason::InvalidPte, "non-leaf PTE at final level");
            }

            tableAddress = (pte >> 10) << kPageShift;
            continue;
        }

        if (level != 0) {
            return failure(TranslationFailureReason::UnsupportedPageSize, "only 4 KiB Sv39 leaf pages are supported");
        }

        const bool userPage = (pte & kPteU) != 0;
        if (privilege_state_->getMode() == PrivilegeMode::USER && !userPage) {
            return failure(TranslationFailureReason::PermissionDenied, "user access to supervisor page");
        }

        if (accessType == MemoryAccessType::InstructionFetch && (pte & kPteX) == 0) {
            return failure(TranslationFailureReason::PermissionDenied, "instruction fetch requires execute permission");
        }
        if (accessType == MemoryAccessType::Load && (pte & kPteR) == 0) {
            return failure(TranslationFailureReason::PermissionDenied, "load requires read permission");
        }
        if (accessType == MemoryAccessType::Store && (pte & kPteW) == 0) {
            return failure(TranslationFailureReason::PermissionDenied, "store requires write permission");
        }

        uint64_t updatedPte = pte | kPteA;
        if (accessType == MemoryAccessType::Store) {
            updatedPte |= kPteD;
        }
        if (updatedPte != pte) {
            const TranslationResult writeResult = writePte(memory_, pteAddress, updatedPte);
            if (!writeResult.success) {
                return writeResult;
            }
        }

        const Address physicalAddress = ((pte >> 10) << kPageShift) | pageOffset;
        return TranslationResult{true, physicalAddress, TranslationFailureReason::None, {}};
    }

    return failure(TranslationFailureReason::InvalidPte, "page walk terminated unexpectedly");
}

} // namespace riscv
