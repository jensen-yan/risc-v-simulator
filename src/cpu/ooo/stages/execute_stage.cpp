#include "cpu/ooo/stages/execute_stage.h"
#include "cpu/ooo/dynamic_inst.h"
#include "common/debug_types.h"
#include "common/types.h"
#include "core/instruction_executor.h"
#include <iostream>
#include <sstream>

namespace riscv {

ExecuteStage::ExecuteStage() {
    // 构造函数：初始化执行阶段
}

void ExecuteStage::execute(CPUState& state) {
    // 首先更新正在执行的指令的状态
    update_execution_units(state);
    
    // 尝试从保留站调度指令到执行单元
    auto dispatch_result = state.reservation_station->dispatch_instruction();
    if (dispatch_result.success) {
        dprintf(EXECUTE, "从保留站调度指令 RS[%d] Inst#%lu 到执行单元", 
                            dispatch_result.rs_entry, dispatch_result.instruction->get_instruction_id());
        
        ExecutionUnit* unit = get_available_unit(dispatch_result.unit_type, state);
        if (unit) {
            unit->busy = true;
            unit->instruction = dispatch_result.instruction;
            unit->has_exception = false;
            
            const char* unit_type_str = "";
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
            
            dprintf(EXECUTE, "指令开始在 %s 单元执行，需要 %d 个周期", 
                                unit_type_str, cycle_count);
            
            // 开始执行指令
            execute_instruction(*unit, dispatch_result.instruction, state);
        } else {
            dprintf(EXECUTE, "没有可用的执行单元");
        }
    } else {
        dprintf(EXECUTE, "保留站没有准备好的指令可调度");
    }
}

void ExecuteStage::execute_instruction(ExecutionUnit& unit, DynamicInstPtr instruction, CPUState& state) {
    try {
        const auto& inst = instruction->get_decoded_info();
        
        switch (inst.type) {
            case InstructionType::R_TYPE:
                // 寄存器-寄存器运算
                unit.result = InstructionExecutor::executeRegisterOperation(inst, instruction->get_src1_value(), instruction->get_src2_value());
                break;
                
            case InstructionType::I_TYPE:
                if (inst.opcode == Opcode::OP_IMM) {
                    // 立即数运算
                    unit.result = InstructionExecutor::executeImmediateOperation(inst, instruction->get_src1_value());
                } else if (inst.opcode == Opcode::LOAD) {
                    // 加载指令
                    uint32_t addr = instruction->get_src1_value() + inst.imm;
                    
                    // 确定访问大小
                    uint8_t access_size = 1; // 默认字节访问
                    switch (inst.funct3) {
                        case static_cast<Funct3>(0): // LB
                        case static_cast<Funct3>(4): // LBU
                            access_size = 1;
                            break;
                        case static_cast<Funct3>(1): // LH
                        case static_cast<Funct3>(5): // LHU
                            access_size = 2;
                            break;
                        case static_cast<Funct3>(2): // LW
                            access_size = 4;
                            break;
                    }
                    
                    // 在execute_instruction中，Load指令只计算地址，实际的转发和内存访问在update_execution_units中进行
                    unit.load_address = addr;
                    unit.load_size = access_size;
                    dprintf(EXECUTE, "Load指令开始执行，地址=0x%x", addr);
                    
                } else if (inst.opcode == Opcode::JALR) {
                    // JALR 指令 - I-type 跳转指令
                    unit.result = instruction->get_pc() + (inst.is_compressed ? 2 : 4);
                    
                    // JALR 指令：跳转目标地址 = rs1 + imm，并清除最低位
                    unit.jump_target = InstructionExecutor::calculateJumpAndLinkTarget(inst, instruction->get_pc(), instruction->get_src1_value());
                    unit.is_jump = true;  // 标记为跳转指令
                    instruction->set_jump_info(true, unit.jump_target);
                } else {
                    unit.has_exception = true;
                    unit.exception_msg = "不支持的I-type指令";
                }
                break;

            case InstructionType::SYSTEM_TYPE:
                // CSR指令或系统调用 - 暂时作为NOP处理
                dprintf(EXECUTE, "执行SYSTEM_TYPE指令(NOP): Inst#%lu PC=0x%x", 
                       instruction->get_instruction_id(), instruction->get_pc());
                unit.result = 0;
                break;
                
            case InstructionType::B_TYPE:
                // 分支指令（BNE, BEQ, BLT等）
                {
                    bool should_branch = InstructionExecutor::evaluateBranchCondition(inst, instruction->get_src1_value(), instruction->get_src2_value());
                    
                    // 设置分支结果（分支指令通常不写回寄存器，但需要完成执行）
                    unit.result = 0;  // 分支指令没有写回值
                    
                    // 简单的分支预测：静态预测不跳转
                    bool predicted_taken = false;  // 总是预测不跳转
                    
                    if (should_branch) {
                        // 分支taken：条件成立，需要跳转
                        unit.jump_target = instruction->get_pc() + inst.imm;
                        unit.is_jump = true;  // 标记需要改变PC
                        instruction->set_jump_info(true, unit.jump_target);
                        
                        if (!predicted_taken) {
                            // 预测不跳转，但实际跳转 -> 预测错误
                            dprintf(EXECUTE, "分支指令 taken，目标地址: 0x%x (PC=0x%x + IMM=%d) (将在提交阶段刷新)",
                                   unit.jump_target, instruction->get_pc(), inst.imm);
                            // 注意：不在执行阶段刷新，让指令正常完成并提交
                            state.branch_mispredicts++;
                        } else {
                            // 预测跳转，实际跳转 -> 预测正确
                            dprintf(EXECUTE, "分支指令 taken，目标地址: 0x%x (分支预测正确)", unit.jump_target);
                        }
                    } else {
                        // 分支not taken：条件不成立，继续顺序执行
                        unit.is_jump = false;  // 不需要改变PC
                        unit.jump_target = 0;
                        
                        if (predicted_taken) {
                            // 预测跳转，但实际不跳转 -> 预测错误
                            dprintf(EXECUTE, "分支指令 not taken (将在提交阶段刷新)");
                            // 注意：不在执行阶段刷新，让指令正常完成并提交
                            state.branch_mispredicts++;
                        } else {
                            // 预测不跳转，实际不跳转 -> 预测正确
                            dprintf(EXECUTE, "分支指令 not taken (分支预测正确)");
                        }
                    }
                }
                break;
                
            case InstructionType::S_TYPE:
                // 存储指令
                {
                    uint32_t addr = instruction->get_src1_value() + inst.imm;
                    
                    // 确定访问大小
                    uint8_t access_size = 1; // 默认字节访问
                    switch (inst.funct3) {
                        case static_cast<Funct3>(0): // SB
                            access_size = 1;
                            break;
                        case static_cast<Funct3>(1): // SH
                            access_size = 2;
                            break;
                        case static_cast<Funct3>(2): // SW
                            access_size = 4;
                            break;
                    }
                    
                    dprintf(EXECUTE, "Store指令执行：地址=0x%x 值=0x%x", addr, instruction->get_src2_value());
                    
                    // 执行Store到内存
                    InstructionExecutor::storeToMemory(state.memory, addr, instruction->get_src2_value(), inst.funct3);
                    
                    // 同时添加到Store Buffer用于Store-to-Load Forwarding
                    state.store_buffer->add_store(instruction, addr, instruction->get_src2_value(), access_size);
                }
                break;
                
            case InstructionType::U_TYPE:
                // 上位立即数指令
                unit.result = InstructionExecutor::executeUpperImmediate(inst, instruction->get_pc());
                break;
                
            case InstructionType::J_TYPE:
                {
                    // JAL 指令 - J-type 无条件跳转
                    unit.result = instruction->get_pc() + (inst.is_compressed ? 2 : 4);
                    unit.jump_target = InstructionExecutor::calculateJumpTarget(inst, instruction->get_pc());
                    unit.is_jump = true;  // 无条件跳转总是需要改变PC
                    instruction->set_jump_info(true, unit.jump_target);
                    
                    // 无条件跳转指令：记录预测错误但不在执行阶段刷新
                    dprintf(EXECUTE, "无条件跳转指令，目标地址: 0x%x (PC=0x%x) (将在提交阶段刷新流水线)",
                           unit.jump_target, instruction->get_pc());
                    
                    // 注意：不在执行阶段刷新，让指令正常完成并提交
                    // 流水线刷新将在提交阶段进行
                    state.branch_mispredicts++;  // 统计预测错误
                }
                break;
                
            default:
                unit.has_exception = true;
                unit.exception_msg = "不支持的指令类型";
                dprintf(EXECUTE, "不支持的指令类型: %d", static_cast<int>(inst.type));
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
            dprintf(EXECUTE, "ALU%zu 执行中，剩余周期: %d", i, unit.remaining_cycles);
            
            if (unit.remaining_cycles <= 0) {
                // 设置执行结果和跳转信息到DynamicInst
                unit.instruction->set_result(unit.result);
                unit.instruction->set_jump_info(unit.is_jump, unit.jump_target);
                
                // 执行完成，发送到CDB
                CommonDataBusEntry cdb_entry(unit.instruction);
                
                state.cdb_queue.push(cdb_entry);
                
                dprintf(EXECUTE, "ALU%zu 执行完成，Inst#%lu 结果 0x%x 发送到CDB", 
                                    i, unit.instruction->get_instruction_id(), unit.result);
                
                // 清空对应的保留站条目
                RSEntry rs_entry = unit.instruction->get_rs_entry();
                state.reservation_station->release_entry(rs_entry);
                
                // 释放执行单元
                unit.busy = false;
                unit.instruction = nullptr;
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
            dprintf(EXECUTE, "BRANCH%zu 执行中，剩余周期: %d", i, unit.remaining_cycles);
            
            if (unit.remaining_cycles <= 0) {
                // 设置执行结果和跳转信息到DynamicInst
                unit.instruction->set_result(unit.result);
                unit.instruction->set_jump_info(unit.is_jump, unit.jump_target);
                
                // 分支指令执行完成，需要发送完成信号到CDB
                CommonDataBusEntry cdb_entry(unit.instruction);
                state.cdb_queue.push(cdb_entry);
                
                const auto& inst_type = unit.instruction->get_decoded_info().type;
                if (inst_type == InstructionType::J_TYPE) {
                    dprintf(EXECUTE, "BRANCH%zu 跳转指令执行完成，Inst#%lu 结果发送到CDB", 
                                        i, unit.instruction->get_instruction_id());
                } else if (inst_type == InstructionType::B_TYPE) {
                    dprintf(EXECUTE, "BRANCH%zu 分支指令执行完成，Inst#%lu 完成信号发送到CDB", 
                                        i, unit.instruction->get_instruction_id());
                } else {
                    dprintf(EXECUTE, "BRANCH%zu 指令执行完成，Inst#%lu 结果发送到CDB", 
                                        i, unit.instruction->get_instruction_id());
                }
                
                // 清空对应的保留站条目
                RSEntry rs_entry = unit.instruction->get_rs_entry();
                state.reservation_station->release_entry(rs_entry);
                
                // 释放执行单元
                unit.busy = false;
                unit.instruction = nullptr;
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
            dprintf(EXECUTE, "LOAD%zu 执行中，剩余周期: %d", i, unit.remaining_cycles);
            
            if (unit.remaining_cycles <= 0) {
                // 在Load指令完成前，再次检查Store依赖
                bool should_wait = state.reorder_buffer->has_earlier_store_pending(unit.instruction->get_instruction_id());
                
                if (should_wait) {
                    // 仍然有未完成的Store依赖，延迟完成
                    unit.remaining_cycles = 1; // 延迟一个周期
                    dprintf(EXECUTE, "LOAD%zu 检测到Store依赖，延迟完成", i);
                    continue; // 跳过完成处理
                }
                
                // 没有Store依赖，可以完成
                // 尝试Store-to-Load Forwarding
                bool used_forwarding = perform_load_execution(unit, state);
                
                // 设置执行结果到DynamicInst
                unit.instruction->set_result(unit.result);
                
                // 加载指令完成，发送到CDB
                CommonDataBusEntry cdb_entry(unit.instruction);
                
                state.cdb_queue.push(cdb_entry);
                
                dprintf(EXECUTE, "LOAD%zu 执行完成，Inst#%lu %s 结果=0x%x 发送到CDB",
                                           i, unit.instruction->get_instruction_id(),
                                           (used_forwarding ? "(使用Store转发)" : "(从内存读取)"),
                                           unit.result);
                
                // 清空对应的保留站条目
                RSEntry rs_entry = unit.instruction->get_rs_entry();
                state.reservation_station->release_entry(rs_entry);
                
                // 释放执行单元
                unit.busy = false;
                unit.instruction = nullptr;
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
            dprintf(EXECUTE, "STORE%zu 执行中，剩余周期: %d", i, unit.remaining_cycles);
            
            if (unit.remaining_cycles <= 0) {
                // 设置执行结果到DynamicInst（存储指令结果为0）
                unit.instruction->set_result(0);
                
                // 存储指令完成，不需要写回结果，但需要更新ROB状态
                CommonDataBusEntry cdb_entry(unit.instruction);
                
                state.cdb_queue.push(cdb_entry);
                
                dprintf(EXECUTE, "STORE%zu 执行完成，Inst#%lu 通知ROB完成", 
                                    i, unit.instruction->get_instruction_id());
                
                // 清空对应的保留站条目
                RSEntry rs_entry = unit.instruction->get_rs_entry();
                state.reservation_station->release_entry(rs_entry);
                
                // 释放执行单元
                unit.busy = false;
                unit.instruction = nullptr;
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
    dprintf(EXECUTE, "执行全流水线刷新");
    
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
    dprintf(EXECUTE, "执行阶段已刷新");
}

void ExecuteStage::reset() {
    dprintf(EXECUTE, "执行阶段已重置");
}

bool ExecuteStage::perform_load_execution(ExecutionUnit& unit, CPUState& state) {
    const auto& inst = unit.instruction->get_decoded_info();
    uint32_t addr = unit.load_address;
    uint8_t access_size = unit.load_size;
    
    // 尝试Store-to-Load Forwarding
    uint32_t forwarded_value;
    bool forwarded = state.store_buffer->forward_load(addr, access_size, forwarded_value);
    
    if (forwarded) {
        // 从Store Buffer获得转发数据，还需要进行符号扩展处理
        switch (inst.funct3) {
            case static_cast<Funct3>(0): // LB - 符号扩展字节
                unit.result = static_cast<uint32_t>(static_cast<int8_t>(forwarded_value & 0xFF));
                break;
            case static_cast<Funct3>(1): // LH - 符号扩展半字
                unit.result = static_cast<uint32_t>(static_cast<int16_t>(forwarded_value & 0xFFFF));
                break;
            case static_cast<Funct3>(2): // LW - 字
                unit.result = forwarded_value;
                break;
            case static_cast<Funct3>(4): // LBU - 零扩展字节
                unit.result = forwarded_value & 0xFF;
                break;
            case static_cast<Funct3>(5): // LHU - 零扩展半字
                unit.result = forwarded_value & 0xFFFF;
                break;
            default:
                unit.result = forwarded_value;
                break;
        }
        
        dprintf(EXECUTE, "Store-to-Load Forwarding成功: 地址=0x%x 转发值=0x%x", 
                       addr, unit.result);
        return true; // 使用了转发
    } else {
        // 没有转发，从内存读取
        dprintf(EXECUTE, "没有Store转发，从内存读取");
        
        unit.result = InstructionExecutor::loadFromMemory(state.memory, addr, inst.funct3);
        
        dprintf(EXECUTE, "内存Load完成：地址=0x%x 结果=0x%x", addr, unit.result);
        return false; // 没有使用转发
    }
}

} // namespace riscv 