#include "core/memory.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <algorithm>

namespace riscv {

Memory::Memory(size_t size) : memory_(size, 0) {
    if (size == 0) {
        throw MemoryException("内存大小不能为0");
    }
}

uint8_t Memory::readByte(Address addr) const {
    checkAddress(addr, 1);
    return memory_[addr];
}

void Memory::writeByte(Address addr, uint8_t value) {
    checkAddress(addr, 1);
    memory_[addr] = value;
}

uint16_t Memory::readHalfWord(Address addr) const {
    checkAddress(addr, 2);
    
    // 小端序读取
    uint16_t result = memory_[addr];
    result |= static_cast<uint16_t>(memory_[addr + 1]) << 8;
    return result;
}

void Memory::writeHalfWord(Address addr, uint16_t value) {
    checkAddress(addr, 2);
    
    // 小端序存储
    memory_[addr] = static_cast<uint8_t>(value & 0xFF);
    memory_[addr + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

uint32_t Memory::readWord(Address addr) const {
    checkAddress(addr, 4);
    
    // 小端序读取
    uint32_t result = memory_[addr];
    result |= static_cast<uint32_t>(memory_[addr + 1]) << 8;
    result |= static_cast<uint32_t>(memory_[addr + 2]) << 16;
    result |= static_cast<uint32_t>(memory_[addr + 3]) << 24;
    return result;
}

void Memory::writeWord(Address addr, uint32_t value) {
    checkAddress(addr, 4);
    
    // 小端序存储
    memory_[addr] = static_cast<uint8_t>(value & 0xFF);
    memory_[addr + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    memory_[addr + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    memory_[addr + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

uint64_t Memory::read64(Address addr) const {
    checkAddress(addr, 8);
    
    // 小端序读取
    uint64_t result = memory_[addr];
    result |= static_cast<uint64_t>(memory_[addr + 1]) << 8;
    result |= static_cast<uint64_t>(memory_[addr + 2]) << 16;
    result |= static_cast<uint64_t>(memory_[addr + 3]) << 24;
    result |= static_cast<uint64_t>(memory_[addr + 4]) << 32;
    result |= static_cast<uint64_t>(memory_[addr + 5]) << 40;
    result |= static_cast<uint64_t>(memory_[addr + 6]) << 48;
    result |= static_cast<uint64_t>(memory_[addr + 7]) << 56;
    return result;
}

void Memory::write64(Address addr, uint64_t value) {
    // 检查是否为 tohost 地址
    if (addr == TOHOST_ADDR) {
        handleTohost(value);
        return;
    }
    
    checkAddress(addr, 8);
    
    // 小端序存储
    memory_[addr] = static_cast<uint8_t>(value & 0xFF);
    memory_[addr + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    memory_[addr + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    memory_[addr + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    memory_[addr + 4] = static_cast<uint8_t>((value >> 32) & 0xFF);
    memory_[addr + 5] = static_cast<uint8_t>((value >> 40) & 0xFF);
    memory_[addr + 6] = static_cast<uint8_t>((value >> 48) & 0xFF);
    memory_[addr + 7] = static_cast<uint8_t>((value >> 56) & 0xFF);
}

Instruction Memory::fetchInstruction(Address addr) const {
    // 支持 C 扩展：指令只需要2字节对齐
    if (addr % 2 != 0) {
        throw MemoryException("指令地址必须2字节对齐: 0x" + 
                             std::to_string(addr));
    }
    
    // 先读取16位，检查是否为压缩指令
    uint16_t first_half = readHalfWord(addr);
    
    // 检查是否为32位指令（最低2位为11）
    if ((first_half & 0x03) == 0x03) {
        // 32位指令，读取完整的32位（可能跨越4字节边界）
        uint16_t second_half = readHalfWord(addr + 2);
        return static_cast<uint32_t>(first_half) | (static_cast<uint32_t>(second_half) << 16);
    } else {
        // 16位压缩指令，直接返回
        return static_cast<uint32_t>(first_half);
    }
}

void Memory::clear() {
    std::fill(memory_.begin(), memory_.end(), 0);
}

void Memory::loadProgram(const std::vector<uint8_t>& program, Address startAddr) {
    if (startAddr + program.size() > memory_.size()) {
        throw MemoryException("程序太大，超出内存范围");
    }
    
    std::copy(program.begin(), program.end(), 
              memory_.begin() + startAddr);
}

void Memory::dump(Address startAddr, size_t length) const {
    if (startAddr >= memory_.size()) {
        std::cout << "地址超出内存范围\n";
        return;
    }
    
    size_t endAddr = std::min(static_cast<size_t>(startAddr + length), 
                              memory_.size());
    
    std::cout << "内存转储 (地址: 0x" << std::hex << startAddr 
              << " - 0x" << (endAddr - 1) << "):\n";
    
    for (Address addr = startAddr; addr < endAddr; addr += 16) {
        std::cout << std::hex << std::setfill('0') << std::setw(8) 
                  << addr << ": ";
        
        // 打印十六进制数据
        for (int i = 0; i < 16 && addr + i < endAddr; ++i) {
            std::cout << std::hex << std::setfill('0') << std::setw(2) 
                      << static_cast<int>(memory_[addr + i]) << " ";
        }
        
        // 对齐
        for (int i = endAddr - addr; i < 16; ++i) {
            std::cout << "   ";
        }
        
        std::cout << " |";
        
        // 打印ASCII字符
        for (int i = 0; i < 16 && addr + i < endAddr; ++i) {
            char c = static_cast<char>(memory_[addr + i]);
            std::cout << (std::isprint(c) ? c : '.');
        }
        
        std::cout << "|\n";
    }
    std::cout << std::dec;  // 恢复十进制输出
}

void Memory::checkAddress(Address addr, size_t accessSize) const {
    if (addr + accessSize > memory_.size()) {
        throw MemoryException("内存访问越界: 地址=0x" + 
                             std::to_string(addr) + 
                             ", 访问大小=" + std::to_string(accessSize) +
                             ", 内存大小=" + std::to_string(memory_.size()));
    }
}

void Memory::handleTohost(uint64_t value) {
    if (value == 0) {
        // 忽略零值写入
        return;
    }
    
    if (value & 1) {
        // 最低位为1表示程序退出
        exit_code_ = static_cast<int>(value >> 1);
        should_exit_ = true;
        std::cout << "[tohost] 程序请求退出，退出码: " << exit_code_ << std::endl;
    } else {
        // 最低位为0表示系统调用
        std::cout << "[tohost] 处理系统调用请求，magic_mem地址: 0x" 
                  << std::hex << value << std::dec << std::endl;
        processSyscall(value);
    }
}

void Memory::processSyscall(Address magic_mem_addr) {
    try {
        // 读取系统调用参数
        uint64_t syscall_num = read64(magic_mem_addr);
        uint64_t arg0 = read64(magic_mem_addr + 8);
        uint64_t arg1 = read64(magic_mem_addr + 16);
        uint64_t arg2 = read64(magic_mem_addr + 24);
        
        std::cout << "[tohost] 系统调用: " << syscall_num 
                  << ", args: " << arg0 << ", " << arg1 << ", " << arg2 << std::endl;
        
        // 处理常见的系统调用
        if (syscall_num == 64) { // SYS_write
            // arg0 = fd, arg1 = buffer地址, arg2 = 长度
            if (arg0 == 1 || arg0 == 2) { // stdout 或 stderr
                std::vector<char> buffer(arg2);
                for (size_t i = 0; i < arg2; i++) {
                    buffer[i] = static_cast<char>(readByte(arg1 + i));
                }
                // 输出到控制台
                std::cout.write(buffer.data(), arg2);
                std::cout.flush();
                
                // 返回成功
                write64(magic_mem_addr, arg2); // 返回写入的字节数
            }
        } else {
            std::cout << "[tohost] 不支持的系统调用: " << syscall_num << std::endl;
            // 返回错误
            write64(magic_mem_addr, static_cast<uint64_t>(-1));
        }
        
        // 写入 fromhost 表示处理完成，解除程序等待
        write64(FROMHOST_ADDR, 1);
        
    } catch (const MemoryException& e) {
        std::cerr << "[tohost] 系统调用处理错误: " << e.what() << std::endl;
        // 写入 fromhost 表示处理完成（即使出错）
        write64(FROMHOST_ADDR, 1);
    }
}

} // namespace riscv