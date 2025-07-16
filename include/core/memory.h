#pragma once

#include "common/types.h"
#include <vector>
#include <memory>

namespace riscv {

/**
 * 内存管理类
 * 负责模拟 RISC-V 的线性内存空间
 */
class Memory {
public:
    static constexpr size_t DEFAULT_SIZE = 1 * 1024 * 1024; // 默认1MB内存
    
    explicit Memory(size_t size = DEFAULT_SIZE);
    ~Memory() = default;
    
    // 禁用拷贝构造和赋值
    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;
    
    // 字节访问
    uint8_t readByte(Address addr) const;
    void writeByte(Address addr, uint8_t value);
    
    // 半字访问（16位）
    uint16_t readHalfWord(Address addr) const;
    void writeHalfWord(Address addr, uint16_t value);
    
    // 字访问（32位）
    uint32_t readWord(Address addr) const;
    void writeWord(Address addr, uint32_t value);
    
    // 指令获取
    Instruction fetchInstruction(Address addr) const;
    
    // 内存管理
    void clear();
    size_t getSize() const { return memory_.size(); }
    
    // 加载程序到内存
    void loadProgram(const std::vector<uint8_t>& program, Address startAddr = 0);
    
    // 调试功能
    void dump(Address startAddr, size_t length) const;
    
private:
    std::vector<uint8_t> memory_;
    
    void checkAddress(Address addr, size_t accessSize) const;
};

} // namespace riscv