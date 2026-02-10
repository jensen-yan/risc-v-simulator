#include "system/difftest.h"
#include "common/cpu_interface.h"
#include "cpu/ooo/ooo_cpu.h"
#include "common/debug_types.h"
#include <array>

namespace riscv {

namespace {
constexpr std::array<uint32_t, 8> kKeyCsrAddrs = {
    0x300,  // mstatus
    0x305,  // mtvec
    0x341,  // mepc
    0x342,  // mcause
    0x343,  // mtval
    0x344,  // mip
    0x340,  // mscratch
    0xF14,  // mhartid
};
}  // namespace

DiffTest::DiffTest(ICpuInterface* main_cpu, ICpuInterface* reference_cpu) 
    : main_cpu_(main_cpu), reference_cpu_(reference_cpu),
      enabled_(true), stop_on_mismatch_(true), comparison_count_(0), mismatch_count_(0) {
    
    if (!main_cpu_ || !reference_cpu_) {
        throw std::invalid_argument("DiffTest: main CPU and reference CPU must not be null");
    }
    
    LOGI(DIFFTEST, "difftest initialized");
}

DiffTest::~DiffTest() = default;

void DiffTest::setReferencePC(uint64_t pc) {
    if (!enabled_ || !reference_cpu_) {
        return;
    }
    
    reference_cpu_->setPC(pc);
}

/**
 * 同步参考CPU的寄存器状态到与乱序CPU一致
 */
void DiffTest::syncReferenceState(ICpuInterface* ooo_cpu) {
    if (!enabled_ || !reference_cpu_ || !ooo_cpu) {
        return;
    }
    
    // 同步所有寄存器状态
    for (RegNum reg = 0; reg < 32; reg++) {
        uint64_t value = ooo_cpu->getRegister(reg);
        reference_cpu_->setRegister(reg, value);
    }
    
    // 同步浮点寄存器状态
    for (RegNum reg = 0; reg < 32; reg++) {
        uint64_t value = ooo_cpu->getFPRegister(reg);
        reference_cpu_->setFPRegister(reg, value);
    }

    // 同步关键CSR状态，避免参考核和主核在系统指令后起始状态漂移
    for (uint32_t csr_addr : kKeyCsrAddrs) {
        reference_cpu_->setCSR(csr_addr, ooo_cpu->getCSR(csr_addr));
    }
}

bool DiffTest::stepAndCompareWithCommittedPC(ICpuInterface* ooo_cpu, uint64_t committed_pc) {
    if (!enabled_ || !reference_cpu_ || !ooo_cpu) {
        return true;
    }
    
    comparison_count_++;
    
    // 确保参考CPU执行到与乱序CPU相同的指令
    uint64_t ref_pc = reference_cpu_->getPC();
    
    if (ref_pc != committed_pc) {
        mismatch_count_++;
        LOGE(DIFFTEST, "pc mismatch: ref=0x%" PRIx64 ", ooo=0x%" PRIx64, ref_pc, committed_pc);
        dumpState(reference_cpu_, ooo_cpu);
        return false;
    }
    
    // 让参考CPU执行这条指令
    reference_cpu_->step();
    
    const bool gpr_match = compareRegisters(ooo_cpu);
    const bool fpr_match = compareFPRegisters(ooo_cpu);
    const bool csr_match = compareCSRRegisters(ooo_cpu);
    if (!(gpr_match && fpr_match && csr_match)) {
        mismatch_count_++;
        dumpState(reference_cpu_, ooo_cpu);
        
        if (stop_on_mismatch_) {
            std::exit(1);
        }
        return false;
    }
    
    return true;
}

void DiffTest::reset() {
    comparison_count_ = 0;
    mismatch_count_ = 0;
    
    if (reference_cpu_) {
        reference_cpu_->reset();
    }
}

DiffTest::Statistics DiffTest::getStatistics() const {
    return {comparison_count_, mismatch_count_};
}

bool DiffTest::compareRegisters(ICpuInterface* ooo_cpu) {
    for (RegNum reg = 1; reg < 32; reg++) {  // 跳过x0，始终为0
        uint64_t ref_value = reference_cpu_->getRegister(reg);
        uint64_t ooo_value = ooo_cpu->getRegister(reg);
        
        if (ref_value != ooo_value) {
            return false;
        }
    }
    
    return true;
}

bool DiffTest::compareFPRegisters(ICpuInterface* ooo_cpu) {
    for (RegNum reg = 0; reg < 32; reg++) {
        uint64_t ref_value = reference_cpu_->getFPRegister(reg);
        uint64_t ooo_value = ooo_cpu->getFPRegister(reg);
        
        if (ref_value != ooo_value) {
            return false;
        }
    }
    
    return true;
}

bool DiffTest::compareCSRRegisters(ICpuInterface* ooo_cpu) {
    for (uint32_t csr_addr : kKeyCsrAddrs) {
        const uint64_t ref_value = reference_cpu_->getCSR(csr_addr);
        const uint64_t ooo_value = ooo_cpu->getCSR(csr_addr);
        if (ref_value != ooo_value) {
            return false;
        }
    }
    return true;
}

void DiffTest::dumpState(ICpuInterface* ref_cpu, ICpuInterface* ooo_cpu) {
    // 转储关键寄存器状态用于调试
    for (RegNum reg = 1; reg < 32; reg++) {
        uint64_t ref_value = ref_cpu->getRegister(reg);
        uint64_t ooo_value = ooo_cpu->getRegister(reg);
        
        if (ref_value != ooo_value) {
            LOGE(DIFFTEST, "register mismatch x%u: ref=0x%" PRIx64 ", ooo=0x%" PRIx64, reg, ref_value, ooo_value);
        }
    }

    for (RegNum reg = 0; reg < 32; reg++) {
        const uint64_t ref_value = ref_cpu->getFPRegister(reg);
        const uint64_t ooo_value = ooo_cpu->getFPRegister(reg);
        if (ref_value != ooo_value) {
            LOGE(DIFFTEST, "fp register mismatch f%u: ref=0x%" PRIx64 ", ooo=0x%" PRIx64,
                 reg, ref_value, ooo_value);
        }
    }

    for (uint32_t csr_addr : kKeyCsrAddrs) {
        const uint64_t ref_value = ref_cpu->getCSR(csr_addr);
        const uint64_t ooo_value = ooo_cpu->getCSR(csr_addr);
        if (ref_value != ooo_value) {
            LOGE(DIFFTEST, "csr mismatch [0x%03x]: ref=0x%" PRIx64 ", ooo=0x%" PRIx64,
                 csr_addr, ref_value, ooo_value);
        }
    }
}

} // namespace riscv 
