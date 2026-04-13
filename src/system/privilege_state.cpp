#include "system/privilege_state.h"

namespace riscv {

namespace {

constexpr uint64_t kMstatusMieMask = 1ULL << 3;
constexpr uint64_t kMstatusMpieMask = 1ULL << 7;
constexpr uint64_t kMstatusMppMask = 0x3ULL << 11;
constexpr uint64_t kMstatusMprvMask = 1ULL << 17;
constexpr uint64_t kMstatusSumMask = 1ULL << 18;

} // namespace

std::optional<PrivilegeMode> decodePrivilegeModeEncoding(uint64_t encoded_mode) {
    switch (encoded_mode & 0x3ULL) {
        case 0:
            return PrivilegeMode::USER;
        case 1:
            return PrivilegeMode::SUPERVISOR;
        case 3:
            return PrivilegeMode::MACHINE;
        default:
            return std::nullopt;
    }
}

std::optional<PrivilegeMode> decodePrivilegeModeFromMstatusMpp(uint64_t mstatus) {
    return decodePrivilegeModeEncoding((mstatus >> 11) & 0x3ULL);
}

std::optional<PrivilegeMode> applyMretPrivilegeMode(uint64_t& mstatus) {
    const auto restored_mode = decodePrivilegeModeFromMstatusMpp(mstatus);
    if (!restored_mode.has_value()) {
        return std::nullopt;
    }

    const uint64_t restored_mie = (mstatus & kMstatusMpieMask) != 0 ? kMstatusMieMask : 0ULL;
    mstatus = (mstatus & ~kMstatusMieMask) | restored_mie;
    mstatus |= kMstatusMpieMask;
    mstatus &= ~kMstatusMppMask;
    return restored_mode;
}

std::optional<PrivilegeMode> PrivilegeState::getEffectiveDataMode() const {
    if (mode_ != PrivilegeMode::MACHINE || (mstatus_ & kMstatusMprvMask) == 0) {
        return mode_;
    }
    return decodePrivilegeModeFromMstatusMpp(mstatus_);
}

bool PrivilegeState::isSumEnabled() const {
    return (mstatus_ & kMstatusSumMask) != 0;
}

} // namespace riscv
