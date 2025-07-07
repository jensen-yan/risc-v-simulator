#include "cpu.h"
#include <iostream>
#include <iomanip>

namespace riscv {

CPU::CPU(std::shared_ptr<Memory> memory) 
    : memory_(memory), pc_(0), halted_(false), instruction_count_(0) {
    // 初始化寄存器，x0寄存器始终为0
    registers_.fill(0);
}

void CPU::step() {
    if (halted_) {
        return;
    }
    
    // TODO: 实现完整的指令执行
    // 暂时只是递增PC和指令计数
    pc_ += 4;
    instruction_count_++;
    
    // 简单的停机条件：PC超出内存范围
    if (pc_ >= memory_->getSize()) {
        halted_ = true;
    }
}

void CPU::run() {
    while (!halted_) {
        step();
        
        // 防止无限循环
        if (instruction_count_ > 100000) {
            std::cout << "警告: 执行指令数超过100000，自动停止\n";
            halted_ = true;
        }
    }
}

void CPU::reset() {
    registers_.fill(0);
    pc_ = 0;
    halted_ = false;
    instruction_count_ = 0;
}

uint32_t CPU::getRegister(RegNum reg) const {
    if (reg >= NUM_REGISTERS) {
        throw SimulatorException("无效的寄存器编号: " + std::to_string(reg));
    }
    return registers_[reg];
}

void CPU::setRegister(RegNum reg, uint32_t value) {
    if (reg >= NUM_REGISTERS) {
        throw SimulatorException("无效的寄存器编号: " + std::to_string(reg));
    }
    
    // x0寄存器始终为0
    if (reg != 0) {
        registers_[reg] = value;
    }
}

void CPU::dumpRegisters() const {
    std::cout << "寄存器状态:\n";
    for (int i = 0; i < NUM_REGISTERS; i += 4) {
        for (int j = 0; j < 4 && i + j < NUM_REGISTERS; ++j) {
            std::cout << "x" << std::setw(2) << (i + j) << ": 0x" 
                      << std::hex << std::setfill('0') << std::setw(8) 
                      << registers_[i + j] << "  ";
        }
        std::cout << "\n";
    }
    std::cout << std::dec;
}

void CPU::dumpState() const {
    std::cout << "CPU状态:\n";
    std::cout << "PC: 0x" << std::hex << pc_ << std::dec << "\n";
    std::cout << "指令计数: " << instruction_count_ << "\n";
    std::cout << "停机状态: " << (halted_ ? "是" : "否") << "\n";
    dumpRegisters();
}

// 临时实现，TODO: 完整实现各种指令类型
void CPU::executeRType(const DecodedInstruction& inst) {
    // TODO: 实现R-type指令
}

void CPU::executeIType(const DecodedInstruction& inst) {
    // TODO: 实现I-type指令
}

void CPU::executeSType(const DecodedInstruction& inst) {
    // TODO: 实现S-type指令
}

void CPU::executeBType(const DecodedInstruction& inst) {
    // TODO: 实现B-type指令
}

void CPU::executeUType(const DecodedInstruction& inst) {
    // TODO: 实现U-type指令
}

void CPU::executeJType(const DecodedInstruction& inst) {
    // TODO: 实现J-type指令
}

void CPU::executeSystem(const DecodedInstruction& inst) {
    // TODO: 实现系统指令
}

uint32_t CPU::loadFromMemory(Address addr, Funct3 funct3) {
    // TODO: 实现内存加载
    return 0;
}

void CPU::storeToMemory(Address addr, uint32_t value, Funct3 funct3) {
    // TODO: 实现内存存储
}

void CPU::handleEcall() {
    // TODO: 实现系统调用
    halted_ = true;
}

void CPU::handleEbreak() {
    // TODO: 实现断点
    halted_ = true;
}

int32_t CPU::signExtend(uint32_t value, int bits) const {
    int32_t mask = (1 << bits) - 1;
    int32_t signBit = 1 << (bits - 1);
    return (value & mask) | (((value & signBit) != 0) ? ~mask : 0);
}

} // namespace riscv