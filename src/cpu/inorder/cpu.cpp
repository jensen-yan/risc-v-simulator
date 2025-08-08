#include "cpu/inorder/cpu.h"
#include "system/syscall_handler.h"
#include "core/instruction_executor.h"
#include <iostream>
#include <iomanip>

namespace riscv {

CPU::CPU(std::shared_ptr<Memory> memory) 
    : memory_(memory), pc_(0), halted_(false), instruction_count_(0), 
      enabled_extensions_(static_cast<uint32_t>(Extension::I) | static_cast<uint32_t>(Extension::M) | 
                         static_cast<uint32_t>(Extension::F) | static_cast<uint32_t>(Extension::C)),
      last_instruction_compressed_(false) {
    // 初始化寄存器，x0寄存器始终为0
    registers_.fill(0);
    fp_registers_.fill(0);
    
    // 初始化系统调用处理器
    syscall_handler_ = std::make_unique<SyscallHandler>(memory_);
}

CPU::~CPU() = default;

void CPU::step() {
    if (halted_) {
        return;
    }
    
    try {
        // 1. 取指令
        Instruction inst = memory_->fetchInstruction(pc_);
        
        // 如果指令为0，可能表明程序结束或到达无效内存区域
        if (inst == 0) {
            std::cerr << "警告: 执行了空指令(NOP)，程序可能结束" << std::endl;
            halted_ = true;
            return;
        }
        
        // 2. 解码指令
        DecodedInstruction decoded;
        
        // 检查是否为压缩指令（16位）
        if ((inst & 0x03) != 0x03) {
            // 压缩指令
            decoded = decoder_.decodeCompressed(static_cast<uint16_t>(inst), enabled_extensions_);
            last_instruction_compressed_ = true;
        } else {
            // 标准32位指令
            decoded = decoder_.decode(inst, enabled_extensions_);
            last_instruction_compressed_ = false;
        }
        
        // 3. 执行指令
        switch (decoded.type) {
            case InstructionType::R_TYPE:
                executeRType(decoded);
                break;
            case InstructionType::I_TYPE:
                executeIType(decoded);
                break;
            case InstructionType::S_TYPE:
                executeSType(decoded);
                break;
            case InstructionType::B_TYPE:
                executeBType(decoded);
                break;
            case InstructionType::U_TYPE:
                executeUType(decoded);
                break;
            case InstructionType::J_TYPE:
                executeJType(decoded);
                break;
            case InstructionType::SYSTEM_TYPE:
                executeSystem(decoded);
                break;
            default:
                throw IllegalInstructionException("未知指令类型");
                break;
        }
        
        instruction_count_++;
        
        // 简单的停机条件：PC超出内存范围
        if (pc_ >= memory_->getSize()) {
            halted_ = true;
        }
        
    } catch (const MemoryException& e) {
        // PC超出范围或访问无效内存
        halted_ = true;
        throw;
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
    fp_registers_.fill(0);
    pc_ = 0;
    halted_ = false;
    instruction_count_ = 0;
}

uint64_t CPU::getRegister(RegNum reg) const {
    if (reg >= NUM_REGISTERS) {
        throw SimulatorException("无效的寄存器编号: " + std::to_string(reg));
    }
    return registers_[reg];
}

void CPU::setRegister(RegNum reg, uint64_t value) {
    if (reg >= NUM_REGISTERS) {
        throw SimulatorException("无效的寄存器编号: " + std::to_string(reg));
    }
    
    // x0寄存器始终为0
    if (reg != 0) {
        registers_[reg] = value;
    }
}

uint64_t CPU::getFPRegister(RegNum reg) const {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    return fp_registers_[reg];
}

void CPU::setFPRegister(RegNum reg, uint64_t value) {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    fp_registers_[reg] = value;
}

float CPU::getFPRegisterFloat(RegNum reg) const {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    // 取低32位重新解释为IEEE 754单精度浮点数
    uint32_t bits = static_cast<uint32_t>(fp_registers_[reg] & 0xFFFFFFFF);
    return *reinterpret_cast<const float*>(&bits);
}

void CPU::setFPRegisterFloat(RegNum reg, float value) {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    // 重新解释IEEE 754单精度浮点数为32位整数，并存储在低32位
    uint32_t bits = *reinterpret_cast<const uint32_t*>(&value);
    fp_registers_[reg] = (fp_registers_[reg] & 0xFFFFFFFF00000000ULL) | bits;
}

double CPU::getFPRegisterDouble(RegNum reg) const {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    // 重新解释64位整数为IEEE 754双精度浮点数
    return *reinterpret_cast<const double*>(&fp_registers_[reg]);
}

void CPU::setFPRegisterDouble(RegNum reg, double value) {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    // 重新解释IEEE 754双精度浮点数为64位整数
    fp_registers_[reg] = *reinterpret_cast<const uint64_t*>(&value);
}

uint64_t CPU::getCSR(uint16_t csr_addr) const {
    // 简化CSR实现：返回固定的合理默认值
    switch (csr_addr) {
        case CSRAddr::FFLAGS:
            return 0x0;  // 无浮点异常标志
        case CSRAddr::FRM:
            return 0x0;  // 默认舍入模式 (RNE)
        case CSRAddr::FCSR:
            return 0x0;  // FCSR = FRM[7:5] | FFLAGS[4:0]
        case CSRAddr::MSTATUS:
            return 0x1800;  // FS字段设置为初始状态
        case CSRAddr::MISA:
            // RV64IMAFDC (基本指令集 + M + A + F + D + C扩展)
            return 0x8000000000141101UL;
        case CSRAddr::MIE:
            return 0x0;  // 禁用所有中断
        case CSRAddr::MTVEC:
            return 0x0;  // 陷阱向量基地址为0
        default:
            // 对于未知CSR，返回0（标准做法）
            return 0x0;
    }
}

void CPU::setCSR(uint16_t csr_addr, uint64_t value) {
    // 简化CSR实现：忽略写入操作
    // 在实际系统中，这些写入会修改CPU状态，但为了简化实现，
    // 我们只需要提供接口而不实际修改状态
    (void)csr_addr;  // 避免未使用参数警告
    (void)value;
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

void CPU::executeImmediateOperations(const DecodedInstruction& inst) {
    uint64_t rs1_val = getRegister(inst.rs1);
    uint64_t result = InstructionExecutor::executeImmediateOperation(inst, rs1_val);
    setRegister(inst.rd, result);
}

void CPU::executeImmediateOperations32(const DecodedInstruction& inst) {
    uint64_t rs1_val = getRegister(inst.rs1);
    uint64_t result = InstructionExecutor::executeImmediateOperation32(inst, rs1_val);
    setRegister(inst.rd, result);
}

void CPU::executeLoadOperations(const DecodedInstruction& inst) {
    uint64_t addr = getRegister(inst.rs1) + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
    uint64_t value = InstructionExecutor::loadFromMemory(memory_, addr, inst.funct3);
    setRegister(inst.rd, value);
}

void CPU::executeJALR(const DecodedInstruction& inst) {
    uint64_t target = InstructionExecutor::calculateJumpAndLinkTarget(inst, pc_, getRegister(inst.rs1));
    uint64_t return_addr = pc_ + (inst.is_compressed ? 2 : 4); // 根据指令长度确定返回地址
    setRegister(inst.rd, return_addr);
    pc_ = target;
}

void CPU::executeRType(const DecodedInstruction& inst) {
    // 检查是否为M扩展指令
    if (inst.opcode == Opcode::OP && inst.funct7 == Funct7::M_EXT) {
        executeMExtension(inst);
        return;
    }
    
    // 检查是否为F扩展指令
    if (inst.opcode == Opcode::OP_FP) {
        executeFPExtension(inst);
        return;
    }
    
    uint64_t rs1_val = getRegister(inst.rs1);
    uint64_t rs2_val = getRegister(inst.rs2);
    uint64_t result;
    
    if (inst.opcode == Opcode::OP) {
        result = InstructionExecutor::executeRegisterOperation(inst, rs1_val, rs2_val);
    } else if (inst.opcode == Opcode::OP_32) {
        // RV64I: 32位算术运算
        result = InstructionExecutor::executeRegisterOperation32(inst, rs1_val, rs2_val);
    } else {
        throw IllegalInstructionException("不支持的R-type指令");
    }
    
    setRegister(inst.rd, result);
    incrementPC();
}

void CPU::executeIType(const DecodedInstruction& inst) {
    switch (inst.opcode) {
        case Opcode::OP_IMM:
            executeImmediateOperations(inst);
            incrementPC();
            break;
        case Opcode::OP_IMM_32:
            // RV64I: 32位立即数运算
            executeImmediateOperations32(inst);
            incrementPC();
            break;
        case Opcode::LOAD:
            executeLoadOperations(inst);
            incrementPC();
            break;
        case Opcode::JALR:
            executeJALR(inst);
            break;
        case Opcode::MISC_MEM:
            // FENCE指令 - 对于单核模拟器，直接跳过
            incrementPC();
            break;
        default:
            throw IllegalInstructionException("不支持的I-type指令");
    }
}

void CPU::executeSType(const DecodedInstruction& inst) {
    if (inst.opcode == Opcode::STORE) {
        uint64_t addr = getRegister(inst.rs1) + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
        uint64_t value = getRegister(inst.rs2);
        InstructionExecutor::storeToMemory(memory_, addr, value, inst.funct3);
        incrementPC();
    } else {
        throw IllegalInstructionException("不支持的S-type指令");
    }
}

void CPU::executeBType(const DecodedInstruction& inst) {
    if (inst.opcode == Opcode::BRANCH) {
        uint64_t rs1_val = getRegister(inst.rs1);
        uint64_t rs2_val = getRegister(inst.rs2);
        bool branch_taken = InstructionExecutor::evaluateBranchCondition(inst, rs1_val, rs2_val);
        
        if (branch_taken) {
            // 跳转到 PC + 符号扩展的立即数
            pc_ = pc_ + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
        } else {
            // 不跳转，正常递增PC
            incrementPC();
        }
    } else {
        throw IllegalInstructionException("不支持的B-type指令");
    }
}

void CPU::executeUType(const DecodedInstruction& inst) {
    uint64_t result = InstructionExecutor::executeUpperImmediate(inst, pc_);
    setRegister(inst.rd, result);
    incrementPC();
}

void CPU::executeJType(const DecodedInstruction& inst) {
    if (inst.opcode == Opcode::JAL) {
        // JAL: Jump and Link
        // 1. 保存返回地址（根据指令长度确定增量）
        uint32_t return_addr = pc_ + (inst.is_compressed ? 2 : 4);
        setRegister(inst.rd, return_addr);
        
        // 2. 跳转到 PC + 符号扩展的立即数
        pc_ = InstructionExecutor::calculateJumpTarget(inst, pc_);
    } else {
        throw IllegalInstructionException("不支持的J-type指令");
    }
}

void CPU::executeSystem(const DecodedInstruction& inst) {
    if (inst.opcode == Opcode::SYSTEM) {
        // 使用更精确的指令判断函数
        if (InstructionExecutor::isSystemCall(inst)) {
            // ECALL - 环境调用
            handleEcall();
        } else if (InstructionExecutor::isBreakpoint(inst)) {
            // EBREAK - 断点
            handleEbreak();
        } else if (InstructionExecutor::isMachineReturn(inst)) {
            // MRET - 机器模式返回, 暂时不实现
            incrementPC();
        } else if (InstructionExecutor::isSupervisorReturn(inst)) {
            // SRET - 监管模式返回（可选实现）
            incrementPC();
        } else if (InstructionExecutor::isUserReturn(inst)) {
            // URET - 用户模式返回（可选实现）
            incrementPC();
        } else if (inst.funct3 != Funct3::ECALL_EBREAK) {
            // CSR指令 - 暂时不实现，直接跳过
            incrementPC();
        } else {
            throw IllegalInstructionException("不支持的系统指令: imm=" + std::to_string(inst.imm));
        }
    } else {
        throw IllegalInstructionException("不支持的系统指令");
    }
}


void CPU::handleEcall() {
    // 处理系统调用
    bool shouldHalt = syscall_handler_->handleSyscall(this);
    if (shouldHalt) {
        halted_ = true;
    } else {
        incrementPC();  // 系统调用完成后继续执行
    }
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

void CPU::executeMExtension(const DecodedInstruction& inst) {
    uint64_t rs1_val = getRegister(inst.rs1);
    uint64_t rs2_val = getRegister(inst.rs2);
    
    uint64_t result = InstructionExecutor::executeMExtension(inst, rs1_val, rs2_val);
    setRegister(inst.rd, result);
    
    incrementPC();
}

void CPU::executeFPExtension(const DecodedInstruction& inst) {
    // 简化的F扩展实现 - 仅支持基本操作
    float rs1_val = getFPRegisterFloat(inst.rs1);
    float rs2_val = getFPRegisterFloat(inst.rs2);
    
    uint32_t result = InstructionExecutor::executeFPExtension(inst, rs1_val, rs2_val);
    
    // 根据指令类型决定结果存储位置
    uint8_t operation = static_cast<uint8_t>(inst.funct7) >> 2;
    if (operation == 0x14) { // 浮点比较指令，结果存入整数寄存器
        setRegister(inst.rd, result);
    } else if (operation == 0x18) { // 浮点转整数指令，结果存入整数寄存器
        setRegister(inst.rd, result);
    } else if (operation == 0x1A) { // 整数转浮点指令，结果存入浮点寄存器
        // 将uint32_t结果重新解释为float
        union { uint32_t i; float f; } converter;
        converter.i = result;
        setFPRegisterFloat(inst.rd, converter.f);
    } else { // 其他浮点运算指令，结果存入浮点寄存器
        // 将uint32_t结果重新解释为float
        union { uint32_t i; float f; } converter;
        converter.i = result;
        setFPRegisterFloat(inst.rd, converter.f);
    }
    
    incrementPC();
}

} // namespace riscv