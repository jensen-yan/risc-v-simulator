#pragma once

#include "common/types.h"
#include <functional>
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
    using ExternalWriteObserver = std::function<void(Address, size_t)>;
    using ExternalWriteObserverId = uint64_t;

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

    // 外部写入（设备/宿主等绕过CPU缓存路径写内存）。
    // 与普通 write* 的区别：会触发外部写通知，供cache做一致性处理。
    void writeByteExternal(Address addr, uint8_t value);
    void writeHalfWordExternal(Address addr, uint16_t value);
    void writeWordExternal(Address addr, uint32_t value);
    void write64External(Address addr, uint64_t value);
    
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
    Address getTohostAddr() const { return tohost_addr_; }
    Address getFromhostAddr() const { return fromhost_addr_; }

    // 外部写观察者（用于cache一致性失效等）。
    // 约定：回调应轻量且不可再回写Memory，避免递归通知。
    ExternalWriteObserverId addExternalWriteObserver(ExternalWriteObserver observer);
    void removeExternalWriteObserver(ExternalWriteObserverId id);
    void clearExternalWriteObservers();
    
private:
    struct ExternalWriteObserverEntry {
        ExternalWriteObserverId id;
        ExternalWriteObserver callback;
    };

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

    std::vector<ExternalWriteObserverEntry> external_write_observers_;
    ExternalWriteObserverId next_external_write_observer_id_ = 1;
    
    void checkAddress(Address addr, size_t accessSize) const;
    void notifyExternalWrite(Address addr, size_t accessSize);
    void writeByteRaw(Address addr, uint8_t value);
    void writeHalfWordRaw(Address addr, uint16_t value);
    void writeWordRaw(Address addr, uint32_t value);
    void write64Raw(Address addr, uint64_t value);
    void handleTohost(uint64_t value);
    void processSyscall(Address magic_mem_addr);
};

} // namespace riscv
