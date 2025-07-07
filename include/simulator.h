#pragma once

#include "cpu.h"
#include "memory.h"
#include <string>
#include <memory>

namespace riscv {

/**
 * RISC-V 模拟器主类
 * 统一管理CPU和内存，提供外部接口
 */
class Simulator {
public:
    explicit Simulator(size_t memorySize = Memory::DEFAULT_SIZE);
    ~Simulator() = default;
    
    // 禁用拷贝构造和赋值
    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;
    
    // 程序管理
    bool loadProgram(const std::string& filename);
    bool loadProgramFromBytes(const std::vector<uint8_t>& program, Address startAddr = 0);
    
    // 执行控制
    void step();                    // 单步执行
    void run();                     // 运行到结束
    void reset();                   // 重置模拟器
    
    // 状态访问
    uint32_t getRegister(RegNum reg) const;
    void setRegister(RegNum reg, uint32_t value);
    uint32_t getPC() const;
    void setPC(uint32_t pc);
    
    // 内存访问
    uint8_t readMemoryByte(Address addr) const;
    uint32_t readMemoryWord(Address addr) const;
    void writeMemoryByte(Address addr, uint8_t value);
    void writeMemoryWord(Address addr, uint32_t value);
    
    // 状态查询
    bool isHalted() const;
    uint64_t getInstructionCount() const;
    
    // 调试功能
    void dumpRegisters() const;
    void dumpMemory(Address startAddr, size_t length) const;
    void dumpState() const;
    
    // 统计信息
    void printStatistics() const;
    
private:
    std::shared_ptr<Memory> memory_;
    std::unique_ptr<CPU> cpu_;
    
    // 辅助方法
    std::vector<uint8_t> loadBinaryFile(const std::string& filename);
};

} // namespace riscv