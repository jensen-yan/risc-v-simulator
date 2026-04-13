#include "system/simulator.h"

#include "common/debug_types.h"
#include "core/csr_utils.h"
#include "core/decoder.h"
#include "core/instruction_executor.h"
#include "system/difftest.h"
#include "system/syscall_handler.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace riscv {

namespace {

std::string toLowerCopy(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    return toLowerCopy(haystack).find(toLowerCopy(needle)) != std::string::npos;
}

CheckpointFailureReason classifyExceptionMessage(const std::string& message) {
    if (containsIgnoreCase(message, "不支持的系统指令") ||
        containsIgnoreCase(message, "unsupported system instruction")) {
        return CheckpointFailureReason::UNIMPLEMENTED_SYSTEM_INSTRUCTION;
    }
    if (containsIgnoreCase(message, "非法指令") ||
        containsIgnoreCase(message, "illegal") ||
        containsIgnoreCase(message, "unsupported instruction type") ||
        containsIgnoreCase(message, "unsupported i-type instruction") ||
        containsIgnoreCase(message, "invalid system_type opcode") ||
        containsIgnoreCase(message, "非法的")) {
        return CheckpointFailureReason::ILLEGAL_INSTRUCTION;
    }
    return CheckpointFailureReason::UNKNOWN;
}

struct StopClassification {
    CheckpointFailureReason reason = CheckpointFailureReason::NONE;
    std::string message;
};

StopClassification classifyStoppedState(ICpuInterface* cpu,
                                        const std::shared_ptr<Memory>& memory,
                                        bool halted_by_instruction_limit,
                                        bool halted_by_cycle_limit) {
    const std::string halt_message = cpu->getLastHaltMessage();
    const uint64_t halt_pc = cpu->getLastHaltPC() != 0 ? cpu->getLastHaltPC() : cpu->getPC();

    if (!halt_message.empty()) {
        return {classifyExceptionMessage(halt_message), halt_message};
    }

    if (memory->shouldExit()) {
        return {CheckpointFailureReason::PROGRAM_EXIT,
                "program exited via tohost/fromhost before reaching the instruction window"};
    }
    if (halted_by_instruction_limit || halted_by_cycle_limit) {
        return {CheckpointFailureReason::WINDOW_NOT_REACHED,
                "execution stopped by simulator instruction/cycle limit before reaching the instruction window"};
    }
    if (!cpu->isHalted()) {
        return {CheckpointFailureReason::WINDOW_NOT_REACHED,
                "execution stopped before reaching the instruction window"};
    }

    const uint64_t stop_pc = halt_pc;
    try {
        const Instruction raw_inst = memory->fetchInstruction(stop_pc);
        if (raw_inst == 0) {
            return {CheckpointFailureReason::WINDOW_NOT_REACHED,
                    "execution reached a zero instruction before the instruction window target"};
        }

        Decoder decoder;
        const uint32_t extensions = cpu->getEnabledExtensions();
        const DecodedInstruction decoded =
            ((raw_inst & 0x03U) != 0x03U)
                ? decoder.decodeCompressed(static_cast<uint16_t>(raw_inst), extensions)
                : decoder.decode(raw_inst, extensions);

        if (decoded.opcode == Opcode::SYSTEM) {
            if (InstructionExecutor::isSystemCall(decoded) &&
                (cpu->getCSR(csr::kMtvec) & ~0x3ULL) == 0 &&
                cpu->getRegister(17) == SyscallHandler::SYS_EXIT) {
                return {CheckpointFailureReason::PROGRAM_EXIT,
                        "program exited via ECALL SYS_exit before reaching the instruction window"};
            }

            if (!InstructionExecutor::isCsrInstruction(decoded) &&
                !InstructionExecutor::isSystemCall(decoded) &&
                !InstructionExecutor::isBreakpoint(decoded) &&
                !InstructionExecutor::isMachineReturn(decoded) &&
                !InstructionExecutor::isSupervisorReturn(decoded) &&
                !InstructionExecutor::isUserReturn(decoded) &&
                !InstructionExecutor::isSfenceVma(decoded)) {
                return {CheckpointFailureReason::UNIMPLEMENTED_SYSTEM_INSTRUCTION,
                        "execution stopped on an unimplemented system instruction"};
            }
        }
    } catch (const IllegalInstructionException& e) {
        return {CheckpointFailureReason::ILLEGAL_INSTRUCTION, e.what()};
    } catch (const SimulatorException&) {
    } catch (const std::exception&) {
    }

    return {CheckpointFailureReason::WINDOW_NOT_REACHED,
            "execution halted before reaching the instruction window"};
}

} // namespace

Simulator::Simulator(size_t memorySize, CpuType cpuType, Address memoryBaseAddress) 
    : memory_(std::make_shared<Memory>(memorySize, memoryBaseAddress)),
      cpu_(CpuFactory::createCpu(cpuType, memory_)),
      cpuType_(cpuType),
      cycle_count_(0) {
    
    // 如果是乱序CPU，创建独立的参考内存和参考CPU用于DiffTest
    if (cpuType == CpuType::OUT_OF_ORDER) {
        reference_memory_ = std::make_shared<Memory>(memorySize, memoryBaseAddress);
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
        
        memory_->resetExitStatus();
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
        memory_->resetExitStatus();
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
        
        memory_->resetExitStatus();
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
            max_in_order_instructions_ > 0 &&
            cpu_->getInstructionCount() > max_in_order_instructions_) {
            LOGW(SYSTEM, "instruction count exceeds limit (%llu), auto halt",
                    static_cast<unsigned long long>(max_in_order_instructions_));
            cpu_->requestHalt();
            halted_by_instruction_limit_ = true;
            break;
        }

        if (cpuType_ == CpuType::OUT_OF_ORDER &&
            max_out_of_order_cycles_ > 0 &&
            cycle_count_ > max_out_of_order_cycles_) {
            LOGW(SYSTEM, "cycle count exceeds limit (%llu), auto halt",
                    static_cast<unsigned long long>(max_out_of_order_cycles_));
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

bool Simulator::runWithWarmup(uint64_t warmupCycles, const std::function<void()>& onWarmup) {
    auto& debugManager = DebugManager::getInstance();
    halted_by_instruction_limit_ = false;
    halted_by_cycle_limit_ = false;
    bool warmup_triggered = false;

    while (!cpu_->isHalted() && !memory_->shouldExit()) {
        step();

        if (!warmup_triggered && warmupCycles > 0 && cycle_count_ >= warmupCycles) {
            warmup_triggered = true;
            if (onWarmup) {
                onWarmup();
            }
        }

        if (cpuType_ == CpuType::IN_ORDER &&
            max_in_order_instructions_ > 0 &&
            cpu_->getInstructionCount() > max_in_order_instructions_) {
            LOGW(SYSTEM, "instruction count exceeds limit (%llu), auto halt",
                    static_cast<unsigned long long>(max_in_order_instructions_));
            cpu_->requestHalt();
            halted_by_instruction_limit_ = true;
            break;
        }

        if (cpuType_ == CpuType::OUT_OF_ORDER &&
            max_out_of_order_cycles_ > 0 &&
            cycle_count_ > max_out_of_order_cycles_) {
            LOGW(SYSTEM, "cycle count exceeds limit (%llu), auto halt",
                    static_cast<unsigned long long>(max_out_of_order_cycles_));
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
    return warmup_triggered;
}

InstructionWindowResult Simulator::runInstructionWindow(uint64_t warmup_instructions,
                                                        uint64_t measure_instructions) {
    auto& debugManager = DebugManager::getInstance();
    halted_by_instruction_limit_ = false;
    halted_by_cycle_limit_ = false;

    InstructionWindowResult result;
    result.stop_pc = cpu_->getPC();
    const uint64_t start_cycle_count = cycle_count_;
    uint64_t measure_start_cycle = start_cycle_count;

    auto refreshResultAccounting = [&]() {
        result.total_instructions =
            result.warmup_instructions_completed + result.measure_instructions_completed;
        result.total_cycles = cycle_count_ - start_cycle_count;
        result.measure_cycles = result.warmup_completed ? (cycle_count_ - measure_start_cycle) : 0;
        result.stop_pc = cpu_->getPC();
    };

    auto finalizeFailure = [&](CheckpointFailureReason reason, const std::string& message) {
        refreshResultAccounting();
        result.success = false;
        result.failure_reason = reason;
        result.message = message;
        result.warmup_completed = (warmup_instructions == 0) ||
                                  (result.warmup_instructions_completed >= warmup_instructions);
        result.measure_completed = (measure_instructions == 0) ||
                                   (result.measure_instructions_completed >= measure_instructions);
        return result;
    };

    auto finalizeSuccess = [&]() {
        refreshResultAccounting();
        result.success = true;
        result.warmup_completed = true;
        result.measure_completed = true;
        result.failure_reason = CheckpointFailureReason::NONE;
        result.message = "instruction window completed";
        return result;
    };

    if (warmup_instructions == 0) {
        result.warmup_completed = true;
        measure_start_cycle = cycle_count_;
        cpu_->resetStats();
    }

    if (measure_instructions == 0) {
        result.measure_completed = true;
        return finalizeSuccess();
    }

    while (true) {
        if (cpu_->isHalted() || memory_->shouldExit()) {
            const auto classification =
                classifyStoppedState(cpu_.get(), memory_, halted_by_instruction_limit_, halted_by_cycle_limit_);
            return finalizeFailure(classification.reason, classification.message);
        }

        const uint64_t warmup_remaining_before_step =
            result.warmup_completed ? 0 : (warmup_instructions - result.warmup_instructions_completed);
        const uint64_t instructions_before = cpu_->getInstructionCount();

        if (!result.warmup_completed && warmup_remaining_before_step > 0) {
            cpu_->setNextStepRetireLimit(static_cast<size_t>(warmup_remaining_before_step));
        } else {
            cpu_->clearNextStepRetireLimit();
        }

        try {
            step();
        } catch (const IllegalInstructionException& e) {
            cpu_->clearNextStepRetireLimit();
            return finalizeFailure(classifyExceptionMessage(e.what()), e.what());
        } catch (const SimulatorException& e) {
            cpu_->clearNextStepRetireLimit();
            return finalizeFailure(classifyExceptionMessage(e.what()), e.what());
        } catch (const std::exception& e) {
            cpu_->clearNextStepRetireLimit();
            return finalizeFailure(CheckpointFailureReason::UNKNOWN, e.what());
        }

        cpu_->clearNextStepRetireLimit();

        const uint64_t retired = cpu_->getInstructionCount() - instructions_before;
        const uint64_t warmup_delta = result.warmup_completed
            ? 0
            : std::min(retired, warmup_remaining_before_step);
        result.warmup_instructions_completed += warmup_delta;

        if (!result.warmup_completed && result.warmup_instructions_completed == warmup_instructions) {
            result.warmup_completed = true;
            result.measure_cycles = 0;
            measure_start_cycle = cycle_count_;
            cpu_->resetStats();
        }

        const uint64_t measure_delta = retired - warmup_delta;
        if (measure_delta > 0) {
            result.measure_instructions_completed += measure_delta;
        }

        if (result.warmup_completed && result.measure_instructions_completed >= measure_instructions) {
            return finalizeSuccess();
        }

        if (cpuType_ == CpuType::IN_ORDER &&
            max_in_order_instructions_ > 0 &&
            cpu_->getInstructionCount() > max_in_order_instructions_) {
            LOGW(SYSTEM, "instruction count exceeds limit (%llu), auto halt",
                 static_cast<unsigned long long>(max_in_order_instructions_));
            cpu_->requestHalt();
            halted_by_instruction_limit_ = true;
        }

        if (cpuType_ == CpuType::OUT_OF_ORDER &&
            max_out_of_order_cycles_ > 0 &&
            cycle_count_ > max_out_of_order_cycles_) {
            LOGW(SYSTEM, "cycle count exceeds limit (%llu), auto halt",
                 static_cast<unsigned long long>(max_out_of_order_cycles_));
            cpu_->requestHalt();
            halted_by_cycle_limit_ = true;
        }

        if (cpu_->isHalted() || memory_->shouldExit()) {
            debugManager.setGlobalContext(cycle_count_, cpu_->getPC());
            const auto classification =
                classifyStoppedState(cpu_.get(), memory_, halted_by_instruction_limit_, halted_by_cycle_limit_);
            return finalizeFailure(classification.reason, classification.message);
        }
    }
}

void Simulator::reset() {
    cpu_->reset();
    cpu_->clearNextStepRetireLimit();
    memory_->clear();
    memory_->resetExitStatus();
    cycle_count_ = 0;
    halted_by_instruction_limit_ = false;
    halted_by_cycle_limit_ = false;
    if (reference_cpu_ && reference_memory_) {
        reference_cpu_->reset();
        reference_cpu_->clearNextStepRetireLimit();
        reference_memory_->clear();
        reference_memory_->resetExitStatus();
    }
    if (difftest_) {
        difftest_->reset();
    }
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
        memory_->resetExitStatus();
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

bool Simulator::loadSnapshot(const SnapshotBundle& snapshot) {
    try {
        reset();
        memory_->resetExitStatus();
        cpu_->setEnabledExtensions(snapshot.enabled_extensions);

        for (const auto& segment : snapshot.memory_segments) {
            if (segment.isFileBacked()) {
                memory_->loadProgramFromFile(segment.file_path, segment.base, segment.size);
            } else if (!segment.bytes.empty()) {
                memory_->loadProgram(segment.bytes, segment.base);
            }
        }

        for (size_t i = 0; i < snapshot.integer_regs.size(); ++i) {
            cpu_->setRegister(static_cast<RegNum>(i), snapshot.integer_regs[i]);
        }
        for (size_t i = 0; i < snapshot.fp_regs.size(); ++i) {
            cpu_->setFPRegister(static_cast<RegNum>(i), snapshot.fp_regs[i]);
        }
        for (const auto& [csr_addr, csr_value] : snapshot.csr_values) {
            cpu_->setCSR(csr_addr, csr_value);
        }
        cpu_->setPC(snapshot.pc);

        if (reference_memory_ && reference_cpu_) {
            reference_memory_->clear();
            reference_memory_->resetExitStatus();
            reference_cpu_->reset();
            reference_cpu_->setEnabledExtensions(snapshot.enabled_extensions);

            for (const auto& segment : snapshot.memory_segments) {
                if (segment.isFileBacked()) {
                    reference_memory_->loadProgramFromFile(segment.file_path, segment.base, segment.size);
                } else if (!segment.bytes.empty()) {
                    reference_memory_->loadProgram(segment.bytes, segment.base);
                }
            }
            for (size_t i = 0; i < snapshot.integer_regs.size(); ++i) {
                reference_cpu_->setRegister(static_cast<RegNum>(i), snapshot.integer_regs[i]);
            }
            for (size_t i = 0; i < snapshot.fp_regs.size(); ++i) {
                reference_cpu_->setFPRegister(static_cast<RegNum>(i), snapshot.fp_regs[i]);
            }
            for (const auto& [csr_addr, csr_value] : snapshot.csr_values) {
                reference_cpu_->setCSR(csr_addr, csr_value);
            }
            reference_cpu_->setPC(snapshot.pc);
        }

        if (difftest_) {
            difftest_->reset();
            cpu_->setDiffTest(difftest_.get());
        }

        halted_by_instruction_limit_ = false;
        halted_by_cycle_limit_ = false;
        DebugManager::getInstance().setGlobalContext(cycle_count_, cpu_->getPC());
        return true;
    } catch (const std::exception& e) {
        LOGE(SYSTEM, "failed to load snapshot: %s", e.what());
        return false;
    }
}

void Simulator::enablePipelineTracer(const std::string& output_path,
                                     uint64_t start_cycle,
                                     uint64_t end_cycle,
                                     size_t max_instructions) {
    if (cpuType_ != CpuType::OUT_OF_ORDER) {
        LOGW(SYSTEM, "pipeline tracer only available for OOO CPU");
        return;
    }

    PipelineTracer::Config config;
    config.start_cycle = start_cycle;
    config.end_cycle = end_cycle;
    config.max_instructions = max_instructions;

    pipeline_tracer_ = std::make_unique<PipelineTracer>(config);
    pipeline_view_path_ = output_path;

    // cpu_ 实际上是 OutOfOrderCpuAdapter 包装了 OutOfOrderCPU
    // 通过 ICpuInterface 的 setPipelineTracer 方法设置
    cpu_->setPipelineTracer(pipeline_tracer_.get());
}

bool Simulator::writePipelineView() const {
    if (!pipeline_tracer_ || pipeline_view_path_.empty()) return false;
    // 同时生成 .txt 文本版
    std::string txt_path = pipeline_view_path_;
    auto dot = txt_path.rfind('.');
    if (dot != std::string::npos) {
        txt_path = txt_path.substr(0, dot) + ".txt";
    } else {
        txt_path += ".txt";
    }
    pipeline_tracer_->generateText(txt_path);
    return pipeline_tracer_->generateHTML(pipeline_view_path_);
}

} // namespace riscv
