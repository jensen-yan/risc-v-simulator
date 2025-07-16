#include "ooo_cpu.h"
#include "syscall_handler.h"
#include "instruction_executor.h"
#include "debug_types.h"
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace riscv {

OutOfOrderCPU::OutOfOrderCPU(std::shared_ptr<Memory> memory) 
    : memory_(memory), pc_(0), halted_(false), instruction_count_(0), cycle_count_(0),
      enabled_extensions_(static_cast<uint32_t>(Extension::I) | static_cast<uint32_t>(Extension::M) | 
                         static_cast<uint32_t>(Extension::F) | static_cast<uint32_t>(Extension::C)),
      branch_mispredicts_(0), pipeline_stalls_(0), global_instruction_id_(0) {
    
    initialize_components();
    initialize_registers();
    initialize_execution_units();
    
    // 初始化系统调用处理器
    syscall_handler_ = std::make_unique<SyscallHandler>(memory_);
    
    std::cout << "乱序执行CPU初始化完成" << std::endl;
}

OutOfOrderCPU::~OutOfOrderCPU() = default;

void OutOfOrderCPU::initialize_components() {
    // 初始化乱序执行组件
    register_rename_ = std::make_unique<RegisterRenameUnit>();
    reservation_station_ = std::make_unique<ReservationStation>();
    reorder_buffer_ = std::make_unique<ReorderBuffer>();
    
    std::cout << "乱序执行组件初始化完成" << std::endl;
}

void OutOfOrderCPU::initialize_registers() {
    // 初始化架构寄存器
    arch_registers_.fill(0);
    arch_fp_registers_.fill(0);
    
    // 初始化物理寄存器
    physical_registers_.fill(0);
    physical_fp_registers_.fill(0);
    
    std::cout << "寄存器文件初始化完成" << std::endl;
}

void OutOfOrderCPU::initialize_execution_units() {
    // 初始化ALU单元
    for (auto& unit : alu_units_) {
        unit.busy = false;
        unit.remaining_cycles = 0;
        unit.has_exception = false;
        unit.is_jump = false;
        unit.jump_target = 0;
    }
    
    // 初始化分支单元
    for (auto& unit : branch_units_) {
        unit.busy = false;
        unit.remaining_cycles = 0;
        unit.has_exception = false;
        unit.is_jump = false;
        unit.jump_target = 0;
    }
    
    // 初始化加载单元
    for (auto& unit : load_units_) {
        unit.busy = false;
        unit.remaining_cycles = 0;
        unit.has_exception = false;
        unit.is_jump = false;
        unit.jump_target = 0;
    }
    
    // 初始化存储单元
    for (auto& unit : store_units_) {
        unit.busy = false;
        unit.remaining_cycles = 0;
        unit.has_exception = false;
        unit.is_jump = false;
        unit.jump_target = 0;
    }

}

void OutOfOrderCPU::step() {
    if (halted_) {
        return;
    }
    
    try {
        // 更新全局调试上下文
        DebugContext::getInstance().setCycle(cycle_count_);

        
        // 乱序执行流水线各阶段
        commit_stage();      // 提交阶段（最先执行，维护程序顺序）
        writeback_stage();   // 写回阶段
        execute_stage();     // 执行阶段
        issue_stage();       // 发射阶段
        decode_stage();      // 译码阶段
        fetch_stage();       // 取指阶段
        
        cycle_count_++;
        
        // 更新执行单元状态
        update_execution_units();
        
        // 简单的停机条件检查
        if (cycle_count_ > 10000) {
            std::cout << "警告: 执行周期数超过10000，自动停止" << std::endl;
            halted_ = true;
        }
        
    } catch (const MemoryException& e) {
        handle_exception(e.what(), pc_);
    } catch (const SimulatorException& e) {
        handle_exception(e.what(), pc_);
    }
}

void OutOfOrderCPU::run() {
    while (!halted_) {
        step();
    }
}

void OutOfOrderCPU::reset() {
    // 重置CPU状态
    pc_ = 0;
    halted_ = false;
    instruction_count_ = 0;
    cycle_count_ = 0;
    branch_mispredicts_ = 0;
    pipeline_stalls_ = 0;
    
    // 重置寄存器
    initialize_registers();
    
    // 重置执行单元
    initialize_execution_units();
    
    // 重置乱序执行组件
    register_rename_ = std::make_unique<RegisterRenameUnit>();
    reservation_station_ = std::make_unique<ReservationStation>();
    reorder_buffer_ = std::make_unique<ReorderBuffer>();
    
    // 清空缓冲区
    while (!fetch_buffer_.empty()) {
        fetch_buffer_.pop();
    }
    while (!cdb_queue_.empty()) {
        cdb_queue_.pop();
    }
    
    std::cout << "乱序执行CPU重置完成" << std::endl;
}

void OutOfOrderCPU::fetch_stage() {
    print_stage_activity("FETCH", "开始取指阶段");
    
    // 如果已经停机，不再取指
    if (halted_) {
        print_stage_activity("FETCH", "CPU已停机，跳过取指");
        return;
    }
    
    // 如果取指缓冲区有空间，取指令
    if (fetch_buffer_.size() < 4) {  // 最多缓存4条指令
        try {
            Instruction raw_inst = memory_->fetchInstruction(pc_);
            
            // 如果指令为0，可能表明程序结束，但不要立即停机
            // 要等待流水线中的指令全部完成提交
            if (raw_inst == 0) {
                print_stage_activity("FETCH", "取指到空指令(0x0)，停止取指但等待流水线清空");
                
                // 检查是否还有未完成的指令
                if (reorder_buffer_->is_empty() && fetch_buffer_.empty() && cdb_queue_.empty()) {
                    halted_ = true;
                    print_stage_activity("FETCH", "流水线已清空，程序结束");
                }
                return;
            }
            
            FetchedInstruction fetched;
            fetched.pc = pc_;
            fetched.instruction = raw_inst;
            
            // 检查是否为压缩指令
            if ((raw_inst & 0x03) != 0x03) {
                fetched.is_compressed = true;
                pc_ += 2;
                std::stringstream ss;
                ss << "取指令 0x" << std::hex << raw_inst << " (压缩指令，PC+2)";
                print_stage_activity("FETCH", ss.str());
            } else {
                fetched.is_compressed = false;
                pc_ += 4;
                std::stringstream ss;
                ss << "取指令 0x" << std::hex << raw_inst << " (正常指令，PC+4)";
                print_stage_activity("FETCH", ss.str());
            }
            
            fetch_buffer_.push(fetched);
            
        } catch (const MemoryException& e) {
            // 取指失败，停止取指但等待流水线清空
            print_stage_activity("FETCH", "取指失败，停止取指但等待流水线清空: " + std::string(e.what()));
            
            // 检查是否还有未完成的指令
            if (reorder_buffer_->is_empty() && fetch_buffer_.empty() && cdb_queue_.empty()) {
                halted_ = true;
                print_stage_activity("FETCH", "流水线已清空，程序结束");
            }
            return;
        }
    } else {
        print_stage_activity("FETCH", "取指缓冲区已满(大小=" + std::to_string(fetch_buffer_.size()) + ")，跳过取指");
    }
    
    // 每个周期结束时检查是否应该停机
    // 如果没有更多指令可取，且流水线为空，则停机
    if (pc_ >= memory_->getSize() || 
        (reorder_buffer_->is_empty() && fetch_buffer_.empty() && cdb_queue_.empty())) {
        // 检查是否还有任何正在执行的指令
        bool has_busy_units = false;
        for (const auto& unit : alu_units_) {
            if (unit.busy) has_busy_units = true;
        }
        for (const auto& unit : branch_units_) {
            if (unit.busy) has_busy_units = true;
        }
        for (const auto& unit : load_units_) {
            if (unit.busy) has_busy_units = true;
        }
        for (const auto& unit : store_units_) {
            if (unit.busy) has_busy_units = true;
        }
        
        if (!has_busy_units && reorder_buffer_->is_empty()) {
            halted_ = true;
            print_stage_activity("FETCH", "所有指令完成，CPU停机");
        }
    }
}

void OutOfOrderCPU::decode_stage() {
    print_stage_activity("DECODE", "开始译码阶段");
    
    // 如果取指缓冲区为空，无法译码
    if (fetch_buffer_.empty()) {
        print_stage_activity("DECODE", "取指缓冲区为空，跳过译码");
        return;
    }
    
    // 如果ROB已满，无法译码
    if (!reorder_buffer_->has_free_entry()) {
        print_stage_activity("DECODE", "ROB已满，译码停顿");
        pipeline_stalls_++;
        return;
    }
    
    // 取出一条指令进行译码
    FetchedInstruction fetched = fetch_buffer_.front();
    fetch_buffer_.pop();
    
    // 分配全局指令序号
    uint64_t instruction_id = ++global_instruction_id_;
    
    // 解码指令
    DecodedInstruction decoded;
    if (fetched.is_compressed) {
        decoded = decoder_.decodeCompressed(static_cast<uint16_t>(fetched.instruction), enabled_extensions_);
        print_stage_activity("DECODE", " 译码完成");
    } else {
        decoded = decoder_.decode(fetched.instruction, enabled_extensions_);
        print_stage_activity("DECODE", " 译码完成");
    }
    
    // 分配ROB表项
    auto rob_result = reorder_buffer_->allocate_entry(decoded, fetched.pc);
    if (!rob_result.success) {
        // ROB分配失败，放回取指缓冲区
        fetch_buffer_.push(fetched);
        print_stage_activity("DECODE", "ROB分配失败，指令放回缓冲区");
        pipeline_stalls_++;
        return;
    }
    
    // 设置指令序号
    reorder_buffer_->set_instruction_id(rob_result.rob_entry, instruction_id);
    
    print_stage_activity("DECODE", " 分配到ROB[" + std::to_string(rob_result.rob_entry) + "]");
    
    // 继续到发射阶段的处理将在issue_stage中完成
    // 这里我们需要一个中间缓冲区来存储译码后的指令
    // 简化实现：直接在issue_stage中处理
}

void OutOfOrderCPU::issue_stage() {
    print_stage_activity("ISSUE", "开始发射阶段");
    
    // 检查ROB中是否有待发射的指令
    if (reorder_buffer_->is_empty()) {
        print_stage_activity("ISSUE", "ROB为空，跳过发射");
        return;
    }
    
    // 遍历ROB，找到状态为ISSUED但还没有发射到保留站的指令
    // 这里简化实现：处理ROB中第一条状态为ISSUED的指令
    
    // 获取可以发射的指令
    auto dispatchable_entry = reorder_buffer_->get_dispatchable_entry();
    if (dispatchable_entry == ReorderBuffer::MAX_ROB_ENTRIES) {  // 没有可发射的指令
        print_stage_activity("ISSUE", "没有可发射的指令");
        return;
    }
    
    const auto& rob_entry = reorder_buffer_->get_entry(dispatchable_entry);
    if (!rob_entry.valid || rob_entry.state != ReorderBufferEntry::State::ALLOCATED) {
        print_stage_activity("ISSUE", "ROB表项状态不正确");
        return;
    }
    
    print_stage_activity("ISSUE", "尝试发射 Inst#" + std::to_string(rob_entry.instruction_id) + 
                        " (ROB[" + std::to_string(dispatchable_entry) + "])");
    
    // 检查保留站是否有空闲表项
    if (!reservation_station_->has_free_entry()) {
        print_stage_activity("ISSUE", "保留站已满，发射停顿");
        pipeline_stalls_++;
        return;
    }
    
    // 进行寄存器重命名
    auto rename_result = register_rename_->rename_instruction(rob_entry.instruction);
    if (!rename_result.success) {
        print_stage_activity("ISSUE", "寄存器重命名失败，发射停顿");
        pipeline_stalls_++;
        return;
    }
    
    // 准备保留站表项
    ReservationStationEntry rs_entry;
    rs_entry.instruction = rob_entry.instruction;
    rs_entry.instruction_id = rob_entry.instruction_id;
    rs_entry.src1_reg = rename_result.src1_reg;
    rs_entry.src2_reg = rename_result.src2_reg;
    rs_entry.dest_reg = rename_result.dest_reg;
    rs_entry.pc = rob_entry.pc;
    rs_entry.rob_entry = dispatchable_entry;
    rs_entry.valid = true;
    
    // 检查操作数是否准备好
    rs_entry.src1_ready = rename_result.src1_ready;
    rs_entry.src2_ready = rename_result.src2_ready;
    
    // 获取操作数值
    rs_entry.src1_value = rename_result.src1_value;
    rs_entry.src2_value = rename_result.src2_value;
    
    // 发射到保留站
    auto issue_result = reservation_station_->issue_instruction(rs_entry);
    if (!issue_result.success) {
        print_stage_activity("ISSUE", "保留站发射失败，回退重命名");
        register_rename_->release_physical_register(rename_result.dest_reg);
        pipeline_stalls_++;
        return;
    }
    
    print_stage_activity("ISSUE", "Inst#" + std::to_string(rob_entry.instruction_id) + 
                        " 成功发射到保留站RS[" + std::to_string(issue_result.rs_entry) + "]");
    
    // 更新ROB表项状态，标记为已发射到保留站
    reorder_buffer_->mark_as_dispatched(dispatchable_entry);
}

void OutOfOrderCPU::execute_stage() {
    print_stage_activity("EXECUTE", "开始执行阶段");
    
    // 尝试从保留站调度指令到执行单元
    auto dispatch_result = reservation_station_->dispatch_instruction();
    if (dispatch_result.success) {
        print_stage_activity("EXECUTE", "从保留站调度指令 RS[" + std::to_string(dispatch_result.rs_entry) + 
                            "] Inst#" + std::to_string(dispatch_result.instruction.instruction_id) + 
                            " 到执行单元");
        
        ExecutionUnit* unit = get_available_unit(dispatch_result.unit_type);
        if (unit) {
            unit->busy = true;
            unit->instruction = dispatch_result.instruction;
            unit->has_exception = false;
            
            std::string unit_type_str;
            int cycle_count = 0;
            
            // 根据指令类型设置执行周期
            switch (dispatch_result.unit_type) {
                case ExecutionUnitType::ALU:
                    unit_type_str = "ALU";
                    unit->remaining_cycles = 1;
                    cycle_count = 1;
                    break;
                case ExecutionUnitType::BRANCH:
                    unit_type_str = "BRANCH";
                    unit->remaining_cycles = 1;
                    cycle_count = 1;
                    break;
                case ExecutionUnitType::LOAD:
                    unit_type_str = "LOAD";
                    unit->remaining_cycles = 2;  // 加载指令需要2个周期
                    cycle_count = 2;
                    break;
                case ExecutionUnitType::STORE:
                    unit_type_str = "STORE";
                    unit->remaining_cycles = 1;
                    cycle_count = 1;
                    break;
            }
            
            print_stage_activity("EXECUTE", "指令开始在 " + unit_type_str + " 单元执行，需要 " + 
                                std::to_string(cycle_count) + " 个周期");
            
            // 开始执行指令
            execute_instruction(*unit, dispatch_result.instruction);
        } else {
            print_stage_activity("EXECUTE", "没有可用的执行单元");
        }
    } else {
        print_stage_activity("EXECUTE", "保留站没有准备好的指令可调度");
    }
}

void OutOfOrderCPU::execute_instruction(ExecutionUnit& unit, const ReservationStationEntry& entry) {
    try {
        const auto& inst = entry.instruction;
        
        switch (inst.type) {
            case InstructionType::R_TYPE:
                // 寄存器-寄存器运算
                unit.result = InstructionExecutor::executeRegisterOperation(inst, entry.src1_value, entry.src2_value);
                break;
                
            case InstructionType::I_TYPE:
                if (inst.opcode == Opcode::OP_IMM) {
                    // 立即数运算
                    unit.result = InstructionExecutor::executeImmediateOperation(inst, entry.src1_value);
                } else if (inst.opcode == Opcode::LOAD) {
                    // 加载指令
                    uint32_t addr = entry.src1_value + inst.imm;
                    unit.result = InstructionExecutor::loadFromMemory(memory_, addr, inst.funct3);
                } else if (inst.opcode == Opcode::SYSTEM) {
                    // 系统调用 - 不需要计算结果
                    unit.result = 0;
                } else {
                    unit.has_exception = true;
                    unit.exception_msg = "不支持的I-type指令";
                }
                break;
                
            case InstructionType::B_TYPE:
                // 分支指令（BNE, BEQ, BLT等）
                {
                    bool should_branch = InstructionExecutor::evaluateBranchCondition(inst, entry.src1_value, entry.src2_value);
                    
                    // 设置分支结果（分支指令通常不写回寄存器，但需要完成执行）
                    unit.result = 0;  // 分支指令没有写回值
                    
                    if (should_branch) {
                        // 分支taken：条件成立，需要跳转
                        unit.jump_target = entry.pc + inst.imm;
                        unit.is_jump = true;  // 标记需要改变PC
                        
                        // 简单的分支预测检查（总是预测不跳转）
                        // 分支预测错误，需要刷新流水线
                        print_stage_activity("EXECUTE", "分支指令 taken，目标地址: 0x" + 
                                            std::to_string(unit.jump_target) + " (分支预测错误)");
                        flush_pipeline();
                        branch_mispredicts_++;
                    } else {
                        // 分支not taken：条件不成立，继续顺序执行
                        unit.is_jump = false;  // 不需要改变PC
                        unit.jump_target = 0;
                        print_stage_activity("EXECUTE", "分支指令 not taken (分支预测正确)");
                    }
                }
                break;
                
            case InstructionType::S_TYPE:
                // 存储指令
                {
                    uint32_t addr = entry.src1_value + inst.imm;
                    InstructionExecutor::storeToMemory(memory_, addr, entry.src2_value, inst.funct3);
                }
                break;
                
            case InstructionType::U_TYPE:
                // 上位立即数指令
                unit.result = InstructionExecutor::executeUpperImmediate(inst, entry.pc);
                break;
                
            case InstructionType::J_TYPE:
                // 跳转指令（JAL, JALR）- 无条件跳转
                unit.result = entry.pc + (inst.is_compressed ? 2 : 4);  // 返回地址
                // 在乱序执行中，不在执行阶段修改PC，而是在提交阶段修改
                // 将跳转目标保存到额外的字段中
                unit.jump_target = InstructionExecutor::calculateJumpTarget(inst, entry.pc);
                unit.is_jump = true;  // 无条件跳转总是需要改变PC
                break;
                
            default:
                unit.has_exception = true;
                unit.exception_msg = "不支持的指令类型";
                break;
        }
        
    } catch (const SimulatorException& e) {
        unit.has_exception = true;
        unit.exception_msg = e.what();
    }
}

bool OutOfOrderCPU::execute_branch_operation(const DecodedInstruction& inst, uint32_t src1, uint32_t src2) {
    bool should_branch = InstructionExecutor::evaluateBranchCondition(inst, src1, src2);
    
    // 简化的分支预测检查
    bool predicted = predict_branch(pc_);
    if (should_branch != predicted) {
        // 分支预测错误
        if (should_branch) {
            pc_ = pc_ + inst.imm;
        }
        update_branch_predictor(pc_, should_branch);
        return true;  // 需要刷新流水线
    }
    
    return false;
}

void OutOfOrderCPU::execute_store_operation(const DecodedInstruction& inst, uint32_t src1, uint32_t src2) {
    uint32_t addr = src1 + inst.imm;
    InstructionExecutor::storeToMemory(memory_, addr, src2, inst.funct3);
}

void OutOfOrderCPU::writeback_stage() {
    print_stage_activity("WRITEBACK", "开始写回阶段");
    
    // 检查执行单元是否完成执行
    update_execution_units();
    
    // 处理CDB队列中的写回请求
    while (!cdb_queue_.empty()) {
        CommonDataBusEntry cdb_entry = cdb_queue_.front();
        cdb_queue_.pop();
        
        print_stage_activity("WRITEBACK", "CDB写回: ROB[" + std::to_string(cdb_entry.rob_entry) + 
                            "] p" + std::to_string(cdb_entry.dest_reg) + 
                            " = 0x" + std::to_string(cdb_entry.value));
        
        // 更新保留站中的操作数
        reservation_station_->update_operands(cdb_entry);
        
        // 更新寄存器重命名映射
        register_rename_->update_physical_register(cdb_entry.dest_reg, cdb_entry.value, cdb_entry.rob_entry);
        
        // 更新ROB表项
        reorder_buffer_->update_entry(cdb_entry.rob_entry, cdb_entry.value, false, "",
                                     cdb_entry.is_jump, cdb_entry.jump_target);
        
        print_stage_activity("WRITEBACK", "ROB[" + std::to_string(cdb_entry.rob_entry) + "] 状态更新为COMPLETED");
    }
    
    if (cdb_queue_.empty()) {
        print_stage_activity("WRITEBACK", "CDB队列为空，无写回操作");
    }
}

void OutOfOrderCPU::commit_stage() {
    print_stage_activity("COMMIT", "开始提交阶段");
    
    // 添加ROB状态检查
    if (reorder_buffer_->is_empty()) {
        print_stage_activity("COMMIT", "ROB为空，无法提交");
        return;
    }
    
    // 检查头部指令的状态
    auto head_entry_id = reorder_buffer_->get_head_entry();
    if (head_entry_id == ReorderBuffer::MAX_ROB_ENTRIES) {
        print_stage_activity("COMMIT", "没有有效的头部表项");
        return;
    }
    
    const auto& head_entry = reorder_buffer_->get_entry(head_entry_id);
    std::string state_str;
    switch (head_entry.state) {
        case ReorderBufferEntry::State::ALLOCATED: state_str = "ALLOCATED"; break;
        case ReorderBufferEntry::State::ISSUED: state_str = "ISSUED"; break;
        case ReorderBufferEntry::State::EXECUTING: state_str = "EXECUTING"; break;
        case ReorderBufferEntry::State::COMPLETED: state_str = "COMPLETED"; break;
        case ReorderBufferEntry::State::RETIRED: state_str = "RETIRED"; break;
    }
    
    print_stage_activity("COMMIT", "头部指令 ROB[" + std::to_string(head_entry_id) + 
                        "] Inst#" + std::to_string(head_entry.instruction_id) + 
                        " 状态: " + state_str + 
                        " 结果准备: " + (head_entry.result_ready ? "是" : "否"));
    
    if (!reorder_buffer_->can_commit()) {
        print_stage_activity("COMMIT", "头部指令尚未完成，无法提交");
        return;
    }
    
    // 尝试提交指令
    while (reorder_buffer_->can_commit()) {
        auto commit_result = reorder_buffer_->commit_instruction();
        if (!commit_result.success) {
            print_stage_activity("COMMIT", "提交失败: " + commit_result.error_message);
            break;
        }
        
        const auto& committed_inst = commit_result.instruction;
        
        // 检查是否有异常
        if (committed_inst.has_exception) {
            print_stage_activity("COMMIT", "提交异常指令: " + committed_inst.exception_msg);
            handle_exception(committed_inst.exception_msg, committed_inst.pc);
            break;
        }
        
        // 提交到架构寄存器
        if (committed_inst.instruction.rd != 0) {  // x0寄存器不能写入
            arch_registers_[committed_inst.instruction.rd] = committed_inst.result;
            print_stage_activity("COMMIT", "Inst#" + std::to_string(committed_inst.instruction_id) + 
                                " x" + std::to_string(committed_inst.instruction.rd) + 
                                " = 0x" + std::to_string(committed_inst.result));
        } else {
            print_stage_activity("COMMIT", "Inst#" + std::to_string(committed_inst.instruction_id) + 
                                " (无目标寄存器)");
        }
        
        // 释放物理寄存器
        register_rename_->commit_instruction(committed_inst.logical_dest, committed_inst.physical_dest);
        
        instruction_count_++;
        
        // 处理跳转指令：只有is_jump=true的指令才会改变PC
        if (committed_inst.is_jump) {
            pc_ = committed_inst.jump_target;
            std::stringstream ss;
            ss << "Inst#" << committed_inst.instruction_id << " 跳转到 0x" << std::hex << committed_inst.jump_target;
            print_stage_activity("COMMIT", ss.str());
        }
        
        // 处理系统调用
        if (committed_inst.instruction.opcode == Opcode::SYSTEM) {
            if (InstructionExecutor::isSystemCall(committed_inst.instruction)) {
                // ECALL
                handleEcall();
            } else if (InstructionExecutor::isBreakpoint(committed_inst.instruction)) {
                // EBREAK
                handleEbreak();
            }
        }
        
        // 如果没有更多指令可提交，跳出循环
        if (!commit_result.has_more) {
            print_stage_activity("COMMIT", "没有更多指令可提交");
            break;
        }
    }
}

void OutOfOrderCPU::update_execution_units() {
    // 更新ALU单元
    for (size_t i = 0; i < alu_units_.size(); ++i) {
        auto& unit = alu_units_[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            print_stage_activity("EXECUTE", "ALU" + std::to_string(i) + " 执行中，剩余周期: " + 
                                std::to_string(unit.remaining_cycles));
            
            if (unit.remaining_cycles <= 0) {
                // 执行完成，发送到CDB
                CommonDataBusEntry cdb_entry;
                cdb_entry.dest_reg = unit.instruction.dest_reg;
                cdb_entry.value = unit.result;
                cdb_entry.rob_entry = unit.instruction.rob_entry;
                cdb_entry.valid = true;
                cdb_entry.is_jump = unit.is_jump;
                cdb_entry.jump_target = unit.jump_target;
                
                cdb_queue_.push(cdb_entry);
                
                print_stage_activity("EXECUTE", "ALU" + std::to_string(i) + " 执行完成，" +
                                    "Inst#" + std::to_string(unit.instruction.instruction_id) + 
                                    " 结果 0x" + std::to_string(unit.result) + " 发送到CDB");
                
                // 释放执行单元
                unit.busy = false;
                unit.has_exception = false;
                unit.exception_msg.clear();
                unit.is_jump = false;
                unit.jump_target = 0;
                
                // 释放保留站中的执行单元状态
                reservation_station_->release_execution_unit(ExecutionUnitType::ALU, i);
            }
        }
    }
    
    // 更新分支单元
    for (size_t i = 0; i < branch_units_.size(); ++i) {
        auto& unit = branch_units_[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            print_stage_activity("EXECUTE", "BRANCH" + std::to_string(i) + " 执行中，剩余周期: " + 
                                std::to_string(unit.remaining_cycles));
            
            if (unit.remaining_cycles <= 0) {
                // 分支指令执行完成，需要发送完成信号到CDB
                CommonDataBusEntry cdb_entry;
                cdb_entry.dest_reg = unit.instruction.dest_reg;  // 对于分支指令通常为0
                cdb_entry.value = unit.result;  // 对于分支指令通常为0
                cdb_entry.rob_entry = unit.instruction.rob_entry;
                cdb_entry.valid = true;
                cdb_entry.is_jump = unit.is_jump;
                cdb_entry.jump_target = unit.jump_target;
                cdb_queue_.push(cdb_entry);
                
                if (unit.instruction.instruction.type == InstructionType::J_TYPE) {
                    print_stage_activity("EXECUTE", "BRANCH" + std::to_string(i) + " 跳转指令执行完成，" +
                                        "Inst#" + std::to_string(unit.instruction.instruction_id) + 
                                        " 结果发送到CDB");
                } else if (unit.instruction.instruction.type == InstructionType::B_TYPE) {
                    print_stage_activity("EXECUTE", "BRANCH" + std::to_string(i) + " 分支指令执行完成，" +
                                        "Inst#" + std::to_string(unit.instruction.instruction_id) + 
                                        " 完成信号发送到CDB");
                } else {
                    print_stage_activity("EXECUTE", "BRANCH" + std::to_string(i) + " 指令执行完成，" +
                                        "Inst#" + std::to_string(unit.instruction.instruction_id) + 
                                        " 结果发送到CDB");
                }
                
                // 释放执行单元
                unit.busy = false;
                unit.has_exception = false;
                unit.exception_msg.clear();
                unit.is_jump = false;
                unit.jump_target = 0;
                
                // 释放保留站中的执行单元状态
                reservation_station_->release_execution_unit(ExecutionUnitType::BRANCH, i);
            }
        }
    }
    
    // 更新加载单元
    for (size_t i = 0; i < load_units_.size(); ++i) {
        auto& unit = load_units_[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            print_stage_activity("EXECUTE", "LOAD" + std::to_string(i) + " 执行中，剩余周期: " + 
                                std::to_string(unit.remaining_cycles));
            
            if (unit.remaining_cycles <= 0) {
                // 加载指令完成，发送到CDB
                CommonDataBusEntry cdb_entry;
                cdb_entry.dest_reg = unit.instruction.dest_reg;
                cdb_entry.value = unit.result;
                cdb_entry.rob_entry = unit.instruction.rob_entry;
                cdb_entry.valid = true;
                
                cdb_queue_.push(cdb_entry);
                
                print_stage_activity("EXECUTE", "LOAD" + std::to_string(i) + " 执行完成，" +
                                    "Inst#" + std::to_string(unit.instruction.instruction_id) + 
                                    " 结果发送到CDB");
                
                // 释放执行单元
                unit.busy = false;
                unit.has_exception = false;
                unit.exception_msg.clear();
                unit.is_jump = false;
                unit.jump_target = 0;
                
                // 释放保留站中的执行单元状态
                reservation_station_->release_execution_unit(ExecutionUnitType::LOAD, i);
            }
        }
    }
    
    // 更新存储单元
    for (size_t i = 0; i < store_units_.size(); ++i) {
        auto& unit = store_units_[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            print_stage_activity("EXECUTE", "STORE" + std::to_string(i) + " 执行中，剩余周期: " + 
                                std::to_string(unit.remaining_cycles));
            
            if (unit.remaining_cycles <= 0) {
                // 存储指令完成，不需要写回结果，但需要更新ROB状态
                CommonDataBusEntry cdb_entry;
                cdb_entry.dest_reg = 0; // 存储指令没有目标寄存器
                cdb_entry.value = 0;
                cdb_entry.rob_entry = unit.instruction.rob_entry;
                cdb_entry.valid = true;
                
                cdb_queue_.push(cdb_entry);
                
                print_stage_activity("EXECUTE", "STORE" + std::to_string(i) + " 执行完成，" +
                                    "Inst#" + std::to_string(unit.instruction.instruction_id) + 
                                    " 通知ROB完成");
                
                // 释放执行单元
                unit.busy = false;
                unit.has_exception = false;
                unit.exception_msg.clear();
                unit.is_jump = false;
                unit.jump_target = 0;
                
                // 释放保留站中的执行单元状态
                reservation_station_->release_execution_unit(ExecutionUnitType::STORE, i);
            }
        }
    }
}

OutOfOrderCPU::ExecutionUnit* OutOfOrderCPU::get_available_unit(ExecutionUnitType type) {
    switch (type) {
        case ExecutionUnitType::ALU:
            for (auto& unit : alu_units_) {
                if (!unit.busy) return &unit;
            }
            break;
        case ExecutionUnitType::BRANCH:
            for (auto& unit : branch_units_) {
                if (!unit.busy) return &unit;
            }
            break;
        case ExecutionUnitType::LOAD:
            for (auto& unit : load_units_) {
                if (!unit.busy) return &unit;
            }
            break;
        case ExecutionUnitType::STORE:
            for (auto& unit : store_units_) {
                if (!unit.busy) return &unit;
            }
            break;
    }
    return nullptr;
}

uint32_t OutOfOrderCPU::get_physical_register_value(PhysRegNum reg) const {
    if (reg < RegisterRenameUnit::NUM_PHYSICAL_REGS) {
        return physical_registers_[reg];
    }
    return 0;
}

void OutOfOrderCPU::set_physical_register_value(PhysRegNum reg, uint32_t value) {
    if (reg < RegisterRenameUnit::NUM_PHYSICAL_REGS) {
        physical_registers_[reg] = value;
    }
}

// 接口兼容性方法
uint32_t OutOfOrderCPU::getRegister(RegNum reg) const {
    if (reg >= NUM_REGISTERS) {
        throw SimulatorException("无效的寄存器编号: " + std::to_string(reg));
    }
    return arch_registers_[reg];
}

void OutOfOrderCPU::setRegister(RegNum reg, uint32_t value) {
    if (reg >= NUM_REGISTERS) {
        throw SimulatorException("无效的寄存器编号: " + std::to_string(reg));
    }
    
    // x0寄存器始终为0
    if (reg != 0) {
        arch_registers_[reg] = value;
    }
}

uint32_t OutOfOrderCPU::getFPRegister(RegNum reg) const {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    return arch_fp_registers_[reg];
}

void OutOfOrderCPU::setFPRegister(RegNum reg, uint32_t value) {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    arch_fp_registers_[reg] = value;
}

float OutOfOrderCPU::getFPRegisterFloat(RegNum reg) const {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    return *reinterpret_cast<const float*>(&arch_fp_registers_[reg]);
}

void OutOfOrderCPU::setFPRegisterFloat(RegNum reg, float value) {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    arch_fp_registers_[reg] = *reinterpret_cast<const uint32_t*>(&value);
}

void OutOfOrderCPU::handle_exception(const std::string& exception_msg, uint32_t pc) {
    std::cerr << "异常: " << exception_msg << ", PC=0x" << std::hex << pc << std::dec << std::endl;
    flush_pipeline();
    halted_ = true;
}

void OutOfOrderCPU::flush_pipeline() {
    // 清空取指缓冲区
    while (!fetch_buffer_.empty()) {
        fetch_buffer_.pop();
    }
    
    // 刷新保留站
    reservation_station_->flush_pipeline();
    
    // 刷新ROB
    reorder_buffer_->flush_pipeline();
    
    // 重新初始化寄存器重命名
    register_rename_ = std::make_unique<RegisterRenameUnit>();
    
    // 清空CDB队列
    while (!cdb_queue_.empty()) {
        cdb_queue_.pop();
    }
    
    // 重置执行单元
    initialize_execution_units();
}

bool OutOfOrderCPU::predict_branch(uint32_t pc) {
    // 简化的分支预测：总是预测不跳转
    return false;
}

void OutOfOrderCPU::update_branch_predictor(uint32_t pc, bool taken) {
    // 简化实现：不更新预测器
}

void OutOfOrderCPU::handleEcall() {
    // 处理系统调用
    bool shouldHalt = syscall_handler_->handleSyscall(this);
    if (shouldHalt) {
        halted_ = true;
    }
}

void OutOfOrderCPU::handleEbreak() {
    std::cout << "遇到断点指令，停止执行" << std::endl;
    halted_ = true;
}

uint32_t OutOfOrderCPU::loadFromMemory(Address addr, Funct3 funct3) {
    return InstructionExecutor::loadFromMemory(memory_, addr, funct3);
}

void OutOfOrderCPU::storeToMemory(Address addr, uint32_t value, Funct3 funct3) {
    InstructionExecutor::storeToMemory(memory_, addr, value, funct3);
}

int32_t OutOfOrderCPU::signExtend(uint32_t value, int bits) const {
    return InstructionExecutor::signExtend(value, bits);
}

void OutOfOrderCPU::getPerformanceStats(uint64_t& instructions, uint64_t& cycles, 
                                       uint64_t& branch_mispredicts, uint64_t& stalls) const {
    instructions = instruction_count_;
    cycles = cycle_count_;
    branch_mispredicts = branch_mispredicts_;
    stalls = pipeline_stalls_;
}

void OutOfOrderCPU::dumpRegisters() const {
    std::cout << "架构寄存器状态:" << std::endl;
    for (int i = 0; i < NUM_REGISTERS; i += 4) {
        for (int j = 0; j < 4 && i + j < NUM_REGISTERS; ++j) {
            std::cout << "x" << std::setw(2) << (i + j) << ": 0x" 
                      << std::hex << std::setfill('0') << std::setw(8) 
                      << arch_registers_[i + j] << "  ";
        }
        std::cout << std::endl;
    }
    std::cout << std::dec;
}

void OutOfOrderCPU::dumpState() const {
    std::cout << "乱序执行CPU状态:" << std::endl;
    std::cout << "PC: 0x" << std::hex << pc_ << std::dec << std::endl;
    std::cout << "指令计数: " << instruction_count_ << std::endl;
    std::cout << "周期计数: " << cycle_count_ << std::endl;
    std::cout << "停机状态: " << (halted_ ? "是" : "否") << std::endl;
    std::cout << "分支预测错误: " << branch_mispredicts_ << std::endl;
    std::cout << "流水线停顿: " << pipeline_stalls_ << std::endl;
    
    if (cycle_count_ > 0) {
        double ipc = static_cast<double>(instruction_count_) / cycle_count_;
        std::cout << "IPC: " << std::fixed << std::setprecision(2) << ipc << std::endl;
    }
    
    dumpRegisters();
}

void OutOfOrderCPU::dumpPipelineState() const {
    std::cout << "\\n=== 乱序执行流水线状态 ===" << std::endl;
    
    // 显示ROB状态
    reorder_buffer_->dump_reorder_buffer();
    
    // 显示保留站状态
    reservation_station_->dump_reservation_station();
    
    // 显示寄存器重命名状态
    register_rename_->dump_rename_table();
    
    // 显示执行单元状态
    reservation_station_->dump_execution_units();
    
    std::cout << "取指缓冲区大小: " << fetch_buffer_.size() << std::endl;
    std::cout << "CDB队列大小: " << cdb_queue_.size() << std::endl;
}

// 调试辅助方法实现
void OutOfOrderCPU::print_cycle_header() {
    std::cout << std::endl;
    std::cout << "=== 周期 " << cycle_count_ << " ===" << std::endl;
    std::cout << "PC: 0x" << std::hex << pc_ << std::dec 
              << ", 已提交指令: " << instruction_count_ << std::endl;
    
    // 打印ROB状态
    if (!reorder_buffer_->is_empty()) {
        std::cout << "ROB状态: 非空" << std::endl;
    }
    
    // 打印保留站状态
    if (!reservation_station_->has_free_entry()) {
        std::cout << "保留站状态: 已满" << std::endl;
    }
    
    // 打印执行单元状态
    bool has_busy_units = false;
    for (size_t i = 0; i < alu_units_.size(); ++i) {
        if (alu_units_[i].busy) {
            std::cout << "ALU" << i << " 忙碌, 剩余周期: " << alu_units_[i].remaining_cycles << std::endl;
            has_busy_units = true;
        }
    }
    
    if (has_busy_units) {
        std::cout << "有执行单元忙碌" << std::endl;
    }
}

void OutOfOrderCPU::print_stage_activity(const std::string& stage, const std::string& activity) {
    auto& debugManager = DebugManager::getInstance();
    debugManager.printf(stage, activity, cycle_count_, pc_);
}

std::string OutOfOrderCPU::get_instruction_debug_info(uint64_t inst_id, uint32_t pc, const std::string& mnemonic) {
    return "Inst#" + std::to_string(inst_id) + " (PC=0x" + 
           std::to_string(pc) + ", " + mnemonic + ")";
}

} // namespace riscv