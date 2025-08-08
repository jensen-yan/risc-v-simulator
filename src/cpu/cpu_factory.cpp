#include "common/cpu_interface.h"
#include "cpu/inorder/cpu.h"
#include "cpu/ooo/ooo_cpu.h"
#include <stdexcept>

namespace riscv {

/**
 * 顺序执行CPU适配器
 * 将原有的CPU类适配到ICpuInterface接口
 */
class InOrderCpuAdapter : public ICpuInterface {
private:
    std::unique_ptr<CPU> cpu_;

public:
    explicit InOrderCpuAdapter(std::shared_ptr<Memory> memory) 
        : cpu_(std::make_unique<CPU>(memory)) {}
    
    void step() override { cpu_->step(); }
    void run() override { cpu_->run(); }
    void reset() override { cpu_->reset(); }
    
    uint64_t getRegister(RegNum reg) const override { return cpu_->getRegister(reg); }
    void setRegister(RegNum reg, uint64_t value) override { cpu_->setRegister(reg, value); }
    
    uint64_t getFPRegister(RegNum reg) const override { return cpu_->getFPRegister(reg); }
    void setFPRegister(RegNum reg, uint64_t value) override { cpu_->setFPRegister(reg, value); }
    float getFPRegisterFloat(RegNum reg) const override { return cpu_->getFPRegisterFloat(reg); }
    void setFPRegisterFloat(RegNum reg, float value) override { cpu_->setFPRegisterFloat(reg, value); }
    double getFPRegisterDouble(RegNum reg) const override { return cpu_->getFPRegisterDouble(reg); }
    void setFPRegisterDouble(RegNum reg, double value) override { cpu_->setFPRegisterDouble(reg, value); }
    
    uint64_t getCSR(uint16_t csr_addr) const override { return cpu_->getCSR(csr_addr); }
    void setCSR(uint16_t csr_addr, uint64_t value) override { cpu_->setCSR(csr_addr, value); }
    
    uint64_t getPC() const override { return cpu_->getPC(); }
    void setPC(uint64_t pc) override { cpu_->setPC(pc); }
    
    bool isHalted() const override { return cpu_->isHalted(); }
    uint64_t getInstructionCount() const override { return cpu_->getInstructionCount(); }
    
    void setEnabledExtensions(uint32_t extensions) override { cpu_->setEnabledExtensions(extensions); }
    uint32_t getEnabledExtensions() const override { return cpu_->getEnabledExtensions(); }
    
    void dumpRegisters() const override { cpu_->dumpRegisters(); }
    void dumpState() const override { cpu_->dumpState(); }
};

/**
 * 乱序执行CPU适配器
 * 将OutOfOrderCPU类适配到ICpuInterface接口
 */
class OutOfOrderCpuAdapter : public ICpuInterface {
private:
    std::unique_ptr<OutOfOrderCPU> cpu_;

public:
    explicit OutOfOrderCpuAdapter(std::shared_ptr<Memory> memory) 
        : cpu_(std::make_unique<OutOfOrderCPU>(memory)) {}
    
    void step() override { cpu_->step(); }
    void run() override { cpu_->run(); }
    void reset() override { cpu_->reset(); }
    
    uint64_t getRegister(RegNum reg) const override { return cpu_->getRegister(reg); }
    void setRegister(RegNum reg, uint64_t value) override { cpu_->setRegister(reg, value); }
    
    uint64_t getFPRegister(RegNum reg) const override { return cpu_->getFPRegister(reg); }
    void setFPRegister(RegNum reg, uint64_t value) override { cpu_->setFPRegister(reg, value); }
    float getFPRegisterFloat(RegNum reg) const override { return cpu_->getFPRegisterFloat(reg); }
    void setFPRegisterFloat(RegNum reg, float value) override { cpu_->setFPRegisterFloat(reg, value); }
    double getFPRegisterDouble(RegNum reg) const override { return cpu_->getFPRegisterDouble(reg); }
    void setFPRegisterDouble(RegNum reg, double value) override { cpu_->setFPRegisterDouble(reg, value); }
    
    uint64_t getCSR(uint16_t csr_addr) const override { return cpu_->getCSR(csr_addr); }
    void setCSR(uint16_t csr_addr, uint64_t value) override { cpu_->setCSR(csr_addr, value); }
    
    uint64_t getPC() const override { return cpu_->getPC(); }
    void setPC(uint64_t pc) override { cpu_->setPC(pc); }
    
    bool isHalted() const override { return cpu_->isHalted(); }
    uint64_t getInstructionCount() const override { return cpu_->getInstructionCount(); }
    
    void setEnabledExtensions(uint32_t extensions) override { cpu_->setEnabledExtensions(extensions); }
    uint32_t getEnabledExtensions() const override { return cpu_->getEnabledExtensions(); }
    
    void dumpRegisters() const override { cpu_->dumpRegisters(); }
    void dumpState() const override { cpu_->dumpState(); }
    
    // DiffTest功能重写
    void setDiffTest(class DiffTest* difftest) override { cpu_->setDiffTest(difftest); }
    void performDiffTestWithCommittedPC(uint64_t committed_pc) override { cpu_->performDiffTestWithCommittedPC(committed_pc); }
    bool isDiffTestEnabled() const override { return cpu_->isDiffTestEnabled(); }
    
    // 乱序执行CPU特有的功能
    OutOfOrderCPU* getOooCpu() { return cpu_.get(); }
};

std::unique_ptr<ICpuInterface> CpuFactory::createCpu(CpuType type, std::shared_ptr<Memory> memory) {
    switch (type) {
        case CpuType::IN_ORDER:
            return std::make_unique<InOrderCpuAdapter>(memory);
        case CpuType::OUT_OF_ORDER:
            return std::make_unique<OutOfOrderCpuAdapter>(memory);
        default:
            throw std::runtime_error("不支持的CPU类型");
    }
}

} // namespace riscv