#pragma once

#include "common/types.h"

#include <optional>

namespace riscv {

std::optional<PrivilegeMode> decodePrivilegeModeEncoding(uint64_t encoded_mode);
std::optional<PrivilegeMode> decodePrivilegeModeFromMstatusMpp(uint64_t mstatus);
std::optional<PrivilegeMode> applyMretPrivilegeMode(uint64_t& mstatus);

class PrivilegeState {
public:
    void setMode(PrivilegeMode mode) { mode_ = mode; }
    PrivilegeMode getMode() const { return mode_; }

    void setSatp(uint64_t satp) { satp_ = satp; }
    uint64_t getSatp() const { return satp_; }

    void setMstatus(uint64_t mstatus) { mstatus_ = mstatus; }
    uint64_t getMstatus() const { return mstatus_; }

    std::optional<PrivilegeMode> getEffectiveDataMode() const;
    bool isSumEnabled() const;

private:
    PrivilegeMode mode_ = PrivilegeMode::MACHINE;
    uint64_t satp_ = 0;
    uint64_t mstatus_ = 0;
};

} // namespace riscv
