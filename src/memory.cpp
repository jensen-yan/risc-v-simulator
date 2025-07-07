#include "memory.h"
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

Instruction Memory::fetchInstruction(Address addr) const {
    // RISC-V 指令必须4字节对齐
    if (addr % 4 != 0) {
        throw MemoryException("指令地址必须4字节对齐: 0x" + 
                             std::to_string(addr));
    }
    return readWord(addr);
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

} // namespace riscv