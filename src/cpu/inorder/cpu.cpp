#include "cpu/inorder/cpu.h"
#include "system/syscall_handler.h"
#include "core/instruction_executor.h"
#include <iostream>
#include <iomanip>
#include <cmath>

namespace {
    // 检查单精度浮点数是否为signaling NaN
    bool isSignalingNaN(float value) {
        uint32_t bits = *reinterpret_cast<const uint32_t*>(&value);
        // IEEE 754: signaling NaN的指数部分全为1，尾数最高位为0，但尾数不全为0
        uint32_t exponent = (bits >> 23) & 0xFF;
        uint32_t mantissa = bits & 0x7FFFFF;
        return (exponent == 0xFF) && (mantissa != 0) && ((mantissa & 0x400000) == 0);
    }
}

namespace riscv {

CPU::CPU(std::shared_ptr<Memory> memory) 
    : memory_(memory), pc_(0), halted_(false), instruction_count_(0), 
      enabled_extensions_(static_cast<uint32_t>(Extension::I) | static_cast<uint32_t>(Extension::M) | 
                         static_cast<uint32_t>(Extension::F) | static_cast<uint32_t>(Extension::D) | 
                         static_cast<uint32_t>(Extension::C)),
      last_instruction_compressed_(false), fp_exception_flags_(0) {
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
        // 浮点相关路径的调试输出（便于定位 FADD/FSUB 等问题）
        if (decoded.opcode == Opcode::OP_FP ||
            decoded.opcode == Opcode::LOAD_FP ||
            decoded.opcode == Opcode::STORE_FP) {
            std::cerr << "[FP] PC=0x" << std::hex << pc_ << std::dec
                      << " op=0x" << std::hex << static_cast<int>(decoded.opcode) << std::dec
                      << " type=" << static_cast<int>(decoded.type)
                      << " f3=0x" << std::hex << static_cast<int>(decoded.funct3) << std::dec
                      << " f7=0x" << std::hex << static_cast<int>(decoded.funct7) << std::dec
                      << " rd=" << static_cast<int>(decoded.rd)
                      << " rs1=" << static_cast<int>(decoded.rs1)
                      << " rs2=" << static_cast<int>(decoded.rs2)
                      << " rs3=" << static_cast<int>(decoded.rs3)
                      << " imm=" << decoded.imm
                      << " comp=" << (last_instruction_compressed_ ? 1 : 0)
                      << " gp(x3)=" << getRegister(3)
                      << std::endl;
        }
        
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
            case InstructionType::R4_TYPE:
                executeR4Type(decoded);
                break;
            case InstructionType::SYSTEM_TYPE:
                executeSystem(decoded);
                break;
            default:
                std::cerr << "ERROR: 未知指令类型 type=" << static_cast<int>(decoded.type) 
                         << " opcode=0x" << std::hex << static_cast<uint8_t>(decoded.opcode) << std::dec << std::endl;
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
    fp_exception_flags_ = 0;
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
    // 重新解释IEEE 754单精度浮点数为32位整数，并进行NaN-boxing（RV64：高32位全1）
    uint32_t bits = *reinterpret_cast<const uint32_t*>(&value);
    fp_registers_[reg] = 0xFFFFFFFF00000000ULL | static_cast<uint64_t>(bits);
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
            return fp_exception_flags_;  // 返回当前的浮点异常标志
        case CSRAddr::FRM:
            return 0x0;  // 默认舍入模式 (RNE)
        case CSRAddr::FCSR:
            return fp_exception_flags_;  // FCSR = FRM[7:5] | FFLAGS[4:0]，简化为只返回FFLAGS
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
    
    // 检查是否为浮点加载指令（需要同时检查操作码和funct3）
    if (inst.opcode == Opcode::LOAD_FP && (inst.funct3 == Funct3::FLW || inst.funct3 == Funct3::FLD)) {
        executeFPLoadOperations(inst);
        return;
    }
    
    // 整数加载指令
    uint64_t value = InstructionExecutor::loadFromMemory(memory_, addr, inst.funct3);
    setRegister(inst.rd, value);
}

void CPU::executeFPLoadOperations(const DecodedInstruction& inst) {
    uint64_t addr = getRegister(inst.rs1) + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
    
    if (inst.funct3 == Funct3::FLW) {
        // FLW: 加载单精度浮点数
        uint32_t value = InstructionExecutor::loadFloatFromMemory(memory_, addr);
        std::cerr << "[FP][FLW] PC=0x" << std::hex << pc_ << std::dec
                  << " rd=f" << static_cast<int>(inst.rd)
                  << " addr=0x" << std::hex << addr << std::dec
                  << " word=0x" << std::hex << value << std::dec
                  << std::endl;
        // RV64 单精度寄存器写回需 NaN-boxing
        fp_registers_[inst.rd] = 0xFFFFFFFF00000000ULL | static_cast<uint64_t>(value);
    } else if (inst.funct3 == Funct3::FLD) {
        // FLD: 加载双精度浮点数
        uint64_t value = InstructionExecutor::loadDoubleFromMemory(memory_, addr);
        std::cerr << "[FP][FLD] PC=0x" << std::hex << pc_ << std::dec
                  << " rd=f" << static_cast<int>(inst.rd)
                  << " addr=0x" << std::hex << addr << std::dec
                  << " dword=0x" << std::hex << value << std::dec
                  << std::endl;
        setFPRegister(inst.rd, value);
    } else {
        throw IllegalInstructionException("未知的浮点加载指令");
    }
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
        case Opcode::LOAD_FP:
            // 浮点加载指令
            executeFPLoadOperations(inst);
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
        
        // 整数存储指令
        uint64_t value = getRegister(inst.rs2);
        InstructionExecutor::storeToMemory(memory_, addr, value, inst.funct3);
        incrementPC();
    } else if (inst.opcode == Opcode::STORE_FP) {
        // 浮点存储指令
        executeFPStoreOperations(inst);
        incrementPC();
    } else {
        throw IllegalInstructionException("不支持的S-type指令");
    }
}

void CPU::executeFPStoreOperations(const DecodedInstruction& inst) {
    uint64_t addr = getRegister(inst.rs1) + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
    
    if (inst.funct3 == Funct3::FSW) {
        // FSW: 存储单精度浮点数
        uint32_t value = static_cast<uint32_t>(getFPRegister(inst.rs2) & 0xFFFFFFFF);
        std::cerr << "[FP][FSW] PC=0x" << std::hex << pc_ << std::dec
                  << " rs2=f" << static_cast<int>(inst.rs2)
                  << " addr=0x" << std::hex << addr << std::dec
                  << " word=0x" << std::hex << value << std::dec
                  << std::endl;
        InstructionExecutor::storeFloatToMemory(memory_, addr, value);
    } else if (inst.funct3 == Funct3::FSD) {
        // FSD: 存储双精度浮点数
        uint64_t value = getFPRegister(inst.rs2);
        std::cerr << "[FP][FSD] PC=0x" << std::hex << pc_ << std::dec
                  << " rs2=f" << static_cast<int>(inst.rs2)
                  << " addr=0x" << std::hex << addr << std::dec
                  << " dword=0x" << std::hex << value << std::dec
                  << std::endl;
        InstructionExecutor::storeDoubleToMemory(memory_, addr, value);
    } else {
        throw IllegalInstructionException("未知的浮点存储指令");
    }
    
    incrementPC();
}

void CPU::executeBType(const DecodedInstruction& inst) {
    if (inst.opcode == Opcode::BRANCH) {
        uint64_t rs1_val = getRegister(inst.rs1);
        uint64_t rs2_val = getRegister(inst.rs2);
        bool branch_taken = InstructionExecutor::evaluateBranchCondition(inst, rs1_val, rs2_val);
        
        // 特殊调试：监控跳转到fail的分支
        uint64_t target_pc = pc_ + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
        if (target_pc == 0x80000470 || target_pc == 0x800003a8) {
            std::cerr << "DEBUG: 分支跳转到fail！" << std::endl;
            std::cerr << "  PC: 0x" << std::hex << pc_ << std::dec << std::endl;
            std::cerr << "  分支条件: " << (branch_taken ? "成立" : "不成立") << std::endl;
            std::cerr << "  rs1(" << static_cast<int>(inst.rs1) << ")=" << rs1_val << std::endl;
            std::cerr << "  rs2(" << static_cast<int>(inst.rs2) << ")=" << rs2_val << std::endl;
            std::cerr << "  目标地址: 0x" << std::hex << target_pc << std::dec << std::endl;
            std::cerr << "  当前gp: " << getRegister(3) << std::endl;
        }
        
        if (branch_taken) {
            // 跳转到 PC + 符号扩展的立即数
            pc_ = target_pc;
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
        if (pc_ == 0x80000470 || pc_ == 0x800003a8) {
            std::cerr << "DEBUG: J 跳转到fail！ 新PC=0x" << std::hex << pc_ << std::dec
                      << " gp=" << getRegister(3) << std::endl;
        }
    } else {
        throw IllegalInstructionException("不支持的J-type指令");
    }
}

void CPU::executeSystem(const DecodedInstruction& inst) {
    if (inst.opcode == Opcode::SYSTEM) {
        // 特殊调试：监控test相关的跳转
        if (pc_ == 0x80000470) {
            std::cerr << "DEBUG: 进入fail例程！" << std::endl;
            std::cerr << "  PC: 0x" << std::hex << pc_ << std::dec << std::endl;
            std::cerr << "  gp(x3): " << getRegister(3) << " (失败的测试编号)" << std::endl;
        }
        
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
            // CSR指令处理
            executeCSRInstruction(inst);
        } else {
            throw IllegalInstructionException("不支持的系统指令: imm=" + std::to_string(inst.imm));
        }
    } else {
        throw IllegalInstructionException("不支持的系统指令");
    }
}

void CPU::executeCSRInstruction(const DecodedInstruction& inst) {
    // CSR指令格式：[31:20] csr_addr | [19:15] rs1 | [14:12] funct3 | [11:7] rd | [6:0] opcode
    uint16_t csr_addr = static_cast<uint16_t>((inst.imm) & 0xFFF);
    
    // 最简化的CSR指令实现 - 只支持读取，固定返回默认值
    switch (inst.funct3) {
        case static_cast<Funct3>(0x1): // CSRRW - CSR Read/Write
        case static_cast<Funct3>(0x2): // CSRRS - CSR Read and Set Bits 
        case static_cast<Funct3>(0x3): // CSRRC - CSR Read and Clear Bits
        {
            // 读取CSR的旧值到rd寄存器
            uint64_t old_value = getCSR(csr_addr);
            if (csr_addr == CSRAddr::FFLAGS) {
                std::cerr << "[FP][CSR] CSRR* fflags -> x" << static_cast<int>(inst.rd)
                          << " old=0x" << std::hex << old_value << std::dec << std::endl;
            }
            setRegister(inst.rd, old_value);
            
            // 特殊处理FFLAGS寄存器：读取后清零异常标志
            if (csr_addr == CSRAddr::FFLAGS && inst.rs1 == 0) {
                // fsflags指令（csrrs a1, fflags, x0）：读取并清零异常标志
                fp_exception_flags_ = 0;
                std::cerr << "[FP][CSR] fflags cleared" << std::endl;
            }
            break;
        }
        case static_cast<Funct3>(0x5): // CSRRWI - CSR Read/Write Immediate
        case static_cast<Funct3>(0x6): // CSRRSI - CSR Read and Set Bits Immediate
        case static_cast<Funct3>(0x7): // CSRRCI - CSR Read and Clear Bits Immediate
        {
            // 立即数版本的CSR指令
            uint64_t old_value = getCSR(csr_addr);
            setRegister(inst.rd, old_value);
            break;
        }
        default:
            throw IllegalInstructionException("不支持的CSR指令类型");
    }
    
    incrementPC();
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
    // 其余代码保持不变
    // 检查是否为双精度指令
    if (inst.funct7 == Funct7::FADD_D || inst.funct7 == Funct7::FSUB_D || 
        inst.funct7 == Funct7::FMUL_D || inst.funct7 == Funct7::FDIV_D ||
        inst.funct7 == Funct7::FSQRT_D || inst.funct7 == Funct7::FSGNJ_D ||
        inst.funct7 == Funct7::FMIN_FMAX_D || inst.funct7 == Funct7::FCVT_INT_D ||
        inst.funct7 == Funct7::FMV_X_D || inst.funct7 == Funct7::FCLASS_D ||\
        inst.funct7 == Funct7::FCMP_D || inst.funct7 == Funct7::FCVT_D_INT ||
        inst.funct7 == Funct7::FMV_D_X || inst.funct7 == Funct7::FCVT_S_D ||
        inst.funct7 == Funct7::FCVT_D_S) {
        executeFPExtensionDouble(inst);
        return;
    }
    
    // 单精度浮点指令处理
    uint32_t result = 0;
    float rs1_val = 0.0f, rs2_val = 0.0f;
    bool used_gpr_source = false;
    
    if (inst.funct7 == Funct7::FCVT_S_INT) {
        // 源自整数寄存器
        uint64_t x = getRegister(inst.rs1);
        switch (inst.rs2) {
            case 0: { // FCVT.S.W
                float f = static_cast<float>(static_cast<int32_t>(x));
                result = *reinterpret_cast<uint32_t*>(&f);
                break;
            }
            case 1: { // FCVT.S.WU
                float f = static_cast<float>(static_cast<uint32_t>(x));
                result = *reinterpret_cast<uint32_t*>(&f);
                break;
            }
            case 2: { // FCVT.S.L
                float f = static_cast<float>(static_cast<int64_t>(x));
                result = *reinterpret_cast<uint32_t*>(&f);
                break;
            }
            case 3: { // FCVT.S.LU
                float f = static_cast<float>(x);
                result = *reinterpret_cast<uint32_t*>(&f);
                break;
            }
            default:
                throw IllegalInstructionException("未知的FCVT.S.INT变体");
        }
        used_gpr_source = true;
    } else if (inst.funct7 == Funct7::FMV_W_X) {
        // 位移动：从整数到浮点
        uint64_t x = getRegister(inst.rs1);
        result = static_cast<uint32_t>(x & 0xFFFFFFFFu);
        used_gpr_source = true;
    } else {
        // 常规单精度：源自浮点寄存器
        rs1_val = getFPRegisterFloat(inst.rs1);
        rs2_val = getFPRegisterFloat(inst.rs2);
        std::cerr << "[FP][OP_FP] PC=0x" << std::hex << pc_ << std::dec
                  << " f7=0x" << std::hex << static_cast<int>(inst.funct7) << std::dec
                  << " rd=f" << static_cast<int>(inst.rd)
                  << " rs1=f" << static_cast<int>(inst.rs1)
                  << " rs2=f" << static_cast<int>(inst.rs2)
                  << " rs1.bits=0x" << std::hex << *reinterpret_cast<uint32_t*>(&rs1_val)
                  << " rs2.bits=0x" << *reinterpret_cast<uint32_t*>(&rs2_val) << std::dec
                  << std::endl;
        result = InstructionExecutor::executeFPExtension(inst, rs1_val, rs2_val);
    }
    
    // 根据指令类型决定结果存储位置
    switch (inst.funct7) {
        case Funct7::FCMP_S:
            // 浮点比较指令，结果存入整数寄存器
            // FLT和FLE指令遇到任何NaN时设置异常标志
            // FEQ指令只在遇到signaling NaN时设置异常标志
            if (std::isnan(rs1_val) || std::isnan(rs2_val)) {
                bool should_set_exception = false;
                
                if (inst.funct3 == Funct3::FLT || inst.funct3 == Funct3::FLE) {
                    // FLT和FLE遇到任何NaN都设置异常
                    should_set_exception = true;
                } else if (inst.funct3 == Funct3::FEQ) {
                    // FEQ只在遇到signaling NaN时设置异常
                    should_set_exception = isSignalingNaN(rs1_val) || isSignalingNaN(rs2_val);
                }
                
                if (should_set_exception) {
                    setFPExceptionFlag(0x10);  // Invalid Operation异常标志
                }
            }
            std::cerr << "[FP][CMP.S] rd=x" << static_cast<int>(inst.rd)
                      << " result=" << result << " fflags=0x" << std::hex << fp_exception_flags_ << std::dec
                      << std::endl;
            setRegister(inst.rd, result);
            break;
            
        case Funct7::FCVT_INT_S:
            // 浮点转整数指令，结果存入整数寄存器
            std::cerr << "[FP][FCVT_INT_S] rd=x" << static_cast<int>(inst.rd)
                      << " result=0x" << std::hex << result << std::dec << std::endl;
            setRegister(inst.rd, result);
            break;
            
        case Funct7::FMV_X_W:
            // FMV.X.W和FCLASS.S指令，结果存入整数寄存器
            {
                // RV64: FMV.X.W 需要对32位结果进行符号扩展到XLEN
                int64_t sext = static_cast<int64_t>(static_cast<int32_t>(result));
                uint64_t xlen_val = static_cast<uint64_t>(sext);
                std::cerr << "[FP][FMV.X.W/FCLASS.S] rd=x" << static_cast<int>(inst.rd)
                          << " result32=0x" << std::hex << result
                          << " sext64=0x" << xlen_val << std::dec << std::endl;
                setRegister(inst.rd, xlen_val);
            }
            break;
            
        case Funct7::FCVT_S_INT:
            // 整数转浮点指令，结果存入浮点寄存器（NaN-boxing）
            std::cerr << "[FP][FCVT.S.INT] rd=f" << static_cast<int>(inst.rd)
                      << " bits=0x" << std::hex << result << std::dec << std::endl;
            fp_registers_[inst.rd] = 0xFFFFFFFF00000000ULL | static_cast<uint64_t>(result);
            break;
            
        case Funct7::FMV_W_X:
            // FMV.W.X：整数到浮点（NaN-boxing）
            std::cerr << "[FP][FMV.W.X] rd=f" << static_cast<int>(inst.rd)
                      << " bits=0x" << std::hex << result << std::dec << std::endl;
            fp_registers_[inst.rd] = 0xFFFFFFFF00000000ULL | static_cast<uint64_t>(result);
            break;
            
        default:
            // 其他浮点运算指令（FADD_S、FSUB_S、FMUL_S、FDIV_S等），结果存入浮点寄存器
            std::cerr << "[FP][ALU.S] rd=f" << static_cast<int>(inst.rd)
                      << " bits=0x" << std::hex << result
                      << " fflags=0x" << fp_exception_flags_ << std::dec << std::endl;
            // NaN-boxing 写回
            fp_registers_[inst.rd] = 0xFFFFFFFF00000000ULL | static_cast<uint64_t>(result);
            
            // 异常标志：依据 IEEE 754 常见情形设置 NV/DZ，并用高精度重算判断 Inexact
            if (inst.funct7 == Funct7::FADD_S) {
                if (std::isinf(rs1_val) && std::isinf(rs2_val) && (std::signbit(rs1_val) != std::signbit(rs2_val))) {
                    setFPExceptionFlag(0x10); // Invalid
                }
                // Inexact: 使用long double重算判断舍入
                if (std::isfinite(rs1_val) && std::isfinite(rs2_val)) {
                    long double ext = static_cast<long double>(rs1_val) + static_cast<long double>(rs2_val);
                    float resf; { uint32_t tmp = result; std::memcpy(&resf, &tmp, sizeof(resf)); }
                    if (std::isfinite(resf) && static_cast<long double>(resf) != ext) {
                        setFPExceptionFlag(0x01);
                    }
                }
            } else if (inst.funct7 == Funct7::FSUB_S) {
                if (std::isinf(rs1_val) && std::isinf(rs2_val) && (std::signbit(rs1_val) == std::signbit(rs2_val))) {
                    setFPExceptionFlag(0x10); // Invalid
                }
                if (std::isfinite(rs1_val) && std::isfinite(rs2_val)) {
                    long double ext = static_cast<long double>(rs1_val) - static_cast<long double>(rs2_val);
                    float resf; { uint32_t tmp = result; std::memcpy(&resf, &tmp, sizeof(resf)); }
                    if (std::isfinite(resf) && static_cast<long double>(resf) != ext) {
                        setFPExceptionFlag(0x01);
                    }
                }
            } else if (inst.funct7 == Funct7::FMUL_S) {
                if ((std::isinf(rs1_val) && rs2_val == 0.0f) || (std::isinf(rs2_val) && rs1_val == 0.0f)) {
                    setFPExceptionFlag(0x10); // Invalid
                }
                if (std::isfinite(rs1_val) && std::isfinite(rs2_val)) {
                    long double ext = static_cast<long double>(rs1_val) * static_cast<long double>(rs2_val);
                    float resf; { uint32_t tmp = result; std::memcpy(&resf, &tmp, sizeof(resf)); }
                    if (std::isfinite(resf) && static_cast<long double>(resf) != ext) {
                        setFPExceptionFlag(0x01);
                    }
                }
            } else if (inst.funct7 == Funct7::FDIV_S) {
                if (rs2_val == 0.0f && !std::isnan(rs1_val)) {
                    setFPExceptionFlag(0x08); // Divide-by-zero
                }
                if (std::isinf(rs1_val) && std::isinf(rs2_val)) {
                    setFPExceptionFlag(0x10); // Invalid
                }
                if (std::isfinite(rs1_val) && std::isfinite(rs2_val) && rs2_val != 0.0f) {
                    long double ext = static_cast<long double>(rs1_val) / static_cast<long double>(rs2_val);
                    float resf; { uint32_t tmp = result; std::memcpy(&resf, &tmp, sizeof(resf)); }
                    if (std::isfinite(resf) && static_cast<long double>(resf) != ext) {
                        setFPExceptionFlag(0x01);
                    }
                }
            } else if (inst.funct7 == Funct7::FSQRT_S) {
                if (rs1_val < 0.0f) {
                    setFPExceptionFlag(0x10); // Invalid
                }
            }
            break;
    }
    
    incrementPC();
}

void CPU::executeFPExtensionDouble(const DecodedInstruction& inst) {
    // 双精度浮点指令处理
    double rs1_val = getFPRegisterDouble(inst.rs1);
    double rs2_val = getFPRegisterDouble(inst.rs2);
    
    std::cerr << "[FP][OP_FP.D] PC=0x" << std::hex << pc_ << std::dec
              << " f7=0x" << std::hex << static_cast<int>(inst.funct7) << std::dec
              << " rd=f" << static_cast<int>(inst.rd)
              << " rs1=f" << static_cast<int>(inst.rs1)
              << " rs2=f" << static_cast<int>(inst.rs2)
              << std::endl;
    uint64_t result = InstructionExecutor::executeFPExtensionDouble(inst, rs1_val, rs2_val);
    
    // 根据指令类型决定结果存储位置
    switch (inst.funct7) {
        case Funct7::FCMP_D:
            // 双精度浮点比较指令，结果存入整数寄存器
            // 检测NaN并设置异常标志
            if (std::isnan(rs1_val) || std::isnan(rs2_val)) {
                setFPExceptionFlag(0x10);  // Invalid Operation异常标志
            }
            std::cerr << "[FP][CMP.D] rd=x" << static_cast<int>(inst.rd)
                      << " result=" << result << " fflags=0x" << std::hex << fp_exception_flags_ << std::dec
                      << std::endl;
            setRegister(inst.rd, result);
            break;
            
        case Funct7::FCVT_INT_D:
            // 双精度转整数指令，结果存入整数寄存器
            std::cerr << "[FP][FCVT_INT_D] rd=x" << static_cast<int>(inst.rd)
                      << " result=0x" << std::hex << result << std::dec << std::endl;
            setRegister(inst.rd, result);
            break;
            
        case Funct7::FMV_X_D:
            // FMV.X.D和FCLASS.D指令，结果存入整数寄存器
            std::cerr << "[FP][FMV.X.D/FCLASS.D] rd=x" << static_cast<int>(inst.rd)
                      << " result=0x" << std::hex << result << std::dec << std::endl;
            setRegister(inst.rd, result);
            break;
            
        case Funct7::FCVT_D_INT:
            // 整数转双精度指令，结果存入浮点寄存器
            std::cerr << "[FP][FCVT.D.INT] rd=f" << static_cast<int>(inst.rd)
                      << " bits=0x" << std::hex << result << std::dec << std::endl;
            setFPRegister(inst.rd, result);
            break;
            
        case Funct7::FMV_D_X:
            // FMV.D.X指令：从整数寄存器移动到浮点寄存器
            std::cerr << "[FP][FMV.D.X] rd=f" << static_cast<int>(inst.rd)
                      << " bits=0x" << std::hex << result << std::dec << std::endl;
            setFPRegister(inst.rd, result);
            break;
            
        case Funct7::FCVT_S_D:
            // FCVT.S.D：双精度转单精度，结果存入浮点寄存器的低32位
            setFPRegister(inst.rd, result);
            break;
            
        default:
            // 其他双精度浮点运算指令，结果存入浮点寄存器
            std::cerr << "[FP][FCVT.S.D/ALU.D] rd=f" << static_cast<int>(inst.rd)
                      << " bits=0x" << std::hex << result << std::dec << std::endl;
            setFPRegister(inst.rd, result);
            break;
    }
    
    incrementPC();
}

void CPU::executeR4Type(const DecodedInstruction& inst) {
    // R4型融合乘加指令处理
    executeFusedMultiplyAdd(inst);
}

void CPU::executeFusedMultiplyAdd(const DecodedInstruction& inst) {
    // 在RISC-V中，融合乘加指令的精度通过操作码的低2位判断
    // 但为了简化，我们直接查看指令的funct2字段（rm字段的低2位）
    // 0b00 = 单精度，0b01 = 双精度
    uint8_t fmt = static_cast<uint8_t>(inst.rm) & 0x3;
    
    if (fmt == 1) {
        // 双精度融合乘加
        double rs1_val = getFPRegisterDouble(inst.rs1);
        double rs2_val = getFPRegisterDouble(inst.rs2);
        double rs3_val = getFPRegisterDouble(inst.rs3);
        
        uint64_t result = InstructionExecutor::executeFusedMultiplyAddDouble(inst, rs1_val, rs2_val, rs3_val);
        std::cerr << "[FP][FMA.D] PC=0x" << std::hex << pc_ << std::dec
                  << " rd=f" << static_cast<int>(inst.rd)
                  << " bits=0x" << std::hex << result << std::dec << std::endl;
        setFPRegister(inst.rd, result);
    } else {
        // 单精度融合乘加（默认）
        float rs1_val = getFPRegisterFloat(inst.rs1);
        float rs2_val = getFPRegisterFloat(inst.rs2);
        float rs3_val = getFPRegisterFloat(inst.rs3);
        
        uint32_t result = InstructionExecutor::executeFusedMultiplyAddSingle(inst, rs1_val, rs2_val, rs3_val);
        std::cerr << "[FP][FMA.S] PC=0x" << std::hex << pc_ << std::dec
                  << " rd=f" << static_cast<int>(inst.rd)
                  << " bits=0x" << std::hex << result << std::dec << std::endl;
        setFPRegister(inst.rd, static_cast<uint64_t>(result));
    }
    
    incrementPC();
}

} // namespace riscv