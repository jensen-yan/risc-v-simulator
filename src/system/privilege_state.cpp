#include "system/privilege_state.h"

namespace riscv {

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

} // namespace riscv
