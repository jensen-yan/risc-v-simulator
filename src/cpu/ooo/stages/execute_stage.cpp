#include "cpu/ooo/stages/execute_stage.h"
#include "cpu/ooo/execute_semantics.h"
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
            
            // 开始执行指令语义
            OOOExecuteSemantics::executeInstruction(*unit, dispatch_result.instruction, state);
        } else {
            LOGT(EXECUTE, "no available execution unit");
        }
    } else {
        LOGT(EXECUTE, "no ready instruction in reservation station");
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
                const auto& inst = unit.instruction->get_decoded_info();
                if (inst.opcode == Opcode::AMO &&
                    state.reorder_buffer->has_earlier_store_pending(unit.instruction->get_instruction_id())) {
                    // AMO需在更老Store完成后再结束，避免破坏单线程程序顺序可见性。
                    unit.remaining_cycles = 1;
                    LOGT(EXECUTE, "inst=%" PRId64 " AMO waits on earlier STORE, delay completion",
                        unit.instruction->get_instruction_id());
                    continue;
                }

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
                // 尝试Store-to-Load Forwarding/内存读取
                LoadExecutionResult load_result = perform_load_execution(unit, state);
                if (load_result == LoadExecutionResult::BlockedByStore) {
                    unit.remaining_cycles = 1; // 等待更老Store提交后再重试
                    LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu blocked by older store overlap, retry next cycle",
                        unit.instruction->get_instruction_id(), i);
                    continue;
                }
                
                LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu done, %s result=0x%" PRIx64 " -> CDB",
                    unit.instruction->get_instruction_id(),
                    i, (load_result == LoadExecutionResult::Forwarded ? "(store-forwarded)" : "(loaded-from-memory)"),
                    unit.result);
                
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
    if (unit.has_exception) {
        unit.instruction->set_exception(unit.exception_msg);
    } else {
        unit.instruction->clear_exception();
    }

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

ExecuteStage::LoadExecutionResult ExecuteStage::perform_load_execution(ExecutionUnit& unit, CPUState& state) {
    const auto& inst = unit.instruction->get_decoded_info();
    uint64_t addr = unit.load_address;
    uint8_t access_size = unit.load_size;
    
    // 尝试Store-to-Load Forwarding
    uint64_t forwarded_value;
    bool blocked = false;
    bool forwarded = state.store_buffer->forward_load(
        addr, access_size, forwarded_value, unit.instruction->get_instruction_id(), blocked);
    if (blocked) {
        return LoadExecutionResult::BlockedByStore;
    }
    
    if (forwarded) {
        if (inst.opcode == Opcode::LOAD_FP) {
            // FLW 需要nan-box到64位浮点寄存器；FLD 直接写入64位
            if (access_size == 4) {
                unit.result = 0xFFFFFFFF00000000ULL | (forwarded_value & 0xFFFFFFFFULL);
            } else {
                unit.result = forwarded_value;
            }
            LOGT(EXECUTE, "fp store-to-load forwarding hit: addr=0x%" PRIx64 " value=0x%" PRIx64,
                 addr, unit.result);
            return LoadExecutionResult::Forwarded;
        }

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
        return LoadExecutionResult::Forwarded; // 使用了转发
    } else {
        if (inst.opcode == Opcode::LOAD_FP) {
            unit.result = InstructionExecutor::loadFPFromMemory(state.memory, addr, inst.funct3);
            LOGT(EXECUTE, "fp memory load done: addr=0x%" PRIx64 " result=0x%" PRIx64, addr, unit.result);
            return LoadExecutionResult::LoadedFromMemory;
        }

        // 没有转发，从内存读取
        LOGT(EXECUTE, "store-to-load forwarding miss, read from memory");
        
        unit.result = InstructionExecutor::loadFromMemory(state.memory, addr, inst.funct3);
        
        LOGT(EXECUTE, "memory load done: addr=0x%" PRIx64 " result=0x%" PRIx64, addr, unit.result);
        return LoadExecutionResult::LoadedFromMemory; // 没有使用转发
    }
}

} // namespace riscv 
