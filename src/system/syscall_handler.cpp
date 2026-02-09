#include "system/syscall_handler.h"
#include "common/cpu_interface.h"
#include "common/debug_types.h"
#include "core/memory.h"
#include <iostream>
#include <cstring>

namespace riscv {

SyscallHandler::SyscallHandler(std::shared_ptr<Memory> memory) : memory_(memory) {
    uint64_t mem_size = memory_ ? static_cast<uint64_t>(memory_->getSize()) : 0;
    current_brk_ = mem_size > 4 ? mem_size / 4 : 1;
}

bool SyscallHandler::handleSyscall(ICpuInterface* cpu) {
    // RISC-V ABI: 系统调用号在 a7 (x17), 参数在 a0-a6 (x10-x16)
    uint64_t syscallNum = cpu->getRegister(17);  // a7
    uint64_t a0 = cpu->getRegister(10);  // a0
    uint64_t a1 = cpu->getRegister(11);  // a1
    uint64_t a2 = cpu->getRegister(12);  // a2
    
    LOGT(SYSCALL, "syscall=%" PRIx64 " a0=0x%" PRIx64 " a1=0x%" PRIx64 " a2=0x%" PRIx64,
         syscallNum, a0, a1, a2);
    
    switch (syscallNum) {
        case SYS_EXIT:
            handleExit(cpu);
            return true;  // 需要停机
            
        case SYS_WRITE:
            handleWrite(cpu);
            break;
            
        case SYS_READ:
            handleRead(cpu);
            break;
            
        case SYS_BRK:
            handleBrk(cpu);
            break;
            
        default:
            std::cerr << "unsupported syscall: " << syscallNum << std::endl;
            // 对于未知系统调用，返回0而不是-1，避免无限循环
            cpu->setRegister(10, 0);
            break;
    }
    
    return false;  // 不需要停机
}

void SyscallHandler::handleExit(ICpuInterface* cpu) {
    uint64_t exitCode = cpu->getRegister(10);  // a0
    
    // riscv-tests使用退出码表示测试结果
    if (exitCode == 0) {
        std::cout << "\n=== TEST RESULT: PASS ===\n";
        std::cout << "Program exited normally, code: " << exitCode << std::endl;
    } else {
        std::cout << "\n=== TEST RESULT: FAIL ===\n";
        std::cout << "Program exited with failure, code: " << exitCode << std::endl;
        
        // 如果退出码是1，通常表示测试失败但程序正常
        // 如果退出码大于1，可能表示具体的失败测试编号
        if (exitCode > 1) {
            std::cout << "Failed test index: " << exitCode << std::endl;
        }
    }
    // 不需要设置返回值，程序将停机
}

void SyscallHandler::handleWrite(ICpuInterface* cpu) {
    uint64_t fd = cpu->getRegister(10);      // a0: 文件描述符
    uint64_t bufAddr = cpu->getRegister(11); // a1: 缓冲区地址
    uint64_t count = cpu->getRegister(12);   // a2: 写入字节数
    
    try {
        if (fd == STDOUT || fd == STDERR) {
            if (count == 0) {
                cpu->setRegister(10, 0);
                return;
            }
            if (bufAddr == 0) {
                cpu->setRegister(10, static_cast<uint64_t>(-1));
                return;
            }
            // 输出到控制台
            std::string output;
            for (size_t i = 0; i < count; i++) {
                uint8_t byte = memory_->readByte(bufAddr + i);
                output += static_cast<char>(byte);
            }
            
            if (fd == STDOUT) {
                std::cout << output;
                std::cout.flush();
            } else {
                std::cerr << output;
                std::cerr.flush();
            }
            
            // 返回写入的字节数
            cpu->setRegister(10, count);
        } else {
            // 不支持的文件描述符
            LOGW(SYSCALL, "unsupported file descriptor for write: %" PRIx64, fd);
            cpu->setRegister(10, static_cast<uint64_t>(-1));
        }
    } catch (const std::exception& e) {
        LOGE(SYSCALL, "write failed: %s", e.what());
        cpu->setRegister(10, static_cast<uint64_t>(-1));
    }
}

void SyscallHandler::handleRead(ICpuInterface* cpu) {
    uint64_t fd = cpu->getRegister(10);      // a0: 文件描述符
    uint64_t bufAddr = cpu->getRegister(11); // a1: 缓冲区地址
    uint64_t count = cpu->getRegister(12);   // a2: 读取字节数
    
    if (fd == STDIN) {
        if (count == 0) {
            cpu->setRegister(10, 0);
            return;
        }
        if (bufAddr == 0) {
            cpu->setRegister(10, static_cast<uint64_t>(-1));
            return;
        }
        // 从标准输入读取（简化实现）
        std::string input;
        std::getline(std::cin, input);
        
        // 限制读取长度
        size_t readLen = std::min(static_cast<size_t>(count), input.length());
        
        // 写入到内存
        for (size_t i = 0; i < readLen; i++) {
            memory_->writeByte(bufAddr + i, static_cast<uint8_t>(input[i]));
        }
        
        // 返回读取的字节数
        cpu->setRegister(10, static_cast<uint64_t>(readLen));
    } else {
        // 不支持的文件描述符
        LOGW(SYSCALL, "unsupported file descriptor for read: %" PRIx64, fd);
        cpu->setRegister(10, static_cast<uint64_t>(-1));
    }
}

void SyscallHandler::handleBrk(ICpuInterface* cpu) {
    uint64_t addr = cpu->getRegister(10);  // a0: 新的程序断点
    
    if (addr == 0) {
        cpu->setRegister(10, current_brk_);
        return;
    }

    current_brk_ = addr;
    cpu->setRegister(10, current_brk_);
}

std::string SyscallHandler::readStringFromMemory(Address addr, size_t maxLen) {
    std::string result;
    
    for (size_t i = 0; i < maxLen; i++) {
        uint8_t byte = memory_->readByte(addr + i);
        if (byte == 0) {
            break;  // 遇到字符串结束符
        }
        result += static_cast<char>(byte);
    }
    
    return result;
}

void SyscallHandler::writeStringToMemory(Address addr, const std::string& str) {
    for (size_t i = 0; i < str.length(); i++) {
        memory_->writeByte(addr + i, static_cast<uint8_t>(str[i]));
    }
    memory_->writeByte(addr + str.length(), 0);  // 添加字符串结束符
}

void SyscallHandler::printSyscallInfo(uint64_t syscallNum, uint64_t a0, uint64_t a1, uint64_t a2) const {
    LOGT(SYSCALL, "syscall=%" PRIx64 " args: a0=0x%" PRIx64 " a1=0x%" PRIx64 " a2=0x%" PRIx64,
         syscallNum, a0, a1, a2);
}

} // namespace riscv
