#include "cpu.h"
#include "syscall_handler.h"
#include <iostream>
#include <iomanip>

namespace riscv {

CPU::CPU(std::shared_ptr<Memory> memory) 
    : memory_(memory), pc_(0), halted_(false), instruction_count_(0) {
    // 初始化寄存器，x0寄存器始终为0
    registers_.fill(0);
    
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
        
        // 如果PC为0，可能表明程序错误跳转
        if (pc_ == 0) {
            std::cerr << "警告: PC跳转到地址0，指令=0x" << std::hex << inst << std::dec << std::endl;
            halted_ = true;
            return;
        }
        
        // 2. 解码指令
        DecodedInstruction decoded = decoder_.decode(inst);
        
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
            default:
                if (decoded.opcode == Opcode::SYSTEM) {
                    executeSystem(decoded);
                } else {
                    throw IllegalInstructionException("未知指令类型");
                }
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
        // 根据立即数字段区分ECALL和EBREAK
        if (inst.imm == 0) {
            // ECALL - 环境调用
            handleEcall();
        } else if (inst.imm == 1) {
            // EBREAK - 断点
            handleEbreak();
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

} // namespace riscv