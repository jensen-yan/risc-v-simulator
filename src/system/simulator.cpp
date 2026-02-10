#include "system/simulator.h"
#include "system/difftest.h"
#include "common/debug_types.h"
#include "cpu/ooo/ooo_cpu.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>

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
        LOGE(SYSTEM, "failed to load program: %s", e.what());
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
        LOGE(SYSTEM, "failed to load program from bytes: %s", e.what());
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
        LOGE(SYSTEM, "failed to load riscv program: %s", e.what());
        return false;
    }
}

void Simulator::setHostCommAddresses(Address tohostAddr, Address fromhostAddr) {
    memory_->setHostCommAddresses(tohostAddr, fromhostAddr);
    if (reference_memory_) {
        reference_memory_->setHostCommAddresses(tohostAddr, fromhostAddr);
    }
}

void Simulator::setEnabledExtensions(uint32_t extensions) {
    if (cpu_) {
        cpu_->setEnabledExtensions(extensions);
    }
    if (reference_cpu_) {
        reference_cpu_->setEnabledExtensions(extensions);
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
    halted_by_instruction_limit_ = false;
    halted_by_cycle_limit_ = false;
    while (!cpu_->isHalted() && !memory_->shouldExit()) {
        step();

        if (cpuType_ == CpuType::IN_ORDER &&
            cpu_->getInstructionCount() > kMaxInOrderInstructions) {
            LOGW(SYSTEM, "instruction count exceeds limit (%llu), auto halt",
                    static_cast<unsigned long long>(kMaxInOrderInstructions));
            cpu_->requestHalt();
            halted_by_instruction_limit_ = true;
            break;
        }

        if (cpuType_ == CpuType::OUT_OF_ORDER &&
            cycle_count_ > kMaxOutOfOrderCycles) {
            LOGW(SYSTEM, "cycle count exceeds limit (%llu), auto halt",
                    static_cast<unsigned long long>(kMaxOutOfOrderCycles));
            cpu_->requestHalt();
            halted_by_cycle_limit_ = true;
            break;
        }
    }

    if (memory_->shouldExit()) {
        LOGI(SYSTEM, "[tohost] program exited via tohost, code=%d",
                static_cast<int>(memory_->getExitCode()));
    }

    debugManager.setGlobalContext(cycle_count_, cpu_->getPC());
}

void Simulator::reset() {
    cpu_->reset();
    memory_->clear();
    cycle_count_ = 0;
    halted_by_instruction_limit_ = false;
    halted_by_cycle_limit_ = false;
    DebugManager::getInstance().setGlobalContext(cycle_count_, cpu_->getPC());
}

bool Simulator::isHalted() const {
    return cpu_->isHalted();
}

uint64_t Simulator::getInstructionCount() const {
    return cpu_->getInstructionCount();
}

bool Simulator::hasProgramExit() const {
    return memory_->shouldExit();
}

int Simulator::getProgramExitCode() const {
    return memory_->getExitCode();
}

bool Simulator::endedOnZeroInstruction() const {
    if (!cpu_->isHalted() || memory_->shouldExit()) {
        return false;
    }

    try {
        return memory_->fetchInstruction(cpu_->getPC()) == 0;
    } catch (const std::exception&) {
        return false;
    }
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

bool Simulator::dumpSignature(const std::string& outputPath,
                              Address startAddr,
                              Address endAddr,
                              size_t granularity) const {
    if (startAddr >= endAddr) {
        LOGE(SYSTEM, "invalid signature range: [0x%" PRIx64 ", 0x%" PRIx64 ")",
             startAddr, endAddr);
        return false;
    }
    if (granularity == 0) {
        LOGE(SYSTEM, "signature granularity must be greater than 0");
        return false;
    }
    if (startAddr % granularity != 0 || endAddr % granularity != 0) {
        LOGE(SYSTEM, "signature range must align with granularity=%zu", granularity);
        return false;
    }

    std::ofstream out(outputPath);
    if (!out.is_open()) {
        LOGE(SYSTEM, "failed to open signature file: %s", outputPath.c_str());
        return false;
    }

    out << std::hex << std::setfill('0');
    const int hexWidth = static_cast<int>(granularity * 2);

    try {
        for (Address addr = startAddr; addr < endAddr; addr += granularity) {
            uint64_t value = 0;
            for (size_t i = 0; i < granularity; ++i) {
                value |= static_cast<uint64_t>(memory_->readByte(addr + i)) << (8 * i);
            }
            out << std::setw(hexWidth) << value << "\n";
        }
    } catch (const std::exception& e) {
        LOGE(SYSTEM, "failed to dump signature: %s", e.what());
        return false;
    }

    LOGI(SYSTEM,
         "signature dumped: path=%s, range=[0x%" PRIx64 ", 0x%" PRIx64 "), granularity=%zu",
         outputPath.c_str(),
         startAddr,
         endAddr,
         granularity);
    return true;
}

void Simulator::printStatistics() const {
    std::cout << "\n=== Execution Stats ===\n";
    std::cout << "Instructions: " << getInstructionCount() << "\n";
    std::cout << "Final PC: 0x" << std::hex << cpu_->getPC() << std::dec << "\n";
    std::cout << "Program State: " << (isHalted() ? "halted" : "running") << "\n";
    std::cout << "Cycles: " << cycle_count_ << "\n";
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
            LOGE(SYSTEM, "invalid elf file: %s", filename.c_str());
            return false;
        }

        if (elfInfo.hasTohostSymbol && elfInfo.hasFromhostSymbol) {
            setHostCommAddresses(elfInfo.tohostAddr, elfInfo.fromhostAddr);
            LOGI(SYSTEM, "use ELF host symbols: tohost=0x%" PRIx64 ", fromhost=0x%" PRIx64,
                 elfInfo.tohostAddr, elfInfo.fromhostAddr);
        } else if (elfInfo.hasTohostSymbol || elfInfo.hasFromhostSymbol) {
            LOGW(SYSTEM, "incomplete ELF host symbols (tohost=%s, fromhost=%s), keep existing defaults",
                 elfInfo.hasTohostSymbol ? "found" : "missing",
                 elfInfo.hasFromhostSymbol ? "found" : "missing");
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
                LOGE(SYSTEM, "failed to load elf for reference cpu: %s", filename.c_str());
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

            LOGI(DIFFTEST, "difftest initialized for ooo mode");
        }
        
        LOGI(SYSTEM, "elf loaded successfully, entry=0x%" PRIx64, elfInfo.entryPoint);
        DebugManager::getInstance().setGlobalContext(cycle_count_, cpu_->getPC());
        return true;
    } catch (const std::exception& e) {
        LOGE(SYSTEM, "failed to load elf program: %s", e.what());
        return false;
    }
}

} // namespace riscv
