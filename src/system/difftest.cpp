#include "system/difftest.h"
#include "common/cpu_interface.h"
#include "cpu/ooo/ooo_cpu.h"
#include "common/debug_types.h"

namespace riscv {

DiffTest::DiffTest(std::shared_ptr<Memory> memory) 
    : enabled_(true), stop_on_mismatch_(true), comparison_count_(0), mismatch_count_(0) {
    
    // 创建参考的顺序CPU
    reference_cpu_ = CpuFactory::createCpu(CpuType::IN_ORDER, memory);
    
    dprintf(DIFFTEST, "初始化完成，参考CPU已创建");
}

DiffTest::~DiffTest() = default;

void DiffTest::setReferencePC(uint64_t pc) {
    if (!enabled_ || !reference_cpu_) {
        return;
    }
    
    reference_cpu_->setPC(pc);
    dprintf(DIFFTEST, "设置参考CPU PC=0x%lx", pc);
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
        uint32_t value = ooo_cpu->getRegister(reg);
        reference_cpu_->setRegister(reg, value);
    }
    
    // 同步浮点寄存器状态
    for (RegNum reg = 0; reg < 32; reg++) {
        uint32_t value = ooo_cpu->getFPRegister(reg);
        reference_cpu_->setFPRegister(reg, value);
    }
    
}

bool DiffTest::stepAndCompareWithCommittedPC(ICpuInterface* ooo_cpu, uint32_t committed_pc) {
    if (!enabled_ || !reference_cpu_ || !ooo_cpu) {
        return true;
    }
    
    comparison_count_++;
    
    // 步骤1: 预检查PC是否一致（使用提供的committed_pc）
    uint32_t ref_pc = reference_cpu_->getPC();
    
    if (ref_pc != committed_pc) {
        dprintf(DIFFTEST, "[PC_MISMATCH] PC预检查失败！提交PC不一致: 参考CPU=0x%x, 乱序CPU提交PC=0x%x", 
                ref_pc, committed_pc);
        
        mismatch_count_++;
        dumpState(reference_cpu_.get(), ooo_cpu);
        return false;
    }
    
    // 步骤2: PC一致，让参考CPU执行一条指令
    reference_cpu_->step();
    
    // 步骤3: 比较执行后的寄存器状态（不比较PC，因为执行后PC会不同）
    bool all_match = true;
    
    // 比较通用寄存器
    if (!compareRegisters(ooo_cpu)) {
        all_match = false;
    }
    
    // 比较浮点寄存器
    if (!compareFPRegisters(ooo_cpu)) {
        all_match = false;
    }
    
    if (!all_match) {
        mismatch_count_++;
        dprintf(DIFFTEST, "发现状态不一致！比较次数: %lu, 总不一致次数: %lu", comparison_count_, mismatch_count_);
        
        // 转储详细状态信息
        dumpState(reference_cpu_.get(), ooo_cpu);
        
        if (stop_on_mismatch_) {
            dprintf(DIFFTEST, "检测到不一致，但继续执行以便观察（调试模式）");
            // 临时禁用自动退出，便于调试
            std::exit(1);
        }
    } else {
        dprintf(DIFFTEST, "比较 #%lu 通过", comparison_count_);
    }
    
    return all_match;
}

void DiffTest::reset() {
    comparison_count_ = 0;
    mismatch_count_ = 0;
    
    if (reference_cpu_) {
        reference_cpu_->reset();
    }
    
    dprintf(DIFFTEST, "已重置统计信息和参考CPU状态");
}

DiffTest::Statistics DiffTest::getStatistics() const {
    return {comparison_count_, mismatch_count_};
}

bool DiffTest::compareRegisters(ICpuInterface* ooo_cpu) {
    bool all_match = true;
    
    for (RegNum reg = 1; reg < 32; reg++) {  // 跳过x0，始终为0
        uint32_t ref_value = reference_cpu_->getRegister(reg);
        uint32_t ooo_value = ooo_cpu->getRegister(reg);
        
        if (ref_value != ooo_value) {
            dprintf(DIFFTEST, "寄存器x%u不一致: 参考CPU=0x%x, 乱序CPU=0x%x", reg, ref_value, ooo_value);
            all_match = false;
        }
    }
    
    return all_match;
}

bool DiffTest::compareFPRegisters(ICpuInterface* ooo_cpu) {
    bool all_match = true;
    
    for (RegNum reg = 0; reg < 32; reg++) {
        uint32_t ref_value = reference_cpu_->getFPRegister(reg);
        uint32_t ooo_value = ooo_cpu->getFPRegister(reg);
        
        if (ref_value != ooo_value) {
            dprintf(DIFFTEST, "浮点寄存器f%u不一致: 参考CPU=0x%x, 乱序CPU=0x%x", reg, ref_value, ooo_value);
            all_match = false;
        }
    }
    
    return all_match;
}

void DiffTest::dumpState(ICpuInterface* ref_cpu, ICpuInterface* ooo_cpu) {
    // 转储PC
    uint32_t ref_pc = ref_cpu->getPC();
    uint32_t ooo_committed_pc = static_cast<OutOfOrderCPU*>(ooo_cpu)->getCommittedPC();
    
    dprintf(DIFFTEST, "[STATE_DUMP] PC状态: 参考CPU=0x%x, 乱序提交PC=0x%x", 
            ref_pc, ooo_committed_pc);
    
    // 转储部分关键寄存器
    dprintf(DIFFTEST, "[STATE_DUMP] 寄存器状态 (只显示非零寄存器):");
    
    for (RegNum reg = 1; reg < 32; reg++) {
        uint32_t ref_value = ref_cpu->getRegister(reg);
        uint32_t ooo_value = ooo_cpu->getRegister(reg);
        
        if (ref_value != 0 || ooo_value != 0) {
            dprintf(DIFFTEST, "[STATE_DUMP] x%u\t0x%x\t0x%x", reg, ref_value, ooo_value);
        }
    }
    
    dprintf(DIFFTEST, "[STATE_DUMP] ================================================\n");
}

} // namespace riscv 