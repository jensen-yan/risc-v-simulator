#include "cpu.h"
#include "syscall_handler.h"
#include <iostream>
#include <iomanip>
#include <limits>

namespace riscv {

CPU::CPU(std::shared_ptr<Memory> memory) 
    : memory_(memory), pc_(0), halted_(false), instruction_count_(0), 
      enabled_extensions_(static_cast<uint32_t>(Extension::I) | static_cast<uint32_t>(Extension::M) | 
                         static_cast<uint32_t>(Extension::F) | static_cast<uint32_t>(Extension::C)) {
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
        DecodedInstruction decoded = decoder_.decode(inst, enabled_extensions_);
        
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

uint32_t CPU::getFPRegister(RegNum reg) const {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    return fp_registers_[reg];
}

void CPU::setFPRegister(RegNum reg, uint32_t value) {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    fp_registers_[reg] = value;
}

float CPU::getFPRegisterFloat(RegNum reg) const {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    // 重新解释32位整数为IEEE 754单精度浮点数
    return *reinterpret_cast<const float*>(&fp_registers_[reg]);
}

void CPU::setFPRegisterFloat(RegNum reg, float value) {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    // 重新解释IEEE 754单精度浮点数为32位整数
    fp_registers_[reg] = *reinterpret_cast<const uint32_t*>(&value);
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
    uint32_t rs1_val = getRegister(inst.rs1);
    uint32_t result = 0;
    
    switch (inst.funct3) {
        case Funct3::ADD_SUB: // ADDI
            result = rs1_val + inst.imm;
            break;
        case Funct3::SLT: // SLTI
            result = (static_cast<int32_t>(rs1_val) < inst.imm) ? 1 : 0;
            break;
        case Funct3::SLTU: // SLTIU
            result = (rs1_val < static_cast<uint32_t>(inst.imm)) ? 1 : 0;
            break;
        case Funct3::XOR: // XORI
            result = rs1_val ^ inst.imm;
            break;
        case Funct3::OR: // ORI
            result = rs1_val | inst.imm;
            break;
        case Funct3::AND: // ANDI
            result = rs1_val & inst.imm;
            break;
        case Funct3::SLL: // SLLI
            // 立即数只取低5位作为移位数
            result = rs1_val << (inst.imm & 0x1F);
            break;
        case Funct3::SRL_SRA: // SRLI/SRAI
            if (inst.funct7 == Funct7::NORMAL) {
                // SRLI - 逻辑右移
                result = rs1_val >> (inst.imm & 0x1F);
            } else {
                // SRAI - 算术右移
                result = static_cast<int32_t>(rs1_val) >> (inst.imm & 0x1F);
            }
            break;
        default:
            throw IllegalInstructionException("不支持的立即数运算");
    }
    
    setRegister(inst.rd, result);
}

void CPU::executeLoadOperations(const DecodedInstruction& inst) {
    uint32_t addr = getRegister(inst.rs1) + inst.imm;
    uint32_t value = loadFromMemory(addr, inst.funct3);
    setRegister(inst.rd, value);
}

void CPU::executeJALR(const DecodedInstruction& inst) {
    uint32_t target = (getRegister(inst.rs1) + inst.imm) & ~1; // 清除最低位
    setRegister(inst.rd, pc_ + 4); // 保存返回地址
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
    
    uint32_t rs1_val = getRegister(inst.rs1);
    uint32_t rs2_val = getRegister(inst.rs2);
    uint32_t result = 0;
    
    switch (inst.funct3) {
        case Funct3::ADD_SUB:
            if (inst.funct7 == Funct7::NORMAL) {
                // ADD
                result = rs1_val + rs2_val;
            } else {
                // SUB
                result = rs1_val - rs2_val;
            }
            break;
        case Funct3::SLL: // SLL - 逻辑左移
            result = rs1_val << (rs2_val & 0x1F);
            break;
        case Funct3::SLT: // SLT - 有符号比较
            result = (static_cast<int32_t>(rs1_val) < static_cast<int32_t>(rs2_val)) ? 1 : 0;
            break;
        case Funct3::SLTU: // SLTU - 无符号比较
            result = (rs1_val < rs2_val) ? 1 : 0;
            break;
        case Funct3::XOR: // XOR - 异或
            result = rs1_val ^ rs2_val;
            break;
        case Funct3::SRL_SRA:
            if (inst.funct7 == Funct7::NORMAL) {
                // SRL - 逻辑右移
                result = rs1_val >> (rs2_val & 0x1F);
            } else {
                // SRA - 算术右移
                result = static_cast<int32_t>(rs1_val) >> (rs2_val & 0x1F);
            }
            break;
        case Funct3::OR: // OR - 或
            result = rs1_val | rs2_val;
            break;
        case Funct3::AND: // AND - 与
            result = rs1_val & rs2_val;
            break;
        default:
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
        uint32_t addr = getRegister(inst.rs1) + inst.imm;
        
        uint32_t value = getRegister(inst.rs2);
        storeToMemory(addr, value, inst.funct3);
        incrementPC();
    } else {
        throw IllegalInstructionException("不支持的S-type指令");
    }
}

void CPU::executeBType(const DecodedInstruction& inst) {
    if (inst.opcode == Opcode::BRANCH) {
        uint32_t rs1_val = getRegister(inst.rs1);
        uint32_t rs2_val = getRegister(inst.rs2);
        bool branch_taken = false;
        
        switch (inst.funct3) {
            case Funct3::BEQ: // Branch if Equal
                branch_taken = (rs1_val == rs2_val);
                break;
            case Funct3::BNE: // Branch if Not Equal
                branch_taken = (rs1_val != rs2_val);
                break;
            case Funct3::BLT: // Branch if Less Than (signed)
                branch_taken = (static_cast<int32_t>(rs1_val) < static_cast<int32_t>(rs2_val));
                break;
            case Funct3::BGE: // Branch if Greater or Equal (signed)
                branch_taken = (static_cast<int32_t>(rs1_val) >= static_cast<int32_t>(rs2_val));
                break;
            case Funct3::BLTU: // Branch if Less Than Unsigned
                branch_taken = (rs1_val < rs2_val);
                break;
            case Funct3::BGEU: // Branch if Greater or Equal Unsigned
                branch_taken = (rs1_val >= rs2_val);
                break;
            default:
                throw IllegalInstructionException("不支持的分支指令");
        }
        
        if (branch_taken) {
            // 跳转到 PC + 符号扩展的立即数
            pc_ = pc_ + inst.imm;
        } else {
            // 不跳转，正常递增PC
            incrementPC();
        }
    } else {
        throw IllegalInstructionException("不支持的B-type指令");
    }
}

void CPU::executeUType(const DecodedInstruction& inst) {
    uint32_t result = 0;
    
    switch (inst.opcode) {
        case Opcode::LUI: // Load Upper Immediate
            // LUI将20位立即数加载到目标寄存器的高20位，低12位清零
            result = static_cast<uint32_t>(inst.imm);
            break;
        case Opcode::AUIPC: // Add Upper Immediate to PC
            // AUIPC将20位立即数加载到高20位，然后加上PC值
            result = static_cast<uint32_t>(inst.imm) + pc_;
            break;
        default:
            throw IllegalInstructionException("不支持的U-type指令");
    }
    
    setRegister(inst.rd, result);
    incrementPC();
}

void CPU::executeJType(const DecodedInstruction& inst) {
    if (inst.opcode == Opcode::JAL) {
        // JAL: Jump and Link
        // 1. 保存返回地址（PC + 4）到目标寄存器
        setRegister(inst.rd, pc_ + 4);
        
        // 2. 跳转到 PC + 符号扩展的立即数
        pc_ = pc_ + inst.imm;
    } else {
        throw IllegalInstructionException("不支持的J-type指令");
    }
}

void CPU::executeSystem(const DecodedInstruction& inst) {
    if (inst.opcode == Opcode::SYSTEM) {
        // CSR指令通过funct3区分，特权指令通过立即数区分
        if (inst.funct3 == Funct3::ADD_SUB && inst.imm == 0) {
            // ECALL - 环境调用
            handleEcall();
        } else if (inst.funct3 == Funct3::ADD_SUB && inst.imm == 1) {
            // EBREAK - 断点
            handleEbreak();
        } else if (inst.funct3 == Funct3::ADD_SUB && inst.imm == 0x302) {
            // MRET - 机器模式返回（简化实现：直接跳转到mepc）
            incrementPC();
        } else if (inst.funct3 != Funct3::ADD_SUB) {
            // CSR指令 - 暂时不实现，直接跳过
            incrementPC();
        } else {
            throw IllegalInstructionException("不支持的系统指令");
        }
    } else {
        throw IllegalInstructionException("不支持的系统指令");
    }
}

uint32_t CPU::loadFromMemory(Address addr, Funct3 funct3) {
    switch (funct3) {
        case Funct3::LB: // Load Byte (符号扩展)
            return static_cast<uint32_t>(static_cast<int8_t>(memory_->readByte(addr)));
        case Funct3::LH: // Load Half Word (符号扩展)
            return static_cast<uint32_t>(static_cast<int16_t>(memory_->readHalfWord(addr)));
        case Funct3::LW: // Load Word
            return memory_->readWord(addr);
        case Funct3::LBU: // Load Byte Unsigned
            return static_cast<uint32_t>(memory_->readByte(addr));
        case Funct3::LHU: // Load Half Word Unsigned
            return static_cast<uint32_t>(memory_->readHalfWord(addr));
        default:
            throw IllegalInstructionException("不支持的加载指令");
    }
}

void CPU::storeToMemory(Address addr, uint32_t value, Funct3 funct3) {
    switch (funct3) {
        case Funct3::SB: // Store Byte
            memory_->writeByte(addr, static_cast<uint8_t>(value & 0xFF));
            break;
        case Funct3::SH: // Store Half Word
            memory_->writeHalfWord(addr, static_cast<uint16_t>(value & 0xFFFF));
            break;
        case Funct3::SW: // Store Word
            memory_->writeWord(addr, value);
            break;
        default:
            throw IllegalInstructionException("不支持的存储指令");
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
    uint32_t rs1_val = getRegister(inst.rs1);
    uint32_t rs2_val = getRegister(inst.rs2);
    
    switch (inst.funct3) {
        case Funct3::MUL: { // MUL - 32位乘法，取低32位
            uint64_t result = static_cast<uint64_t>(rs1_val) * static_cast<uint64_t>(rs2_val);
            setRegister(inst.rd, static_cast<uint32_t>(result));
            break;
        }
        case Funct3::MULH: { // MULH - 有符号高位乘法
            int64_t result = static_cast<int64_t>(static_cast<int32_t>(rs1_val)) * 
                            static_cast<int64_t>(static_cast<int32_t>(rs2_val));
            setRegister(inst.rd, static_cast<uint32_t>(result >> 32));
            break;
        }
        case Funct3::MULHSU: { // MULHSU - 有符号*无符号高位乘法
            int64_t result = static_cast<int64_t>(static_cast<int32_t>(rs1_val)) * 
                            static_cast<int64_t>(rs2_val);
            setRegister(inst.rd, static_cast<uint32_t>(result >> 32));
            break;
        }
        case Funct3::MULHU: { // MULHU - 无符号高位乘法
            uint64_t result = static_cast<uint64_t>(rs1_val) * static_cast<uint64_t>(rs2_val);
            setRegister(inst.rd, static_cast<uint32_t>(result >> 32));
            break;
        }
        case Funct3::DIV: { // DIV - 有符号除法
            if (rs2_val == 0) {
                setRegister(inst.rd, 0xFFFFFFFF); // 除零结果
            } else {
                int32_t result = static_cast<int32_t>(rs1_val) / static_cast<int32_t>(rs2_val);
                setRegister(inst.rd, static_cast<uint32_t>(result));
            }
            break;
        }
        case Funct3::DIVU: { // DIVU - 无符号除法
            if (rs2_val == 0) {
                setRegister(inst.rd, 0xFFFFFFFF); // 除零结果
            } else {
                setRegister(inst.rd, rs1_val / rs2_val);
            }
            break;
        }
        case Funct3::REM: { // REM - 有符号求余
            if (rs2_val == 0) {
                setRegister(inst.rd, rs1_val); // 除零时返回被除数
            } else {
                int32_t result = static_cast<int32_t>(rs1_val) % static_cast<int32_t>(rs2_val);
                setRegister(inst.rd, static_cast<uint32_t>(result));
            }
            break;
        }
        case Funct3::REMU: { // REMU - 无符号求余
            if (rs2_val == 0) {
                setRegister(inst.rd, rs1_val); // 除零时返回被除数
            } else {
                setRegister(inst.rd, rs1_val % rs2_val);
            }
            break;
        }
        default:
            throw IllegalInstructionException("不支持的M扩展指令");
    }
    
    incrementPC();
}

void CPU::executeFPExtension(const DecodedInstruction& inst) {
    // 简化的F扩展实现 - 仅支持基本操作
    float rs1_val = getFPRegisterFloat(inst.rs1);
    float rs2_val = getFPRegisterFloat(inst.rs2);
    
    // funct7的高5位表示操作类型
    uint8_t operation = static_cast<uint8_t>(inst.funct7) >> 2;
    
    switch (operation) {
        case 0x00: { // FADD.S - 浮点加法
            float result = rs1_val + rs2_val;
            setFPRegisterFloat(inst.rd, result);
            break;
        }
        case 0x01: { // FSUB.S - 浮点减法
            float result = rs1_val - rs2_val;
            setFPRegisterFloat(inst.rd, result);
            break;
        }
        case 0x02: { // FMUL.S - 浮点乘法
            float result = rs1_val * rs2_val;
            setFPRegisterFloat(inst.rd, result);
            break;
        }
        case 0x03: { // FDIV.S - 浮点除法
            if (rs2_val == 0.0f) {
                // 处理除零情况，返回无穷大
                setFPRegisterFloat(inst.rd, std::numeric_limits<float>::infinity());
            } else {
                float result = rs1_val / rs2_val;
                setFPRegisterFloat(inst.rd, result);
            }
            break;
        }
        case 0x14: { // FEQ.S, FLT.S, FLE.S - 浮点比较
            uint32_t result = 0;
            switch (inst.funct3) {
                case static_cast<Funct3>(0x02): // FEQ.S
                    result = (rs1_val == rs2_val) ? 1 : 0;
                    break;
                case static_cast<Funct3>(0x01): // FLT.S
                    result = (rs1_val < rs2_val) ? 1 : 0;
                    break;
                case static_cast<Funct3>(0x00): // FLE.S
                    result = (rs1_val <= rs2_val) ? 1 : 0;
                    break;
                default:
                    throw IllegalInstructionException("不支持的浮点比较指令");
            }
            setRegister(inst.rd, result); // 比较结果存入整数寄存器
            break;
        }
        case 0x18: { // FCVT.W.S, FCVT.WU.S - 浮点转整数
            if (inst.rs2 == 0) { // FCVT.W.S - 转有符号整数
                int32_t result = static_cast<int32_t>(rs1_val);
                setRegister(inst.rd, static_cast<uint32_t>(result));
            } else if (inst.rs2 == 1) { // FCVT.WU.S - 转无符号整数
                uint32_t result = static_cast<uint32_t>(rs1_val);
                setRegister(inst.rd, result);
            } else {
                throw IllegalInstructionException("不支持的浮点转换指令");
            }
            break;
        }
        case 0x1A: { // FCVT.S.W, FCVT.S.WU - 整数转浮点
            uint32_t int_val = getRegister(inst.rs1);
            if (inst.rs2 == 0) { // FCVT.S.W - 有符号整数转浮点
                float result = static_cast<float>(static_cast<int32_t>(int_val));
                setFPRegisterFloat(inst.rd, result);
            } else if (inst.rs2 == 1) { // FCVT.S.WU - 无符号整数转浮点
                float result = static_cast<float>(int_val);
                setFPRegisterFloat(inst.rd, result);
            } else {
                throw IllegalInstructionException("不支持的整数转换指令");
            }
            break;
        }
        default:
            throw IllegalInstructionException("不支持的F扩展指令");
    }
    
    incrementPC();
}

} // namespace riscv