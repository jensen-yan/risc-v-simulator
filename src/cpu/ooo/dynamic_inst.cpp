#include "cpu/ooo/dynamic_inst.h"
#include "common/debug_types.h"
#include <sstream>
#include <iomanip>

namespace riscv {

// ========== 构造函数实现 ==========
DynamicInst::DynamicInst() 
    : instruction_id_(0), pc_(0), status_(Status::ALLOCATED),
      logical_dest_(0), physical_dest_(0), logical_src1_(0), logical_src2_(0),
      physical_src1_(0), physical_src2_(0),
      src1_ready_(false), src2_ready_(false), src1_value_(0), src2_value_(0),
      result_(0), result_ready_(false), has_exception_(false),
      rob_entry_(0), rs_entry_(0), is_jump_(false), jump_target_(0),
      fetch_cycle_(0), decode_cycle_(0), issue_cycle_(0), 
      execute_cycle_(0), complete_cycle_(0), retire_cycle_(0) {
}

DynamicInst::DynamicInst(const DecodedInstruction& decoded_info, uint64_t pc, uint64_t instruction_id)
    : decoded_info_(decoded_info), instruction_id_(instruction_id), pc_(pc), 
      status_(Status::ALLOCATED),
      logical_dest_(0), physical_dest_(0), logical_src1_(0), logical_src2_(0),
      physical_src1_(0), physical_src2_(0),
      src1_ready_(false), src2_ready_(false), src1_value_(0), src2_value_(0),
      result_(0), result_ready_(false), has_exception_(false),
      rob_entry_(0), rs_entry_(0), is_jump_(false), jump_target_(0),
      fetch_cycle_(0), decode_cycle_(0), issue_cycle_(0), 
      execute_cycle_(0), complete_cycle_(0), retire_cycle_(0) {
    
    initialize_from_decoded_instruction();
}

// ========== 指令类型判断实现 ==========
bool DynamicInst::is_load_instruction() const {
    return decoded_info_.opcode == Opcode::LOAD;
}

bool DynamicInst::is_store_instruction() const {
    return decoded_info_.opcode == Opcode::STORE;
}

bool DynamicInst::is_branch_instruction() const {
    return decoded_info_.opcode == Opcode::BRANCH;
}

bool DynamicInst::is_jump_instruction() const {
    return decoded_info_.opcode == Opcode::JAL || 
           decoded_info_.opcode == Opcode::JALR;
}

// 设置跳转状态时的调试日志
void DynamicInst::set_jump_info(bool is_jump, uint64_t target) {
    is_jump_ = is_jump;
    jump_target_ = target;
    if (is_jump_) {
        dprintf(EXECUTE, "[JUMP_SET] Inst#%" PRId64 " PC=0x%" PRIx64 " 设置为跳转指令，目标=0x%" PRIx64, 
                instruction_id_, pc_, target);
    }
}


bool DynamicInst::is_alu_instruction() const {
    return decoded_info_.opcode == Opcode::OP ||
           decoded_info_.opcode == Opcode::OP_IMM ||
           decoded_info_.opcode == Opcode::LUI ||
           decoded_info_.opcode == Opcode::AUIPC;
}

// ========== 执行单元类型获取 ==========
ExecutionUnitType DynamicInst::get_required_execution_unit() const {
    if (is_load_instruction()) {
        return ExecutionUnitType::LOAD;
    } else if (is_store_instruction()) {
        return ExecutionUnitType::STORE;
    } else if (is_branch_instruction() || is_jump_instruction()) {
        return ExecutionUnitType::BRANCH;
    } else {
        return ExecutionUnitType::ALU;
    }
}

// ========== 状态转换辅助函数 ==========
const char* DynamicInst::status_to_string(Status status) {
    switch (status) {
        case Status::ALLOCATED:  return "ALLOCATED";
        case Status::ISSUED:     return "ISSUED";
        case Status::EXECUTING:  return "EXECUTING";
        case Status::COMPLETED:  return "COMPLETED";
        case Status::RETIRED:    return "RETIRED";
        default:                 return "UNKNOWN";
    }
}

// ========== 调试和序列化实现 ==========
std::string DynamicInst::to_string() const {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    ss << "DynamicInst[ID=" << std::dec << instruction_id_ 
       << ", PC=0x" << std::hex << std::setw(8) << pc_
       << ", Status=" << status_to_string(status_)
       << ", Opcode=0x" << std::setw(2) << static_cast<int>(decoded_info_.opcode);
    
    if (logical_dest_ != 0) {
        ss << ", Dest=x" << std::dec << static_cast<int>(logical_dest_)
           << "->p" << static_cast<int>(physical_dest_);
    }
    
    if (logical_src1_ != 0 || logical_src2_ != 0) {
        ss << ", Src1=x" << std::dec << static_cast<int>(logical_src1_)
           << "(p" << static_cast<int>(physical_src1_) 
           << "," << (src1_ready_ ? "R" : "W") << ")";
        ss << ", Src2=x" << static_cast<int>(logical_src2_)
           << "(p" << static_cast<int>(physical_src2_) 
           << "," << (src2_ready_ ? "R" : "W") << ")";
    }
    
    if (result_ready_) {
        ss << ", Result=0x" << std::hex << std::setw(8) << result_;
    }
    
    if (has_exception_) {
        ss << ", Exception=\"" << exception_msg_ << "\"";
    }
    
    if (is_jump_) {
        ss << ", Jump->0x" << std::hex << std::setw(8) << jump_target_;
    }
    
    ss << "]";
    return ss.str();
}

void DynamicInst::dump_state() const {
    std::cout << "=== DynamicInst State Dump ===" << std::endl;
    std::cout << "Basic Info:" << std::endl;
    std::cout << "  Instruction ID: " << instruction_id_ << std::endl;
    std::cout << "  PC: 0x" << std::hex << pc_ << std::dec << std::endl;
    std::cout << "  Status: " << status_to_string(status_) << std::endl;
    
    std::cout << "Instruction Info:" << std::endl;
    std::cout << "  Opcode: 0x" << std::hex << static_cast<int>(decoded_info_.opcode) << std::dec << std::endl;
    std::cout << "  Type: " << static_cast<int>(decoded_info_.type) << std::endl;
    std::cout << "  Immediate: " << decoded_info_.imm << std::endl;
    
    std::cout << "Register Info:" << std::endl;
    std::cout << "  Dest: x" << static_cast<int>(logical_dest_) 
              << " -> p" << static_cast<int>(physical_dest_) << std::endl;
    std::cout << "  Src1: x" << static_cast<int>(logical_src1_) 
              << " -> p" << static_cast<int>(physical_src1_) 
              << " (" << (src1_ready_ ? "Ready" : "Waiting") << ")" << std::endl;
    std::cout << "  Src2: x" << static_cast<int>(logical_src2_) 
              << " -> p" << static_cast<int>(physical_src2_) 
              << " (" << (src2_ready_ ? "Ready" : "Waiting") << ")" << std::endl;
    
    if (src1_ready_) {
        std::cout << "  Src1 Value: 0x" << std::hex << src1_value_ << std::dec << std::endl;
    }
    if (src2_ready_) {
        std::cout << "  Src2 Value: 0x" << std::hex << src2_value_ << std::dec << std::endl;
    }
    
    std::cout << "Execution Info:" << std::endl;
    std::cout << "  Ready to Execute: " << (is_ready_to_execute() ? "Yes" : "No") << std::endl;
    std::cout << "  Required Unit: " << static_cast<int>(get_required_execution_unit()) << std::endl;
    std::cout << "  Result Ready: " << (result_ready_ ? "Yes" : "No") << std::endl;
    if (result_ready_) {
        std::cout << "  Result: 0x" << std::hex << result_ << std::dec << std::endl;
    }
    
    if (has_exception_) {
        std::cout << "Exception: " << exception_msg_ << std::endl;
    }
    
    if (is_jump_) {
        std::cout << "Jump Info:" << std::endl;
        std::cout << "  Jump Target: 0x" << std::hex << jump_target_ << std::dec << std::endl;
    }
    
    std::cout << "ROB/RS Info:" << std::endl;
    std::cout << "  ROB Entry: " << rob_entry_ << std::endl;
    std::cout << "  RS Entry: " << rs_entry_ << std::endl;
    
    std::cout << "Timing Info:" << std::endl;
    std::cout << "  Fetch: " << fetch_cycle_ << std::endl;
    std::cout << "  Decode: " << decode_cycle_ << std::endl;
    std::cout << "  Issue: " << issue_cycle_ << std::endl;
    std::cout << "  Execute: " << execute_cycle_ << std::endl;
    std::cout << "  Complete: " << complete_cycle_ << std::endl;
    std::cout << "  Retire: " << retire_cycle_ << std::endl;
    
    std::cout << "===========================" << std::endl;
}

// ========== 状态重置实现 ==========
void DynamicInst::reset_to_allocated() {
    status_ = Status::ALLOCATED;
    result_ready_ = false;
    has_exception_ = false;
    exception_msg_.clear();
    is_jump_ = false;
    jump_target_ = 0;
    rs_entry_ = 0;
    
    // 保留 ROB entry、instruction_id、pc 等基础信息
    // 重置执行相关的周期信息
    execute_cycle_ = 0;
    complete_cycle_ = 0;
    retire_cycle_ = 0;
}

// ========== 私有初始化函数实现 ==========
void DynamicInst::initialize_from_decoded_instruction() {
    extract_register_info();
    setup_execution_requirements();
}

void DynamicInst::extract_register_info() {
    // 提取寄存器信息
    logical_dest_ = decoded_info_.rd;
    logical_src1_ = decoded_info_.rs1;
    logical_src2_ = decoded_info_.rs2;
    
    // x0 寄存器总是准备好且值为0
    if (logical_src1_ == 0) {
        src1_ready_ = true;
        src1_value_ = 0;
    }
    if (logical_src2_ == 0) {
        src2_ready_ = true;
        src2_value_ = 0;
    }
    
    // 对于立即数指令，第二个操作数总是准备好的
    if (decoded_info_.type == InstructionType::I_TYPE ||
        decoded_info_.type == InstructionType::U_TYPE ||
        decoded_info_.type == InstructionType::J_TYPE) {
        src2_ready_ = true;
        src2_value_ = static_cast<uint64_t>(decoded_info_.imm);
    }
}

void DynamicInst::setup_execution_requirements() {
    // 设置执行单元需求
    ExecutionUnitType unit_type = get_required_execution_unit();
    
    // 初始化扩展信息
    exec_info_ = ExecutionInfo();
    exec_info_->required_unit_type = unit_type;
    
    // 根据指令类型设置执行周期
    switch (unit_type) {
        case ExecutionUnitType::ALU:
            exec_info_->execution_cycles = 1;
            break;
        case ExecutionUnitType::LOAD:
            exec_info_->execution_cycles = 2;  // 加载指令需要2个周期
            exec_info_->has_memory_dependency = true;
            memory_info_ = MemoryInfo();
            memory_info_->is_memory_op = true;
            memory_info_->is_load = true;
            break;
        case ExecutionUnitType::STORE:
            exec_info_->execution_cycles = 1;
            exec_info_->has_memory_dependency = true;
            memory_info_ = MemoryInfo();
            memory_info_->is_memory_op = true;
            memory_info_->is_store = true;
            break;
        case ExecutionUnitType::BRANCH:
            exec_info_->execution_cycles = 1;
            if (is_branch_instruction()) {
                branch_info_ = BranchInfo();
                branch_info_->is_branch = true;
            }
            break;
    }
    
    exec_info_->remaining_cycles = exec_info_->execution_cycles;
}

// ========== 工厂函数实现 ==========
DynamicInstPtr create_dynamic_inst(const DecodedInstruction& decoded_info, 
                                  uint64_t pc, uint64_t instruction_id) {
    return std::make_shared<DynamicInst>(decoded_info, pc, instruction_id);
}

} // namespace riscv