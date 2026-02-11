#include "cpu/inorder/cpu.h"
#include "common/debug_types.h"
#include "core/csr_utils.h"
#include "core/instruction_executor.h"
#include "system/syscall_handler.h"
#include <iostream>
#include <iomanip>
#include <cinttypes>

namespace riscv {

CPU::CPU(std::shared_ptr<Memory> memory) 
    : memory_(memory), pc_(0), halted_(false), instruction_count_(0), 
      enabled_extensions_(static_cast<uint32_t>(Extension::I) | static_cast<uint32_t>(Extension::M) | 
                         static_cast<uint32_t>(Extension::A) | static_cast<uint32_t>(Extension::F) |
                         static_cast<uint32_t>(Extension::D) |
                         static_cast<uint32_t>(Extension::C)),
      last_instruction_compressed_(false), reservation_valid_(false), reservation_addr_(0) {
    // 初始化寄存器，x0寄存器始终为0
    registers_.fill(0);
    fp_registers_.fill(0);
    csr_registers_.fill(0);
    
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
        const uint64_t pc_before = pc_;
        Instruction inst = memory_->fetchInstruction(pc_);
        
        // 如果指令为0，可能表明程序结束或到达无效内存区域
        if (inst == 0) {
            LOGW(INORDER, "executed zero instruction, halt as possible program end");
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
        
        LOGT(INORDER,
                  "pc=0x%" PRIx64 " inst=0x%" PRIx32 " rd=%u rs1=%u rs2=%u imm=%d c=%s",
                  pc_before,
                  static_cast<uint32_t>(inst),
                  static_cast<unsigned>(decoded.rd),
                  static_cast<unsigned>(decoded.rs1),
                  static_cast<unsigned>(decoded.rs2),
                  static_cast<int>(decoded.imm),
                  decoded.is_compressed ? "y" : "n");

        const uint64_t rs1_val = getRegister(decoded.rs1);
        const uint64_t rs2_val = getRegister(decoded.rs2);
        const uint64_t rd_before = getRegister(decoded.rd);

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

        // 更新基础性能计数器，供 benchmark 的计时/统计逻辑使用。
        constexpr uint32_t MCYCLE = 0xB00;
        constexpr uint32_t MINSTRET = 0xB02;
        constexpr uint32_t CYCLE = 0xC00;
        constexpr uint32_t INSTRET = 0xC02;
        const uint64_t retired = instruction_count_;
        csr_registers_[MCYCLE] = retired;
        csr_registers_[MINSTRET] = retired;
        csr_registers_[CYCLE] = retired;
        csr_registers_[INSTRET] = retired;

        const uint64_t rd_after = getRegister(decoded.rd);
        if (decoded.rd != 0 && rd_after != rd_before) {
            LOGT(INORDER,
                      "pc=0x%" PRIx64 " x%u:0x%" PRIx64 "->0x%" PRIx64
                      " rs1=x%u:0x%" PRIx64 " rs2=x%u:0x%" PRIx64 " imm=%d",
                      pc_before,
                      static_cast<unsigned>(decoded.rd),
                      rd_before,
                      rd_after,
                      static_cast<unsigned>(decoded.rs1),
                      rs1_val,
                      static_cast<unsigned>(decoded.rs2),
                      rs2_val,
                      static_cast<int>(decoded.imm));
        }
        
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
    while (!halted_ && !memory_->shouldExit()) {
        step();
    }
}

void CPU::reset() {
    registers_.fill(0);
    fp_registers_.fill(0);
    csr_registers_.fill(0);
    pc_ = 0;
    halted_ = false;
    instruction_count_ = 0;
    reservation_valid_ = false;
    reservation_addr_ = 0;
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
    // RV64 FLEN=64: 单精度读取时仅看低32位
    union {
        uint32_t u;
        float f;
    } conv{static_cast<uint32_t>(fp_registers_[reg])};
    return conv.f;
}

void CPU::setFPRegisterFloat(RegNum reg, float value) {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    union {
        uint32_t u;
        float f;
    } conv{};
    conv.f = value;
    // 单精度写入采用 NaN-boxing，上32位全1
    fp_registers_[reg] = 0xFFFFFFFF00000000ULL | static_cast<uint64_t>(conv.u);
}

uint64_t CPU::getCSR(uint32_t addr) const {
    if (addr >= NUM_CSR_REGISTERS) {
        throw SimulatorException("无效的CSR地址: " + std::to_string(addr));
    }
    return csr::read(csr_registers_, addr);
}

void CPU::setCSR(uint32_t addr, uint64_t value) {
    if (addr >= NUM_CSR_REGISTERS) {
        throw SimulatorException("无效的CSR地址: " + std::to_string(addr));
    }
    csr::write(csr_registers_, addr, value);
}

void CPU::dumpRegisters() const {
    std::cout << "Registers:\n";
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
    std::cout << "CPU State:\n";
    std::cout << "PC: 0x" << std::hex << pc_ << std::dec << "\n";
    std::cout << "Instructions: " << instruction_count_ << "\n";
    std::cout << "Halted: " << (halted_ ? "yes" : "no") << "\n";
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

bool CPU::executeLoadOperations(const DecodedInstruction& inst) {
    uint64_t addr = getRegister(inst.rs1) + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
    if (isDataAddressMisaligned(addr, inst.memory_access_size)) {
        raiseLoadAddressMisaligned(addr);
        return false;
    }
    uint64_t value = InstructionExecutor::loadFromMemory(memory_, addr, inst.funct3);
    setRegister(inst.rd, value);
    return true;
}

void CPU::executeJALR(const DecodedInstruction& inst) {
    uint64_t target = InstructionExecutor::calculateJumpAndLinkTarget(inst, pc_, getRegister(inst.rs1));
    if (isInstructionAddressMisaligned(target)) {
        raiseInstructionAddressMisaligned(target);
        return;
    }

    uint64_t return_addr = pc_ + (inst.is_compressed ? 2 : 4); // 根据指令长度确定返回地址
    setRegister(inst.rd, return_addr);
    pc_ = target;
}

void CPU::executeRType(const DecodedInstruction& inst) {
    if (inst.opcode == Opcode::AMO) {
        executeAtomicExtension(inst);
        return;
    }

    // 检查是否为M扩展指令
    if ((inst.opcode == Opcode::OP || inst.opcode == Opcode::OP_32) &&
        inst.funct7 == Funct7::M_EXT) {
        executeMExtension(inst);
        return;
    }
    
    // 检查是否为F扩展指令
    if (InstructionExecutor::isFloatingPointInstruction(inst)) {
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
            if (executeLoadOperations(inst)) {
                incrementPC();
            }
            break;
        case Opcode::LOAD_FP: {
            uint64_t addr = getRegister(inst.rs1) + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
            if (isDataAddressMisaligned(addr, inst.memory_access_size)) {
                raiseLoadAddressMisaligned(addr);
                break;
            }
            uint64_t value = InstructionExecutor::loadFPFromMemory(memory_, addr, inst.funct3);
            setFPRegister(inst.rd, value);
            incrementPC();
            break;
        }
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
        if (isDataAddressMisaligned(addr, inst.memory_access_size)) {
            raiseStoreAddressMisaligned(addr);
            return;
        }
        uint64_t value = getRegister(inst.rs2);
        InstructionExecutor::storeToMemory(memory_, addr, value, inst.funct3);
        reservation_valid_ = false;
        incrementPC();
    } else if (inst.opcode == Opcode::STORE_FP) {
        uint64_t addr = getRegister(inst.rs1) + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
        if (isDataAddressMisaligned(addr, inst.memory_access_size)) {
            raiseStoreAddressMisaligned(addr);
            return;
        }
        uint64_t value = getFPRegister(inst.rs2);
        InstructionExecutor::storeFPToMemory(memory_, addr, value, inst.funct3);
        reservation_valid_ = false;
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
            const uint64_t target = pc_ + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
            if (isInstructionAddressMisaligned(target)) {
                raiseInstructionAddressMisaligned(target);
                return;
            }
            pc_ = target;
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
        const uint64_t target = InstructionExecutor::calculateJumpTarget(inst, pc_);
        if (isInstructionAddressMisaligned(target)) {
            raiseInstructionAddressMisaligned(target);
            return;
        }

        // JAL: Jump and Link
        // 1. 保存返回地址（根据指令长度确定增量）
        uint32_t return_addr = pc_ + (inst.is_compressed ? 2 : 4);
        setRegister(inst.rd, return_addr);
        
        // 2. 跳转到 PC + 符号扩展的立即数
        pc_ = target;
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
            // MRET - 从机器模式异常返回到MEPC
            pc_ = getCSR(csr::kMepc);
        } else if (InstructionExecutor::isSupervisorReturn(inst)) {
            // SRET - 监管模式返回（可选实现）
            incrementPC();
        } else if (InstructionExecutor::isUserReturn(inst)) {
            // URET - 用户模式返回（可选实现）
            incrementPC();
        } else if (InstructionExecutor::isSfenceVma(inst)) {
            // SFENCE.VMA - 单核且无TLB模型，按NOP处理
            incrementPC();
        } else if (InstructionExecutor::isCsrInstruction(inst)) {
            const uint32_t csr_addr = static_cast<uint32_t>(inst.imm) & 0xFFFU;
            const auto csr_result = InstructionExecutor::executeCsrInstruction(
                inst, getRegister(inst.rs1), getCSR(csr_addr));
            setCSR(csr_addr, csr_result.write_value);
            setRegister(inst.rd, csr_result.read_value);
            incrementPC();
        } else {
            throw IllegalInstructionException("不支持的系统指令: imm=" + std::to_string(inst.imm));
        }
    } else {
        throw IllegalInstructionException("不支持的系统指令");
    }
}


void CPU::handleEcall() {
    // arch-test 里 mtvec 会初始化为 trap 入口，此时 ECALL 需要走异常陷入语义。
    if (csr::machineTrapVectorBase(csr_registers_) != 0) {
        enterMachineTrap(csr::kMachineEcallCause, 0);
        return;
    }

    // 兼容原有运行时：未配置 trap 向量时，保留宿主 syscall 行为。
    bool should_halt = syscall_handler_->handleSyscall(this);
    if (should_halt) {
        halted_ = true;
    } else {
        incrementPC();
    }
}

void CPU::handleEbreak() {
    enterMachineTrap(csr::kBreakpointCause, pc_);
}

void CPU::enterMachineTrap(uint64_t cause, uint64_t tval) {
    pc_ = csr::enterMachineTrap(csr_registers_, pc_, cause, tval);
}

bool CPU::isExtensionEnabled(Extension extension) const {
    return (enabled_extensions_ & static_cast<uint32_t>(extension)) != 0;
}

bool CPU::isInstructionAddressMisaligned(uint64_t addr) const {
    // C 扩展开启时 IALIGN=16，否则 IALIGN=32。
    if (isExtensionEnabled(Extension::C)) {
        return (addr & 0x1ULL) != 0;
    }
    return (addr & 0x3ULL) != 0;
}

bool CPU::isDataAddressMisaligned(uint64_t addr, uint8_t access_size) const {
    if (access_size <= 1) {
        return false;
    }
    return (addr & static_cast<uint64_t>(access_size - 1)) != 0;
}

void CPU::raiseInstructionAddressMisaligned(uint64_t target_addr) {
    enterMachineTrap(csr::kInstructionAddressMisalignedCause, target_addr);
}

void CPU::raiseLoadAddressMisaligned(uint64_t target_addr) {
    enterMachineTrap(csr::kLoadAddressMisalignedCause, target_addr);
}

void CPU::raiseStoreAddressMisaligned(uint64_t target_addr) {
    enterMachineTrap(csr::kStoreAddressMisalignedCause, target_addr);
}

int32_t CPU::signExtend(uint32_t value, int bits) const {
    int32_t mask = (1 << bits) - 1;
    int32_t signBit = 1 << (bits - 1);
    return (value & mask) | (((value & signBit) != 0) ? ~mask : 0);
}

void CPU::executeMExtension(const DecodedInstruction& inst) {
    uint64_t rs1_val = getRegister(inst.rs1);
    uint64_t rs2_val = getRegister(inst.rs2);

    uint64_t result = 0;
    if (inst.opcode == Opcode::OP_32) {
        result = InstructionExecutor::executeMExtension32(inst, rs1_val, rs2_val);
    } else {
        result = InstructionExecutor::executeMExtension(inst, rs1_val, rs2_val);
    }
    setRegister(inst.rd, result);
    
    incrementPC();
}

void CPU::executeFPExtension(const DecodedInstruction& inst) {
    const uint8_t current_frm = static_cast<uint8_t>(getCSR(csr::kFrm) & 0x7U);
    InstructionExecutor::FpExecuteResult fp_result;
    if (inst.opcode == Opcode::FMADD ||
        inst.opcode == Opcode::FMSUB ||
        inst.opcode == Opcode::FNMSUB ||
        inst.opcode == Opcode::FNMADD) {
        fp_result = InstructionExecutor::executeFusedFPOperation(
            inst,
            getFPRegister(inst.rs1),
            getFPRegister(inst.rs2),
            getFPRegister(inst.rs3),
            current_frm);
    } else {
        fp_result = InstructionExecutor::executeFPOperation(
            inst,
            getFPRegister(inst.rs1),
            getFPRegister(inst.rs2),
            getRegister(inst.rs1),
            current_frm);
    }

    if (fp_result.write_int_reg) {
        setRegister(inst.rd, fp_result.value);
    } else if (fp_result.write_fp_reg) {
        setFPRegister(inst.rd, fp_result.value);
    }

    if (fp_result.fflags != 0) {
        setCSR(csr::kFflags, getCSR(csr::kFflags) | fp_result.fflags);
    }

    incrementPC();
}

void CPU::executeAtomicExtension(const DecodedInstruction& inst) {
    const uint64_t addr = getRegister(inst.rs1);
    if (isDataAddressMisaligned(addr, inst.memory_access_size)) {
        raiseStoreAddressMisaligned(addr);
        return;
    }
    uint64_t memory_value = 0;
    switch (inst.funct3) {
        case Funct3::LW:
            memory_value = memory_->readWord(addr);
            break;
        case Funct3::LD:
            memory_value = memory_->read64(addr);
            break;
        default:
            throw IllegalInstructionException("A扩展仅支持W/D宽度");
    }

    const bool reservation_hit = reservation_valid_ && (reservation_addr_ == addr);
    const auto amo_result = InstructionExecutor::executeAtomicOperation(
        inst, memory_value, getRegister(inst.rs2), reservation_hit);

    if (amo_result.acquire_reservation) {
        reservation_valid_ = true;
        reservation_addr_ = addr;
    }
    if (amo_result.release_reservation) {
        reservation_valid_ = false;
    }

    if (amo_result.do_store) {
        if (inst.funct3 == Funct3::LW) {
            memory_->writeWord(addr, static_cast<uint32_t>(amo_result.store_value));
        } else {
            memory_->write64(addr, amo_result.store_value);
        }
    }

    setRegister(inst.rd, amo_result.rd_value);
    incrementPC();
}

} // namespace riscv
