#pragma once

#include "common/types.h"
#include <vector>
#include <memory>
#include <cstdlib>

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
    
    // 双字访问（64位）
    uint64_t read64(Address addr) const;
    void write64(Address addr, uint64_t value);
    
    // 指令获取
    Instruction fetchInstruction(Address addr) const;
    
    // 内存管理
    void clear();
    size_t getSize() const { return memory_size_; }
    
    // 加载程序到内存
    void loadProgram(const std::vector<uint8_t>& program, Address startAddr = 0);
    
    // 调试功能
    void dump(Address startAddr, size_t length) const;
    
    // tohost/fromhost 机制支持
    bool shouldExit() const { return should_exit_; }
    int getExitCode() const { return exit_code_; }
    void resetExitStatus() { should_exit_ = false; exit_code_ = 0; }
    void setHostCommAddresses(Address tohostAddr, Address fromhostAddr);
    
private:
    std::unique_ptr<uint8_t, decltype(&std::free)> memory_;
    size_t memory_size_;
    
    // tohost/fromhost 特殊地址（默认值可被ELF环境覆盖）
    static constexpr Address DEFAULT_TOHOST_ADDR = 0x80001000;
    static constexpr Address DEFAULT_FROMHOST_ADDR = 0x80001040;
    Address tohost_addr_ = DEFAULT_TOHOST_ADDR;
    Address fromhost_addr_ = DEFAULT_FROMHOST_ADDR;
    
    // 程序退出状态
    bool should_exit_ = false;
    int exit_code_ = 0;
    
    void checkAddress(Address addr, size_t accessSize) const;
    void handleTohost(uint64_t value);
    void processSyscall(Address magic_mem_addr);
};

} // namespace riscv
