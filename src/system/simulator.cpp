#include "system/simulator.h"
#include "system/difftest.h"
#include "common/debug_types.h"
#include "cpu/ooo/ooo_cpu.h"
#include <iostream>
#include <fstream>
#include <iomanip>

namespace riscv {

Simulator::Simulator(size_t memorySize, CpuType cpuType) 
    : memory_(std::make_shared<Memory>(memorySize)),
      cpu_(CpuFactory::createCpu(cpuType, memory_)),
      cpuType_(cpuType),
      cycle_count_(0) {
    
    // 如果是乱序CPU，创建独立的参考内存和参考CPU用于DiffTest
    if (cpuType == CpuType::OUT_OF_ORDER) {
        reference_memory_ = std::make_shared<Memory>(memorySize);
        reference_cpu_ = CpuFactory::createCpu(CpuType::IN_ORDER, reference_memory_);
        
        // DiffTest将在loadElfProgram中创建，因为需要两个CPU都加载相同的程序
    }

    DebugManager::getInstance().setGlobalContext(cycle_count_, cpu_->getPC());
}

Simulator::~Simulator() = default;

bool Simulator::loadProgram(const std::string& filename) {
    try {
        auto program = loadBinaryFile(filename);
        if (program.empty()) {
            return false;
        }
        
        memory_->loadProgram(program);
        cpu_->reset();
        cycle_count_ = 0;
        DebugManager::getInstance().setGlobalContext(cycle_count_, cpu_->getPC());
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
        cycle_count_ = 0;
        DebugManager::getInstance().setGlobalContext(cycle_count_, cpu_->getPC());
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
        cycle_count_ = 0;
        
        // 设置程序计数器
        cpu_->setPC(loadAddr);
        
        // 设置栈指针 - 栈从内存顶部开始向下增长
        // RISC-V ABI约定：x2 是栈指针 (sp)
        cpu_->setRegister(2, memory_->getSize() - 4); // sp
        
        // 其他ABI寄存器：
        // x1 = ra (返回地址寄存器)，暂时设为0，程序结束时会用到
        // x8 = s0/fp (帧指针)，初始化为栈指针值
        cpu_->setRegister(8, memory_->getSize() - 4); // s0/fp

        DebugManager::getInstance().setGlobalContext(cycle_count_, cpu_->getPC());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "加载RISC-V程序失败: " << e.what() << "\n";
        return false;
    }
}

void Simulator::step() {
    auto& debugManager = DebugManager::getInstance();
    debugManager.setGlobalContext(cycle_count_, cpu_->getPC());

    if (cpu_->isHalted()) {
        return;
    }

    cpu_->step();

    ++cycle_count_;
    debugManager.setGlobalContext(cycle_count_, cpu_->getPC());
}

void Simulator::run() {
    auto& debugManager = DebugManager::getInstance();
    while (!cpu_->isHalted() && !memory_->shouldExit()) {
        step();

        if (cpuType_ == CpuType::IN_ORDER &&
            cpu_->getInstructionCount() > kMaxInOrderInstructions) {
            LOG_WARN(SYSTEM, "执行指令数超过限制(%llu)，自动停止",
                     static_cast<unsigned long long>(kMaxInOrderInstructions));
            cpu_->requestHalt();
            break;
        }

        if (cpuType_ == CpuType::OUT_OF_ORDER &&
            cycle_count_ > kMaxOutOfOrderCycles) {
            LOG_WARN(SYSTEM, "执行周期数超过限制(%llu)，自动停止",
                     static_cast<unsigned long long>(kMaxOutOfOrderCycles));
            cpu_->requestHalt();
            break;
        }
    }

    if (memory_->shouldExit()) {
        LOG_INFO(SYSTEM, "[tohost] 程序通过tohost机制退出，退出码: %d",
                 static_cast<int>(memory_->getExitCode()));
    }

    debugManager.setGlobalContext(cycle_count_, cpu_->getPC());
}

void Simulator::reset() {
    cpu_->reset();
    memory_->clear();
    cycle_count_ = 0;
    DebugManager::getInstance().setGlobalContext(cycle_count_, cpu_->getPC());
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
    std::cout << "最终PC: 0x" << std::hex << cpu_->getPC() << std::dec << "\n";
    std::cout << "程序状态: " << (isHalted() ? "已停机" : "运行中") << "\n";
    std::cout << "总周期数: " << cycle_count_ << "\n";
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

bool Simulator::loadElfProgram(const std::string& filename) {
    try {
        // 加载ELF文件到主CPU内存
        ElfLoader::ElfInfo elfInfo = ElfLoader::loadElfFile(filename, memory_);
        
        if (!elfInfo.isValid) {
            std::cerr << "无效的ELF文件: " << filename << std::endl;
            return false;
        }

        // 重置主CPU状态
        cpu_->reset();
        cycle_count_ = 0;
        
        // 设置主CPU的程序计数器为ELF入口点
        cpu_->setPC(elfInfo.entryPoint);
        
        // 设置主CPU的栈指针 - 栈从内存顶部开始向下增长
        cpu_->setRegister(2, memory_->getSize() - 4); // sp
        cpu_->setRegister(8, memory_->getSize() - 4); // s0/fp
        
        // 如果是乱序CPU模式，同时加载ELF到参考CPU
        if (cpuType_ == CpuType::OUT_OF_ORDER && reference_memory_ && reference_cpu_) {
            // 加载相同的ELF文件到参考CPU内存
            ElfLoader::ElfInfo refElfInfo = ElfLoader::loadElfFile(filename, reference_memory_);
            
            if (!refElfInfo.isValid) {
                std::cerr << "参考CPU ELF加载失败: " << filename << std::endl;
                return false;
            }
            
            // 重置参考CPU状态
            reference_cpu_->reset();
            
            // 设置参考CPU的程序计数器和栈指针
            reference_cpu_->setPC(refElfInfo.entryPoint);
            reference_cpu_->setRegister(2, reference_memory_->getSize() - 4); // sp
            reference_cpu_->setRegister(8, reference_memory_->getSize() - 4); // s0/fp
            
            // 创建DiffTest组件，传入两个独立的CPU
            difftest_ = std::make_unique<DiffTest>(cpu_.get(), reference_cpu_.get());
            
            // 将DiffTest设置到乱序CPU中
            cpu_->setDiffTest(difftest_.get());
            
	        std::cout << "DiffTest已初始化" << std::endl;
        }
        
        std::cout << "ELF程序加载成功，入口点: 0x" << std::hex << elfInfo.entryPoint << std::dec << std::endl;
        DebugManager::getInstance().setGlobalContext(cycle_count_, cpu_->getPC());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "加载ELF程序失败: " << e.what() << std::endl;
        return false;
    }
}

} // namespace riscv
