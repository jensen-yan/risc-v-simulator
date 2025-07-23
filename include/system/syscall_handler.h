#pragma once

#include "common/types.h"
#include <memory>
#include <string>

namespace riscv {

class Memory;
class ICpuInterface;

/**
 * 系统调用处理器
 * 实现基本的系统调用支持，包括I/O操作
 */
class SyscallHandler {
public:
    // 系统调用号定义（类似Linux ABI）
    enum SyscallNumber {
        SYS_EXIT = 93,      // 退出程序
        SYS_WRITE = 64,     // 写入数据到文件描述符
        SYS_READ = 63,      // 从文件描述符读取数据
        SYS_BRK = 214,      // 设置程序断点（内存管理）
        SYS_GETTIMEOFDAY = 169  // 获取时间（可选）
    };

    // 文件描述符
    enum FileDescriptor {
        STDIN = 0,
        STDOUT = 1,
        STDERR = 2
    };

    SyscallHandler(std::shared_ptr<Memory> memory);
    ~SyscallHandler() = default;

    /**
     * 处理系统调用
     * @param cpu CPU实例，用于访问寄存器
     * @return 是否需要停机
     */
    bool handleSyscall(ICpuInterface* cpu);

private:
    std::shared_ptr<Memory> memory_;
    
    // 系统调用实现
    void handleExit(ICpuInterface* cpu);
    void handleWrite(ICpuInterface* cpu);
    void handleRead(ICpuInterface* cpu);
    void handleBrk(ICpuInterface* cpu);
    
    // 辅助方法
    std::string readStringFromMemory(Address addr, size_t maxLen = 1024);
    void writeStringToMemory(Address addr, const std::string& str);
    
    // 调试输出
    void printSyscallInfo(uint64_t syscallNum, uint64_t a0, uint64_t a1, uint64_t a2) const;
};

} // namespace riscv