#pragma once

#include "common/types.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace riscv::csr {

constexpr std::size_t kCsrRegisterCount = 4096;
using CsrFile = std::array<std::uint64_t, kCsrRegisterCount>;

constexpr std::uint32_t kFflags = 0x001;
constexpr std::uint32_t kFrm = 0x002;
constexpr std::uint32_t kFcsr = 0x003;

constexpr std::uint32_t kMtvec = 0x305;
constexpr std::uint32_t kMstatus = 0x300;
constexpr std::uint32_t kMepc = 0x341;
constexpr std::uint32_t kMcause = 0x342;
constexpr std::uint32_t kMtval = 0x343;

constexpr std::uint64_t kInstructionAccessFaultCause = 1;
constexpr std::uint64_t kInstructionAddressMisalignedCause = 0;
constexpr std::uint64_t kBreakpointCause = 3;
constexpr std::uint64_t kLoadAccessFaultCause = 5;
constexpr std::uint64_t kStoreAccessFaultCause = 7;
constexpr std::uint64_t kMachineEcallCause = 11;
constexpr std::uint64_t kInstructionPageFaultCause = 12;
constexpr std::uint64_t kLoadPageFaultCause = 13;
constexpr std::uint64_t kStorePageFaultCause = 15;

constexpr std::uint64_t kMstatusMieMask = 1ULL << 3;
constexpr std::uint64_t kMstatusMpieMask = 1ULL << 7;
constexpr std::uint64_t kMstatusMppMask = 0x3ULL << 11;

inline std::uint64_t read(const CsrFile& csr, std::uint32_t addr) {
    if (addr == kFflags) {
        return csr[kFcsr] & 0x1FU;
    }
    if (addr == kFrm) {
        return (csr[kFcsr] >> 5) & 0x7U;
    }
    return csr[addr];
}

inline void write(CsrFile& csr, std::uint32_t addr, std::uint64_t value) {
    if (addr == kFflags) {
        const std::uint64_t fflags = value & 0x1FU;
        csr[kFflags] = fflags;
        csr[kFcsr] = (csr[kFcsr] & ~0x1FU) | fflags;
        return;
    }

    if (addr == kFrm) {
        const std::uint64_t frm = value & 0x7U;
        csr[kFrm] = frm;
        csr[kFcsr] = (csr[kFcsr] & ~0xE0U) | (frm << 5);
        return;
    }

    if (addr == kFcsr) {
        const std::uint64_t fcsr = value & 0xFFU;
        csr[kFcsr] = fcsr;
        csr[kFflags] = fcsr & 0x1FU;
        csr[kFrm] = (fcsr >> 5) & 0x7U;
        return;
    }

    csr[addr] = value;
}

inline std::uint64_t machineTrapVectorBase(const CsrFile& csr) {
    return read(csr, kMtvec) & ~0x3ULL;
}

inline std::uint64_t encodePrivilegeMode(PrivilegeMode mode) {
    switch (mode) {
        case PrivilegeMode::USER:
            return 0;
        case PrivilegeMode::SUPERVISOR:
            return 1;
        case PrivilegeMode::MACHINE:
        default:
            return 3;
    }
}

inline std::uint64_t enterMachineTrap(CsrFile& csr,
                                      std::uint64_t instruction_pc,
                                      std::uint64_t cause,
                                      std::uint64_t tval,
                                      PrivilegeMode current_mode) {
    std::uint64_t mstatus = read(csr, kMstatus);
    const std::uint64_t mie = (mstatus & kMstatusMieMask) != 0 ? kMstatusMpieMask : 0ULL;
    mstatus = (mstatus & ~(kMstatusMieMask | kMstatusMpieMask | kMstatusMppMask)) |
              mie |
              (encodePrivilegeMode(current_mode) << 11);
    write(csr, kMstatus, mstatus);
    write(csr, kMepc, instruction_pc);
    write(csr, kMcause, cause);
    write(csr, kMtval, tval);
    return machineTrapVectorBase(csr);
}

}  // namespace riscv::csr
