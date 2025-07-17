#include "cpu/ooo/stages/execute_stage.h"
#include "common/debug_types.h"
#include "core/instruction_executor.h"
#include <iostream>
#include <sstream>

namespace riscv {

ExecuteStage::ExecuteStage() {
    // 构造函数：初始化执行阶段
}

void ExecuteStage::execute(CPUState& state) {
    print_stage_activity("开始执行阶段", state.cycle_count, state.pc);
    
    // 首先更新正在执行的指令的状态
    update_execution_units(state);
    
    // 尝试从保留站调度指令到执行单元
    auto dispatch_result = state.reservation_station->dispatch_instruction();
    if (dispatch_result.success) {
        print_stage_activity("从保留站调度指令 RS[" + std::to_string(dispatch_result.rs_entry) + 
                            "] Inst#" + std::to_string(dispatch_result.instruction.instruction_id) + 
                            " 到执行单元", state.cycle_count, state.pc);
        
        ExecutionUnit* unit = get_available_unit(dispatch_result.unit_type, state);
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
            
            print_stage_activity("指令开始在 " + unit_type_str + " 单元执行，需要 " + 
                                std::to_string(cycle_count) + " 个周期", state.cycle_count, state.pc);
            
            // 开始执行指令
            execute_instruction(*unit, dispatch_result.instruction, state);
        } else {
            print_stage_activity("没有可用的执行单元", state.cycle_count, state.pc);
        }
    } else {
        print_stage_activity("保留站没有准备好的指令可调度", state.cycle_count, state.pc);
    }
}

void ExecuteStage::execute_instruction(ExecutionUnit& unit, const ReservationStationEntry& entry, CPUState& state) {
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
                    unit.result = InstructionExecutor::loadFromMemory(state.memory, addr, inst.funct3);
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
                    
                    // 简单的分支预测：静态预测不跳转
                    bool predicted_taken = false;  // 总是预测不跳转
                    
                    if (should_branch) {
                        // 分支taken：条件成立，需要跳转
                        unit.jump_target = entry.pc + inst.imm;
                        unit.is_jump = true;  // 标记需要改变PC
                        
                        if (!predicted_taken) {
                            // 预测不跳转，但实际跳转 -> 预测错误
                            std::stringstream ss;
                            ss << "分支指令 taken，目标地址: 0x" << std::hex << unit.jump_target 
                               << " (PC=0x" << std::hex << entry.pc << " + IMM=" << std::dec << inst.imm << ") (将在提交阶段刷新)";
                            print_stage_activity(ss.str(), state.cycle_count, state.pc);
                            // 注意：不在执行阶段刷新，让指令正常完成并提交
                            state.branch_mispredicts++;
                        } else {
                            // 预测跳转，实际跳转 -> 预测正确
                            std::stringstream ss;
                            ss << "分支指令 taken，目标地址: 0x" << std::hex << unit.jump_target 
                               << " (分支预测正确)";
                            print_stage_activity(ss.str(), state.cycle_count, state.pc);
                        }
                    } else {
                        // 分支not taken：条件不成立，继续顺序执行
                        unit.is_jump = false;  // 不需要改变PC
                        unit.jump_target = 0;
                        
                        if (predicted_taken) {
                            // 预测跳转，但实际不跳转 -> 预测错误
                            print_stage_activity("分支指令 not taken (将在提交阶段刷新)", state.cycle_count, state.pc);
                            // 注意：不在执行阶段刷新，让指令正常完成并提交
                            state.branch_mispredicts++;
                        } else {
                            // 预测不跳转，实际不跳转 -> 预测正确
                            print_stage_activity("分支指令 not taken (分支预测正确)", state.cycle_count, state.pc);
                        }
                    }
                }
                break;
                
            case InstructionType::S_TYPE:
                // 存储指令
                {
                    uint32_t addr = entry.src1_value + inst.imm;
                    InstructionExecutor::storeToMemory(state.memory, addr, entry.src2_value, inst.funct3);
                }
                break;
                
            case InstructionType::U_TYPE:
                // 上位立即数指令
                unit.result = InstructionExecutor::executeUpperImmediate(inst, entry.pc);
                break;
                
            case InstructionType::J_TYPE:
                {
                    // 跳转指令（JAL, JALR）- 无条件跳转
                    unit.result = entry.pc + (inst.is_compressed ? 2 : 4);  // 返回地址
                    unit.jump_target = InstructionExecutor::calculateJumpTarget(inst, entry.pc);
                    unit.is_jump = true;  // 无条件跳转总是需要改变PC
                    
                    // 无条件跳转指令：记录预测错误但不在执行阶段刷新
                    std::stringstream ss;
                    ss << "无条件跳转指令，目标地址: 0x" << std::hex << unit.jump_target 
                       << " (PC=0x" << std::hex << entry.pc << ") (将在提交阶段刷新流水线)";
                    print_stage_activity(ss.str(), state.cycle_count, state.pc);
                    
                    // 注意：不在执行阶段刷新，让指令正常完成并提交
                    // 流水线刷新将在提交阶段进行
                    state.branch_mispredicts++;  // 统计预测错误
                }
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

void ExecuteStage::update_execution_units(CPUState& state) {
    // 更新ALU单元
    for (size_t i = 0; i < state.alu_units.size(); ++i) {
        auto& unit = state.alu_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            print_stage_activity("ALU" + std::to_string(i) + " 执行中，剩余周期: " + 
                                std::to_string(unit.remaining_cycles), state.cycle_count, state.pc);
            
            if (unit.remaining_cycles <= 0) {
                // 执行完成，发送到CDB
                CommonDataBusEntry cdb_entry;
                cdb_entry.dest_reg = unit.instruction.dest_reg;
                cdb_entry.value = unit.result;
                cdb_entry.rob_entry = unit.instruction.rob_entry;
                cdb_entry.valid = true;
                cdb_entry.is_jump = unit.is_jump;
                cdb_entry.jump_target = unit.jump_target;
                
                state.cdb_queue.push(cdb_entry);
                
                print_stage_activity("ALU" + std::to_string(i) + " 执行完成，" +
                                    "Inst#" + std::to_string(unit.instruction.instruction_id) + 
                                    " 结果 0x" + std::to_string(unit.result) + " 发送到CDB", 
                                    state.cycle_count, state.pc);
                
                // 释放执行单元
                unit.busy = false;
                unit.has_exception = false;
                unit.exception_msg.clear();
                unit.is_jump = false;
                unit.jump_target = 0;
                
                // 释放保留站中的执行单元状态
                state.reservation_station->release_execution_unit(ExecutionUnitType::ALU, i);
            }
        }
    }
    
    // 更新分支单元
    for (size_t i = 0; i < state.branch_units.size(); ++i) {
        auto& unit = state.branch_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            print_stage_activity("BRANCH" + std::to_string(i) + " 执行中，剩余周期: " + 
                                std::to_string(unit.remaining_cycles), state.cycle_count, state.pc);
            
            if (unit.remaining_cycles <= 0) {
                // 分支指令执行完成，需要发送完成信号到CDB
                CommonDataBusEntry cdb_entry;
                cdb_entry.dest_reg = unit.instruction.dest_reg;  // 对于分支指令通常为0
                cdb_entry.value = unit.result;  // 对于分支指令通常为0
                cdb_entry.rob_entry = unit.instruction.rob_entry;
                cdb_entry.valid = true;
                cdb_entry.is_jump = unit.is_jump;
                cdb_entry.jump_target = unit.jump_target;
                state.cdb_queue.push(cdb_entry);
                
                if (unit.instruction.instruction.type == InstructionType::J_TYPE) {
                    print_stage_activity("BRANCH" + std::to_string(i) + " 跳转指令执行完成，" +
                                        "Inst#" + std::to_string(unit.instruction.instruction_id) + 
                                        " 结果发送到CDB", state.cycle_count, state.pc);
                } else if (unit.instruction.instruction.type == InstructionType::B_TYPE) {
                    print_stage_activity("BRANCH" + std::to_string(i) + " 分支指令执行完成，" +
                                        "Inst#" + std::to_string(unit.instruction.instruction_id) + 
                                        " 完成信号发送到CDB", state.cycle_count, state.pc);
                } else {
                    print_stage_activity("BRANCH" + std::to_string(i) + " 指令执行完成，" +
                                        "Inst#" + std::to_string(unit.instruction.instruction_id) + 
                                        " 结果发送到CDB", state.cycle_count, state.pc);
                }
                
                // 释放执行单元
                unit.busy = false;
                unit.has_exception = false;
                unit.exception_msg.clear();
                unit.is_jump = false;
                unit.jump_target = 0;
                
                // 释放保留站中的执行单元状态
                state.reservation_station->release_execution_unit(ExecutionUnitType::BRANCH, i);
            }
        }
    }
    
    // 更新加载单元
    for (size_t i = 0; i < state.load_units.size(); ++i) {
        auto& unit = state.load_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            print_stage_activity("LOAD" + std::to_string(i) + " 执行中，剩余周期: " + 
                                std::to_string(unit.remaining_cycles), state.cycle_count, state.pc);
            
            if (unit.remaining_cycles <= 0) {
                // 加载指令完成，发送到CDB
                CommonDataBusEntry cdb_entry;
                cdb_entry.dest_reg = unit.instruction.dest_reg;
                cdb_entry.value = unit.result;
                cdb_entry.rob_entry = unit.instruction.rob_entry;
                cdb_entry.valid = true;
                
                state.cdb_queue.push(cdb_entry);
                
                print_stage_activity("LOAD" + std::to_string(i) + " 执行完成，" +
                                    "Inst#" + std::to_string(unit.instruction.instruction_id) + 
                                    " 结果发送到CDB", state.cycle_count, state.pc);
                
                // 释放执行单元
                unit.busy = false;
                unit.has_exception = false;
                unit.exception_msg.clear();
                unit.is_jump = false;
                unit.jump_target = 0;
                
                // 释放保留站中的执行单元状态
                state.reservation_station->release_execution_unit(ExecutionUnitType::LOAD, i);
            }
        }
    }
    
    // 更新存储单元
    for (size_t i = 0; i < state.store_units.size(); ++i) {
        auto& unit = state.store_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            print_stage_activity("STORE" + std::to_string(i) + " 执行中，剩余周期: " + 
                                std::to_string(unit.remaining_cycles), state.cycle_count, state.pc);
            
            if (unit.remaining_cycles <= 0) {
                // 存储指令完成，不需要写回结果，但需要更新ROB状态
                CommonDataBusEntry cdb_entry;
                cdb_entry.dest_reg = 0; // 存储指令没有目标寄存器
                cdb_entry.value = 0;
                cdb_entry.rob_entry = unit.instruction.rob_entry;
                cdb_entry.valid = true;
                
                state.cdb_queue.push(cdb_entry);
                
                print_stage_activity("STORE" + std::to_string(i) + " 执行完成，" +
                                    "Inst#" + std::to_string(unit.instruction.instruction_id) + 
                                    " 通知ROB完成", state.cycle_count, state.pc);
                
                // 释放执行单元
                unit.busy = false;
                unit.has_exception = false;
                unit.exception_msg.clear();
                unit.is_jump = false;
                unit.jump_target = 0;
                
                // 释放保留站中的执行单元状态
                state.reservation_station->release_execution_unit(ExecutionUnitType::STORE, i);
            }
        }
    }
}

ExecutionUnit* ExecuteStage::get_available_unit(ExecutionUnitType type, CPUState& state) {
    switch (type) {
        case ExecutionUnitType::ALU:
            for (auto& unit : state.alu_units) {
                if (!unit.busy) return &unit;
            }
            break;
        case ExecutionUnitType::BRANCH:
            for (auto& unit : state.branch_units) {
                if (!unit.busy) return &unit;
            }
            break;
        case ExecutionUnitType::LOAD:
            for (auto& unit : state.load_units) {
                if (!unit.busy) return &unit;
            }
            break;
        case ExecutionUnitType::STORE:
            for (auto& unit : state.store_units) {
                if (!unit.busy) return &unit;
            }
            break;
    }
    return nullptr;
}

bool ExecuteStage::execute_branch_operation(const DecodedInstruction& inst, uint32_t src1, uint32_t src2, CPUState& state) {
    bool should_branch = InstructionExecutor::evaluateBranchCondition(inst, src1, src2);
    
    // 简化的分支预测检查
    bool predicted = predict_branch(state.pc);
    if (should_branch != predicted) {
        // 分支预测错误
        if (should_branch) {
            state.pc = state.pc + inst.imm;
        }
        update_branch_predictor(state.pc, should_branch);
        return true;  // 需要刷新流水线
    }
    
    return false;
}

void ExecuteStage::execute_store_operation(const DecodedInstruction& inst, uint32_t src1, uint32_t src2, CPUState& state) {
    uint32_t addr = src1 + inst.imm;
    InstructionExecutor::storeToMemory(state.memory, addr, src2, inst.funct3);
}

bool ExecuteStage::predict_branch(uint32_t pc) {
    // 简化的分支预测：总是预测不跳转
    return false;
}

void ExecuteStage::update_branch_predictor(uint32_t pc, bool taken) {
    // 简化实现：不更新预测器
}

void ExecuteStage::flush_pipeline(CPUState& state) {
    // 传统的全刷新方法（用于异常处理等场景）
    print_stage_activity("执行全流水线刷新", state.cycle_count, state.pc);
    
    // 清空取指缓冲区
    while (!state.fetch_buffer.empty()) {
        state.fetch_buffer.pop();
    }
    
    // 刷新保留站
    state.reservation_station->flush_pipeline();
    
    // 刷新ROB
    state.reorder_buffer->flush_pipeline();
    
    // 重新初始化寄存器重命名
    state.register_rename = std::make_unique<RegisterRenameUnit>();
    
    // 清空CDB队列
    while (!state.cdb_queue.empty()) {
        state.cdb_queue.pop();
    }
    
    // 重置执行单元
    reset_execution_units(state);
}

void ExecuteStage::reset_execution_units(CPUState& state) {
    // 重置所有执行单元
    for (auto& unit : state.alu_units) {
        unit.busy = false;
        unit.remaining_cycles = 0;
        unit.has_exception = false;
        unit.is_jump = false;
        unit.jump_target = 0;
    }
    
    for (auto& unit : state.branch_units) {
        unit.busy = false;
        unit.remaining_cycles = 0;
        unit.has_exception = false;
        unit.is_jump = false;
        unit.jump_target = 0;
    }
    
    for (auto& unit : state.load_units) {
        unit.busy = false;
        unit.remaining_cycles = 0;
        unit.has_exception = false;
        unit.is_jump = false;
        unit.jump_target = 0;
    }
    
    for (auto& unit : state.store_units) {
        unit.busy = false;
        unit.remaining_cycles = 0;
        unit.has_exception = false;
        unit.is_jump = false;
        unit.jump_target = 0;
    }
}

void ExecuteStage::flush() {
    print_stage_activity("执行阶段已刷新", 0, 0);
}

void ExecuteStage::reset() {
    print_stage_activity("执行阶段已重置", 0, 0);
}

void ExecuteStage::print_stage_activity(const std::string& activity, uint64_t cycle, uint32_t pc) {
    auto& debugManager = DebugManager::getInstance();
    debugManager.printf(get_stage_name(), activity, cycle, pc);
}

} // namespace riscv 