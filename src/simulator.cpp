#include "simulator.h"
#include <iostream>
#include <fstream>
#include <iomanip>

namespace riscv {

Simulator::Simulator(size_t memorySize) 
    : memory_(std::make_shared<Memory>(memorySize)),
      cpu_(std::make_unique<CPU>(memory_)) {
}

bool Simulator::loadProgram(const std::string& filename) {
    try {
        auto program = loadBinaryFile(filename);
        if (program.empty()) {
            return false;
        }
        
        memory_->loadProgram(program);
        cpu_->reset();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "加载程序失败: " << e.what() << "\n";
        return false;
    }
}

bool Simulator::loadProgramFromBytes(const std::vector<uint8_t>& program, Address startAddr) {
    try {
        memory_->loadProgram(program, startAddr);
        cpu_->reset();
        cpu_->setPC(startAddr);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "加载程序失败: " << e.what() << "\n";
        return false;
    }
}

bool Simulator::loadRiscvProgram(const std::string& filename, Address loadAddr) {
    try {
        auto program = loadBinaryFile(filename);
        if (program.empty()) {
            return false;
        }
        
        // 加载程序到指定地址
        memory_->loadProgram(program, loadAddr);
        
        // 重置CPU状态
        cpu_->reset();
        
        // 设置程序计数器
        cpu_->setPC(loadAddr);
        
        // 设置栈指针 - 栈从内存顶部开始向下增长
        // RISC-V ABI约定：x2 是栈指针 (sp)
        cpu_->setRegister(2, memory_->getSize() - 4); // sp
        
        // 其他ABI寄存器：
        // x1 = ra (返回地址寄存器)，暂时设为0，程序结束时会用到
        // x8 = s0/fp (帧指针)，初始化为栈指针值
        cpu_->setRegister(8, memory_->getSize() - 4); // s0/fp
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "加载RISC-V程序失败: " << e.what() << "\n";
        return false;
    }
}

void Simulator::step() {
    cpu_->step();
}

void Simulator::run() {
    cpu_->run();
}

void Simulator::reset() {
    cpu_->reset();
    memory_->clear();
}

uint32_t Simulator::getRegister(RegNum reg) const {
    return cpu_->getRegister(reg);
}

void Simulator::setRegister(RegNum reg, uint32_t value) {
    cpu_->setRegister(reg, value);
}

uint32_t Simulator::getPC() const {
    return cpu_->getPC();
}

void Simulator::setPC(uint32_t pc) {
    cpu_->setPC(pc);
}

uint8_t Simulator::readMemoryByte(Address addr) const {
    return memory_->readByte(addr);
}

uint32_t Simulator::readMemoryWord(Address addr) const {
    return memory_->readWord(addr);
}

void Simulator::writeMemoryByte(Address addr, uint8_t value) {
    memory_->writeByte(addr, value);
}

void Simulator::writeMemoryWord(Address addr, uint32_t value) {
    memory_->writeWord(addr, value);
}

bool Simulator::isHalted() const {
    return cpu_->isHalted();
}

uint64_t Simulator::getInstructionCount() const {
    return cpu_->getInstructionCount();
}

void Simulator::dumpRegisters() const {
    cpu_->dumpRegisters();
}

void Simulator::dumpMemory(Address startAddr, size_t length) const {
    memory_->dump(startAddr, length);
}

void Simulator::dumpState() const {
    cpu_->dumpState();
}

void Simulator::printStatistics() const {
    std::cout << "\n=== 执行统计 ===\n";
    std::cout << "总执行指令数: " << getInstructionCount() << "\n";
    std::cout << "最终PC: 0x" << std::hex << getPC() << std::dec << "\n";
    std::cout << "程序状态: " << (isHalted() ? "已停机" : "运行中") << "\n";
}

std::vector<uint8_t> Simulator::loadBinaryFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw SimulatorException("无法打开文件: " + filename);
    }
    
    // 获取文件大小
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    if (fileSize == 0) {
        throw SimulatorException("文件为空: " + filename);
    }
    
    if (fileSize > memory_->getSize()) {
        throw SimulatorException("文件太大，超出内存容量");
    }
    
    // 读取文件内容
    std::vector<uint8_t> program(fileSize);
    file.read(reinterpret_cast<char*>(program.data()), fileSize);
    
    if (!file.good()) {
        throw SimulatorException("读取文件失败: " + filename);
    }
    
    return program;
}

} // namespace riscv