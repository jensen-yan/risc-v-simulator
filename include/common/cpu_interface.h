#pragma once

#include "common/types.h"
#include <memory>

namespace riscv {

// 前向声明
class Memory;

/**
 * CPU抽象接口
 * 定义了CPU的基本功能，支持不同的CPU实现
 */
class ICpuInterface {
public:
    virtual ~ICpuInterface() = default;
    
    // 执行控制
    virtual void step() = 0;
    virtual void run() = 0;
    virtual void reset() = 0;
    
    // 寄存器访问
    virtual uint64_t getRegister(RegNum reg) const = 0;
    virtual void setRegister(RegNum reg, uint64_t value) = 0;
    
    // 浮点寄存器访问
    virtual uint32_t getFPRegister(RegNum reg) const = 0;
    virtual void setFPRegister(RegNum reg, uint32_t value) = 0;
    virtual float getFPRegisterFloat(RegNum reg) const = 0;
    virtual void setFPRegisterFloat(RegNum reg, float value) = 0;
    
    // 程序计数器
    virtual uint64_t getPC() const = 0;
    virtual void setPC(uint64_t pc) = 0;
    
    // 状态查询
    virtual bool isHalted() const = 0;
    virtual uint64_t getInstructionCount() const = 0;
    
    // 扩展支持
    virtual void setEnabledExtensions(uint32_t extensions) = 0;
    virtual uint32_t getEnabledExtensions() const = 0;
    
    // 调试功能
    virtual void dumpRegisters() const = 0;
    virtual void dumpState() const = 0;
    
    // DiffTest功能（默认实现为空，只有OOO CPU需要实现）
    virtual void setDiffTest(class DiffTest* difftest) {} // 设置DiffTest引用
    virtual void performDiffTestWithCommittedPC(uint64_t committed_pc) {}
    virtual bool isDiffTestEnabled() const { return false; }
};

/**
 * CPU工厂类
 * 用于创建不同类型的CPU实例
 */
enum class CpuType {
    IN_ORDER,    // 顺序执行CPU
    OUT_OF_ORDER // 乱序执行CPU
};

class CpuFactory {
public:
    static std::unique_ptr<ICpuInterface> createCpu(CpuType type, std::shared_ptr<Memory> memory);
};

} // namespace riscv