#pragma once

#include "common/types.h"

namespace riscv {

class PrivilegeState {
public:
    void setMode(PrivilegeMode mode) { mode_ = mode; }
    PrivilegeMode getMode() const { return mode_; }

    void setSatp(uint64_t satp) { satp_ = satp; }
    uint64_t getSatp() const { return satp_; }

private:
    PrivilegeMode mode_ = PrivilegeMode::MACHINE;
    uint64_t satp_ = 0;
};

} // namespace riscv
