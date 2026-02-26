#include "core/memory.h"
#include "common/debug_types.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <utility>

namespace riscv {

Memory::Memory(size_t size)
    : memory_(nullptr, &std::free), memory_size_(size) {
    if (size == 0) {
        throw MemoryException("内存大小不能为0");
    }

    void* raw = std::calloc(memory_size_, sizeof(uint8_t));
    if (!raw) {
        throw MemoryException("内存分配失败");
    }
    memory_.reset(static_cast<uint8_t*>(raw));
}

uint8_t Memory::readByte(Address addr) const {
    checkAddress(addr, 1);
    return memory_.get()[addr];
}

void Memory::writeByte(Address addr, uint8_t value) {
    writeByteRaw(addr, value);
}

uint16_t Memory::readHalfWord(Address addr) const {
    checkAddress(addr, 2);
    
    // 小端序读取
    uint16_t result = memory_.get()[addr];
    result |= static_cast<uint16_t>(memory_.get()[addr + 1]) << 8;
    return result;
}

void Memory::writeHalfWord(Address addr, uint16_t value) {
    writeHalfWordRaw(addr, value);
}

uint32_t Memory::readWord(Address addr) const {
    checkAddress(addr, 4);
    
    // 小端序读取
    uint32_t result = memory_.get()[addr];
    result |= static_cast<uint32_t>(memory_.get()[addr + 1]) << 8;
    result |= static_cast<uint32_t>(memory_.get()[addr + 2]) << 16;
    result |= static_cast<uint32_t>(memory_.get()[addr + 3]) << 24;
    return result;
}

void Memory::writeWord(Address addr, uint32_t value) {
    if (addr == tohost_addr_) {
        handleTohost(static_cast<uint64_t>(value));
        return;
    }
    writeWordRaw(addr, value);
}

uint64_t Memory::read64(Address addr) const {
    checkAddress(addr, 8);
    
    // 小端序读取
    uint64_t result = memory_.get()[addr];
    result |= static_cast<uint64_t>(memory_.get()[addr + 1]) << 8;
    result |= static_cast<uint64_t>(memory_.get()[addr + 2]) << 16;
    result |= static_cast<uint64_t>(memory_.get()[addr + 3]) << 24;
    result |= static_cast<uint64_t>(memory_.get()[addr + 4]) << 32;
    result |= static_cast<uint64_t>(memory_.get()[addr + 5]) << 40;
    result |= static_cast<uint64_t>(memory_.get()[addr + 6]) << 48;
    result |= static_cast<uint64_t>(memory_.get()[addr + 7]) << 56;
    return result;
}

void Memory::write64(Address addr, uint64_t value) {
    // 检查是否为 tohost 地址
    if (addr == tohost_addr_) {
        handleTohost(value);
        return;
    }
    write64Raw(addr, value);
}

void Memory::writeByteExternal(Address addr, uint8_t value) {
    writeByteRaw(addr, value);
    notifyExternalWrite(addr, 1);
}

void Memory::writeHalfWordExternal(Address addr, uint16_t value) {
    writeHalfWordRaw(addr, value);
    notifyExternalWrite(addr, 2);
}

void Memory::writeWordExternal(Address addr, uint32_t value) {
    writeWordRaw(addr, value);
    notifyExternalWrite(addr, 4);
}

void Memory::write64External(Address addr, uint64_t value) {
    write64Raw(addr, value);
    // 外部写必须通知观察者；否则CPU私有cache可能读取到旧行。
    notifyExternalWrite(addr, 8);
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
    std::memset(memory_.get(), 0, memory_size_);
}

void Memory::loadProgram(const std::vector<uint8_t>& program, Address startAddr) {
    if (startAddr + program.size() > memory_size_) {
        throw MemoryException("程序太大，超出内存范围");
    }
    
    std::copy(program.begin(), program.end(), memory_.get() + startAddr);
}

void Memory::dump(Address startAddr, size_t length) const {
    if (startAddr >= memory_size_) {
        std::cout << "address out of memory range\n";
        return;
    }
    
    size_t endAddr = std::min(static_cast<size_t>(startAddr + length), 
                              memory_size_);
    
    std::cout << "memory dump (address: 0x" << std::hex << startAddr
              << " - 0x" << (endAddr - 1) << "):\n";
    
    for (Address addr = startAddr; addr < endAddr; addr += 16) {
        std::cout << std::hex << std::setfill('0') << std::setw(8) 
                  << addr << ": ";
        
        // 打印十六进制数据
        for (int i = 0; i < 16 && addr + i < endAddr; ++i) {
            std::cout << std::hex << std::setfill('0') << std::setw(2) 
                      << static_cast<int>(memory_.get()[addr + i]) << " ";
        }
        
        // 对齐
        for (int i = endAddr - addr; i < 16; ++i) {
            std::cout << "   ";
        }
        
        std::cout << " |";
        
        // 打印ASCII字符
        for (int i = 0; i < 16 && addr + i < endAddr; ++i) {
            char c = static_cast<char>(memory_.get()[addr + i]);
            std::cout << (std::isprint(c) ? c : '.');
        }
        
        std::cout << "|\n";
    }
    std::cout << std::dec;  // 恢复十进制输出
}

void Memory::checkAddress(Address addr, size_t accessSize) const {
    if (addr + accessSize > memory_size_) {
        throw MemoryException("内存访问越界: 地址=0x" + 
                             std::to_string(addr) + 
                             ", 访问大小=" + std::to_string(accessSize) +
                             ", 内存大小=" + std::to_string(memory_size_));
    }
}

void Memory::notifyExternalWrite(Address addr, size_t accessSize) {
    // 统一外部写事件入口：Memory不关心具体策略，由观察者决定如何做一致性处理。
    for (const auto& observer : external_write_observers_) {
        if (observer.callback) {
            observer.callback(addr, accessSize);
        }
    }
}

void Memory::writeByteRaw(Address addr, uint8_t value) {
    checkAddress(addr, 1);
    memory_.get()[addr] = value;
}

void Memory::writeHalfWordRaw(Address addr, uint16_t value) {
    checkAddress(addr, 2);
    memory_.get()[addr] = static_cast<uint8_t>(value & 0xFF);
    memory_.get()[addr + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void Memory::writeWordRaw(Address addr, uint32_t value) {
    checkAddress(addr, 4);
    memory_.get()[addr] = static_cast<uint8_t>(value & 0xFF);
    memory_.get()[addr + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    memory_.get()[addr + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    memory_.get()[addr + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void Memory::write64Raw(Address addr, uint64_t value) {
    checkAddress(addr, 8);
    memory_.get()[addr] = static_cast<uint8_t>(value & 0xFF);
    memory_.get()[addr + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    memory_.get()[addr + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    memory_.get()[addr + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    memory_.get()[addr + 4] = static_cast<uint8_t>((value >> 32) & 0xFF);
    memory_.get()[addr + 5] = static_cast<uint8_t>((value >> 40) & 0xFF);
    memory_.get()[addr + 6] = static_cast<uint8_t>((value >> 48) & 0xFF);
    memory_.get()[addr + 7] = static_cast<uint8_t>((value >> 56) & 0xFF);
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
        LOGI(SYSTEM, "[tohost] program requests exit, code=%d", exit_code_);
    } else {
        // 最低位为0表示系统调用
        LOGT(SYSTEM, "[tohost] syscall request, magic_mem=0x%" PRIx64, value);
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
        
        LOGT(SYSTEM, "[tohost] syscall=%" PRIx64 ", args=[%" PRIx64 ", %" PRIx64 ", %" PRIx64 "]",
             syscall_num, arg0, arg1, arg2);
        
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
                
                // 返回成功属于“外部写回”（非CPU流水线写），必须走External路径。
                write64External(magic_mem_addr, arg2);
            }
        } else {
            LOGW(SYSTEM, "[tohost] unsupported syscall: %" PRIx64, syscall_num);
            // 返回错误同样属于外部写回。
            write64External(magic_mem_addr, static_cast<uint64_t>(-1));
        }
        
        // 写入 fromhost 属于外部设备语义，走External路径保持一致性。
        write64External(fromhost_addr_, 1);
        
    } catch (const MemoryException& e) {
        LOGE(SYSTEM, "[tohost] syscall handling error: %s", e.what());
        // 写入 fromhost 表示处理完成（即使出错）
        write64External(fromhost_addr_, 1);
    }
}

void Memory::setHostCommAddresses(Address tohostAddr, Address fromhostAddr) {
    tohost_addr_ = tohostAddr;
    fromhost_addr_ = fromhostAddr;
}

Memory::ExternalWriteObserverId Memory::addExternalWriteObserver(ExternalWriteObserver observer) {
    const ExternalWriteObserverId id = next_external_write_observer_id_++;
    external_write_observers_.push_back({id, std::move(observer)});
    return id;
}

void Memory::removeExternalWriteObserver(ExternalWriteObserverId id) {
    external_write_observers_.erase(
        std::remove_if(external_write_observers_.begin(),
                       external_write_observers_.end(),
                       [id](const ExternalWriteObserverEntry& entry) { return entry.id == id; }),
        external_write_observers_.end());
}

void Memory::clearExternalWriteObservers() {
    external_write_observers_.clear();
}

} // namespace riscv
