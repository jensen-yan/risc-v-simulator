#include "cpu/ooo/stages/execute_stage.h"
#include "cpu/ooo/dynamic_inst.h"
#include "common/debug_types.h"
#include "common/types.h"
#include "core/instruction_executor.h"

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
        LOGT(EXECUTE, "dispatch inst=%" PRId64 " from rs[%d] to execution unit",
            dispatch_result.instruction->get_instruction_id(), dispatch_result.rs_entry);
        
        ExecutionUnit* unit = get_available_unit(dispatch_result.unit_type, state);
        if (unit) {
            unit->busy = true;
            unit->instruction = dispatch_result.instruction;
            unit->has_exception = false;
            
            const char* unit_type_str = "";
            
            // 使用预解析的执行周期数，无需重复判断指令类型
            const auto& decoded_info = dispatch_result.instruction->get_decoded_info();
            unit->remaining_cycles = decoded_info.execution_cycles;
            
            // 根据执行单元类型设置调试信息
            switch (dispatch_result.unit_type) {
                case ExecutionUnitType::ALU:
                    unit_type_str = "ALU";
                    break;
                case ExecutionUnitType::BRANCH:
                    unit_type_str = "BRANCH";
                    break;
                case ExecutionUnitType::LOAD:
                    unit_type_str = "LOAD";
                    break;
                case ExecutionUnitType::STORE:
                    unit_type_str = "STORE";
                    break;
            }
            
            LOGT(EXECUTE, "inst=%" PRId64 " start on %s, cycles=%d",
                dispatch_result.instruction->get_instruction_id(), unit_type_str, unit->remaining_cycles);
            
            // 开始执行指令
            execute_instruction(*unit, dispatch_result.instruction, state);
        } else {
            LOGT(EXECUTE, "no available execution unit");
        }
    } else {
        LOGT(EXECUTE, "no ready instruction in reservation station");
    }
}

void ExecuteStage::execute_instruction(ExecutionUnit& unit, DynamicInstPtr instruction, CPUState& state) {
    try {
        const auto& inst = instruction->get_decoded_info();
        
        // 首先检查解码时发现的异常
        if (inst.has_decode_exception) {
            unit.has_exception = true;
            unit.exception_msg = inst.decode_exception_msg;
            LOGW(EXECUTE, "decode exception: %s", inst.decode_exception_msg.c_str());
            return;
        }
        
        switch (inst.type) {
            case InstructionType::R_TYPE:
                if (inst.opcode == Opcode::OP) {
                    // 寄存器-寄存器运算
                    unit.result = InstructionExecutor::executeRegisterOperation(inst, instruction->get_src1_value(), instruction->get_src2_value());
                } else if (inst.opcode == Opcode::OP_32) {
                    // RV64I: 32位寄存器运算（W后缀）
                    unit.result = InstructionExecutor::executeRegisterOperation32(inst, instruction->get_src1_value(), instruction->get_src2_value());
                } else {
                    // 其他R_TYPE指令（如M扩展、F扩展等）
                    unit.result = InstructionExecutor::executeRegisterOperation(inst, instruction->get_src1_value(), instruction->get_src2_value());
                }
                break;
                
            case InstructionType::I_TYPE:
                if (inst.opcode == Opcode::OP_IMM) {
                    // 立即数运算
                    unit.result = InstructionExecutor::executeImmediateOperation(inst, instruction->get_src1_value());
                } else if (inst.opcode == Opcode::OP_IMM_32) {
                    // RV64I: 32位立即数运算（W后缀）
                    unit.result = InstructionExecutor::executeImmediateOperation32(inst, instruction->get_src1_value());
                } else if (inst.opcode == Opcode::LOAD) {
                    // 加载指令 - 使用预解析的静态信息
                    uint64_t addr = instruction->get_src1_value() + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
                    
                    // 异常已在解码时检测，这里直接使用预解析的信息
                    unit.load_address = addr;
                    unit.load_size = inst.memory_access_size;
                    LOGT(EXECUTE, "start LOAD: addr=0x%" PRIx64 ", size=%d", addr, inst.memory_access_size);
                    
                } else if (inst.opcode == Opcode::JALR) {
                    // JALR 指令 - I-type 跳转指令
                    unit.result = instruction->get_pc() + (inst.is_compressed ? 2 : 4);
                    
                    // JALR 指令：跳转目标地址 = rs1 + imm，并清除最低位
                    unit.jump_target = InstructionExecutor::calculateJumpAndLinkTarget(inst, instruction->get_pc(), instruction->get_src1_value());
                    unit.is_jump = true;  // 标记为跳转指令
                    instruction->set_jump_info(true, unit.jump_target);
                } else {
                    unit.has_exception = true;
                    unit.exception_msg = "unsupported I-type instruction";
                }
                break;

            case InstructionType::SYSTEM_TYPE:
                // CSR指令或系统调用 - 暂时作为NOP处理
                LOGT(EXECUTE, "inst=%" PRId64 " execute SYSTEM_TYPE as NOP",
                    instruction->get_instruction_id());
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
                            LOGT(EXECUTE, "branch taken, target=0x%" PRIx64 " (pc=0x%" PRIx64 " + imm=%d), flush at commit",
                                   unit.jump_target, instruction->get_pc(), inst.imm);
                            // 注意：不在执行阶段刷新，让指令正常完成并提交
                            state.branch_mispredicts++;
                        } else {
                            // 预测跳转，实际跳转 -> 预测正确
                            LOGT(EXECUTE, "branch taken, target=0x%" PRIx64 " (prediction correct)", unit.jump_target);
                        }
                    } else {
                        // 分支not taken：条件不成立，继续顺序执行
                        unit.is_jump = false;  // 不需要改变PC
                        unit.jump_target = 0;
                        
                        if (predicted_taken) {
                            // 预测跳转，但实际不跳转 -> 预测错误
                            LOGT(EXECUTE, "branch not taken, flush at commit");
                            // 注意：不在执行阶段刷新，让指令正常完成并提交
                            state.branch_mispredicts++;
                        } else {
                            // 预测不跳转，实际不跳转 -> 预测正确
                            LOGT(EXECUTE, "branch not taken (prediction correct)");
                        }
                    }
                }
                break;
                
            case InstructionType::S_TYPE:
                // 存储指令 - 使用预解析的静态信息
                {
                    uint64_t addr = instruction->get_src1_value() + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
                    
                    // 异常已在解码时检测，这里直接使用预解析的信息
                    LOGT(EXECUTE, "execute STORE: addr=0x%" PRIx64 " value=0x%" PRIx64 " size=%d",
                            addr, instruction->get_src2_value(), inst.memory_access_size);
                    
                    // 执行Store到内存
                    InstructionExecutor::storeToMemory(state.memory, addr, instruction->get_src2_value(), inst.funct3);
                    
                    // 同时添加到Store Buffer用于Store-to-Load Forwarding
                    state.store_buffer->add_store(instruction, addr, instruction->get_src2_value(), inst.memory_access_size);
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
                    LOGT(EXECUTE, "unconditional jump, target=0x%" PRIx64 " (pc=0x%" PRIx64 "), flush at commit",
                           unit.jump_target, instruction->get_pc());
                    
                    // 注意：不在执行阶段刷新，让指令正常完成并提交
                    // 流水线刷新将在提交阶段进行
                    state.branch_mispredicts++;  // 统计预测错误
                }
                break;
                
            default:
                unit.has_exception = true;
                unit.exception_msg = "unsupported instruction type";
                LOGW(EXECUTE, "unsupported instruction type: %d", static_cast<int>(inst.type));
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
            LOGT(EXECUTE, "inst=%" PRId64 " ALU%zu running, remaining=%d",
                unit.instruction->get_instruction_id(), i, unit.remaining_cycles);
            
            if (unit.remaining_cycles <= 0) {
                LOGT(EXECUTE, "inst=%" PRId64 " ALU%zu done, result=0x%" PRIx64 " -> CDB",
                    unit.instruction->get_instruction_id(), i, unit.result);
                
                complete_execution_unit(unit, ExecutionUnitType::ALU, i, state);
            }
        }
    }
    
    // 更新分支单元
    for (size_t i = 0; i < state.branch_units.size(); ++i) {
        auto& unit = state.branch_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            LOGT(EXECUTE, "inst=%" PRId64 " BRANCH%zu running, remaining=%d",
                unit.instruction->get_instruction_id(), i, unit.remaining_cycles);
            
            if (unit.remaining_cycles <= 0) {
                LOGT(EXECUTE, "inst=%" PRId64 " BRANCH%zu done -> CDB",
                    unit.instruction->get_instruction_id(), i);
                complete_execution_unit(unit, ExecutionUnitType::BRANCH, i, state);
            }
        }
    }
    
    // 更新加载单元
    for (size_t i = 0; i < state.load_units.size(); ++i) {
        auto& unit = state.load_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu running, remaining=%d",
                unit.instruction->get_instruction_id(), i, unit.remaining_cycles);
            
            if (unit.remaining_cycles <= 0) {
                // 在Load指令完成前，再次检查Store依赖
                bool should_wait = state.reorder_buffer->has_earlier_store_pending(unit.instruction->get_instruction_id());
                
                if (should_wait) {
                    // 仍然有未完成的Store依赖，延迟完成
                    unit.remaining_cycles = 1; // 延迟一个周期
                    LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu waits on earlier STORE, delay completion",
                        unit.instruction->get_instruction_id(), i);
                    continue; // 跳过完成处理
                }
                
                // 没有Store依赖，可以完成
                // 尝试Store-to-Load Forwarding
                bool used_forwarding = perform_load_execution(unit, state);
                
                LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu done, %s result=0x%" PRIx64 " -> CDB",
                    unit.instruction->get_instruction_id(),
                    i, (used_forwarding ? "(store-forwarded)" : "(loaded-from-memory)"), unit.result);
                
                complete_execution_unit(unit, ExecutionUnitType::LOAD, i, state);
            }
        }
    }
    
    // 更新存储单元
    for (size_t i = 0; i < state.store_units.size(); ++i) {
        auto& unit = state.store_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            LOGT(EXECUTE, "inst=%" PRId64 " STORE%zu running, remaining=%d",
                unit.instruction->get_instruction_id(), i, unit.remaining_cycles);
            
            if (unit.remaining_cycles <= 0) {
                // 存储指令结果为0
                unit.result = 0;
                
                LOGT(EXECUTE, "inst=%" PRId64 " STORE%zu done, notify ROB",
                    unit.instruction->get_instruction_id(), i);
                
                complete_execution_unit(unit, ExecutionUnitType::STORE, i, state);
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

bool ExecuteStage::execute_branch_operation(const DecodedInstruction& inst, uint64_t src1, uint64_t src2, CPUState& state) {
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

void ExecuteStage::execute_store_operation(const DecodedInstruction& inst, uint64_t src1, uint64_t src2, CPUState& state) {
    uint64_t addr = src1 + inst.imm;
    InstructionExecutor::storeToMemory(state.memory, addr, src2, inst.funct3);
}

bool ExecuteStage::predict_branch(uint64_t pc) {
    // 简化的分支预测：总是预测不跳转
    return false;
}

void ExecuteStage::update_branch_predictor(uint64_t pc, bool taken) {
    // 简化实现：不更新预测器
}

void ExecuteStage::flush_pipeline(CPUState& state) {
    // 传统的全刷新方法（用于异常处理等场景）
    LOGT(EXECUTE, "flush entire pipeline");
    
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

void ExecuteStage::reset_single_unit(ExecutionUnit& unit) {
    // 重置单个执行单元的通用逻辑
    unit.busy = false;
    unit.remaining_cycles = 0;
    unit.has_exception = false;
    unit.is_jump = false;
    unit.jump_target = 0;
    unit.instruction = nullptr;
    unit.exception_msg.clear();
}

template<typename UnitContainer>
void ExecuteStage::reset_unit_container(UnitContainer& units) {
    // 重置执行单元容器的通用函数
    for (auto& unit : units) {
        reset_single_unit(unit);
    }
}

void ExecuteStage::complete_execution_unit(ExecutionUnit& unit, ExecutionUnitType unit_type, size_t unit_index, CPUState& state) {
    // 设置执行结果和跳转信息到DynamicInst
    unit.instruction->set_result(unit.result);
    unit.instruction->set_jump_info(unit.is_jump, unit.jump_target);
    
    // 执行完成，发送到CDB
    CommonDataBusEntry cdb_entry(unit.instruction);
    state.cdb_queue.push(cdb_entry);
    
    // 清空对应的保留站条目
    RSEntry rs_entry = unit.instruction->get_rs_entry();
    state.reservation_station->release_entry(rs_entry);
    
    // 释放执行单元
    reset_single_unit(unit);
    
    // 释放保留站中的执行单元状态
    state.reservation_station->release_execution_unit(unit_type, unit_index);
}

void ExecuteStage::reset_execution_units(CPUState& state) {
    // 重置所有执行单元
    reset_unit_container(state.alu_units);
    reset_unit_container(state.branch_units);
    reset_unit_container(state.load_units);
    reset_unit_container(state.store_units);
}

void ExecuteStage::flush() {
    LOGT(EXECUTE, "execute stage flushed");
}

void ExecuteStage::reset() {
    LOGT(EXECUTE, "execute stage reset");
}

bool ExecuteStage::perform_load_execution(ExecutionUnit& unit, CPUState& state) {
    const auto& inst = unit.instruction->get_decoded_info();
    uint64_t addr = unit.load_address;
    uint8_t access_size = unit.load_size;
    
    // 尝试Store-to-Load Forwarding
    uint64_t forwarded_value;
    bool forwarded = state.store_buffer->forward_load(addr, access_size, forwarded_value);
    
    if (forwarded) {
        // 从Store Buffer获得转发数据，根据预解析的符号扩展信息处理
        if (inst.is_signed_load) {
            // 符号扩展Load指令：LB, LH, LW
            switch (access_size) {
                case 1: // LB
                    unit.result = static_cast<uint64_t>(static_cast<int8_t>(forwarded_value & 0xFF));
                    break;
                case 2: // LH
                    unit.result = static_cast<uint64_t>(static_cast<int16_t>(forwarded_value & 0xFFFF));
                    break;
                case 4: // LW
                    unit.result = static_cast<uint64_t>(static_cast<int32_t>(forwarded_value & 0xFFFFFFFF));
                    break;
                case 8: // LD (64位)
                    unit.result = forwarded_value;
                    break;
                default:
                    unit.result = forwarded_value;
                    break;
            }
        } else {
            // 零扩展Load指令：LBU, LHU, LWU
            switch (access_size) {
                case 1: // LBU
                    unit.result = forwarded_value & 0xFF;
                    break;
                case 2: // LHU
                    unit.result = forwarded_value & 0xFFFF;
                    break;
                case 4: // LWU (RV64新增)
                    unit.result = forwarded_value & 0xFFFFFFFF;
                    break;
                case 8: // LD (64位)
                    unit.result = forwarded_value;
                    break;
                default:
                    unit.result = forwarded_value;
                    break;
            }
        }
        
        LOGT(EXECUTE, "store-to-load forwarding hit: addr=0x%" PRIx64 " value=0x%" PRIx64 " %s-extended",
                       addr, unit.result, inst.is_signed_load ? "sign" : "zero");
        return true; // 使用了转发
    } else {
        // 没有转发，从内存读取
        LOGT(EXECUTE, "store-to-load forwarding miss, read from memory");
        
        unit.result = InstructionExecutor::loadFromMemory(state.memory, addr, inst.funct3);
        
        LOGT(EXECUTE, "memory load done: addr=0x%" PRIx64 " result=0x%" PRIx64, addr, unit.result);
        return false; // 没有使用转发
    }
}

} // namespace riscv 
