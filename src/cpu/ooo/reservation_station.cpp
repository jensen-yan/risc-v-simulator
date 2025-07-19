#include "cpu/ooo/reservation_station.h"
#include "common/debug_types.h"
#include <climits>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cassert>

namespace riscv {

ReservationStation::ReservationStation() 
    : rs_entries(MAX_RS_ENTRIES),
      alu_units_busy(MAX_ALU_UNITS, false),
      branch_units_busy(MAX_BRANCH_UNITS, false),
      load_units_busy(MAX_LOAD_UNITS, false),
      store_units_busy(MAX_STORE_UNITS, false),
      issued_count(0),
      dispatched_count(0),
      stall_count(0) {
    
    initialize_free_list();
    initialize_execution_units();
}

void ReservationStation::initialize_free_list() {
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        rs_entries[i] = nullptr;  // 初始化为空指针
    }
}

void ReservationStation::initialize_execution_units() {
    std::fill(alu_units_busy.begin(), alu_units_busy.end(), false);
    std::fill(branch_units_busy.begin(), branch_units_busy.end(), false);
    std::fill(load_units_busy.begin(), load_units_busy.end(), false);
    std::fill(store_units_busy.begin(), store_units_busy.end(), false);
}

ReservationStation::IssueResult ReservationStation::issue_instruction(DynamicInstPtr dynamic_inst) {
    IssueResult result;
    result.success = false;
    
    if (!dynamic_inst) {
        result.error_message = "无效的DynamicInst指针";
        return result;
    }
    
    if (!has_free_entry()) {
        result.error_message = "保留站已满，无法发射指令";
        stall_count++;
        return result;
    }
    
    // 分配保留站表项
    RSEntry rs_id = allocate_entry();
    rs_entries[rs_id] = dynamic_inst;
    
    // 设置RS表项编号并更新状态
    dynamic_inst->set_rs_entry(rs_id);
    dynamic_inst->set_status(DynamicInst::Status::ISSUED);
    
    result.success = true;
    result.rs_entry = rs_id;
    issued_count++;
    
    dprintf(RS, "发射指令到保留站 RS%d, PC=0x%x, InstID=%lu", 
           (int)rs_id, dynamic_inst->get_pc(), dynamic_inst->get_instruction_id());
    
    return result;
}

ReservationStation::DispatchResult ReservationStation::dispatch_instruction() {
    DispatchResult result;
    result.success = false;
    
    // 选择一条准备好执行的指令
    RSEntry ready_rs = select_ready_instruction();
    if (ready_rs >= MAX_RS_ENTRIES) {
        result.error_message = "没有准备好的指令可调度";
        return result;
    }
    
    DynamicInstPtr instruction = rs_entries[ready_rs];
    if (!instruction) {
        result.error_message = "无效的指令指针";
        return result;
    }
    
    // 获取所需执行单元类型
    ExecutionUnitType unit_type = instruction->get_required_execution_unit();
    
    // 检查执行单元是否可用
    if (!is_execution_unit_available(unit_type)) {
        result.error_message = "执行单元不可用";
        stall_count++;
        return result;
    }
    
    // 分配执行单元
    int unit_id = allocate_execution_unit(unit_type);
    if (unit_id < 0) {
        result.error_message = "执行单元分配失败";
        return result;
    }
    
    // 更新指令状态
    instruction->set_status(DynamicInst::Status::EXECUTING);
    
    // 设置执行周期信息
    auto& exec_info = instruction->get_execution_info();
    exec_info.remaining_cycles = exec_info.execution_cycles;
    
    result.success = true;
    result.rs_entry = ready_rs;
    result.unit_type = unit_type;
    result.unit_id = unit_id;
    result.instruction = instruction;
    
    dispatched_count++;
    
    dprintf(RS, "调度指令到执行单元 %s%d, PC=0x%x, InstID=%lu", 
           (unit_type == ExecutionUnitType::ALU ? "ALU" :
            unit_type == ExecutionUnitType::BRANCH ? "BRANCH" :
            unit_type == ExecutionUnitType::LOAD ? "LOAD" : "STORE"),
           unit_id, instruction->get_pc(), instruction->get_instruction_id());
    
    // 注意：不在这里清空RS条目，等指令完成并写回CDB后再清空
    // 这样可以避免重复调度同一条指令
    
    return result;
}

void ReservationStation::update_operands(const CommonDataBusEntry& cdb_entry) {
    if (!cdb_entry.valid) return;
    
    // 遍历所有保留站表项，更新等待该物理寄存器的操作数
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        if (rs_entries[i]) {
            DynamicInstPtr inst = rs_entries[i];
            
            // 检查源操作数1
            if (!inst->is_src1_ready() && inst->get_physical_src1() == cdb_entry.dest_reg) {
                inst->set_src1_ready(true, cdb_entry.value);
                dprintf(RS, "RS%d 源操作数1就绪: p%d = 0x%x", i, cdb_entry.dest_reg, cdb_entry.value);
            }
            
            // 检查源操作数2
            if (!inst->is_src2_ready() && inst->get_physical_src2() == cdb_entry.dest_reg) {
                inst->set_src2_ready(true, cdb_entry.value);
                dprintf(RS, "RS%d 源操作数2就绪: p%d = 0x%x", i, cdb_entry.dest_reg, cdb_entry.value);
            }
        }
    }
}

void ReservationStation::release_entry(RSEntry rs_entry) {
    if (rs_entry >= MAX_RS_ENTRIES) return;
    
    if (rs_entries[rs_entry]) {
        dprintf(RS, "释放保留站表项 RS%d", (int)rs_entry);
        rs_entries[rs_entry] = nullptr;
    }
}

void ReservationStation::flush_pipeline() {
    dprintf(RS, "刷新保留站流水线");
    
    // 清空所有表项
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        rs_entries[i] = nullptr;
    }
    
    // 重新初始化
    initialize_free_list();
    initialize_execution_units();
}

bool ReservationStation::has_free_entry() const {
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        if (rs_entries[i] == nullptr) {
            return true;
        }
    }
    return false;
}

size_t ReservationStation::get_free_entry_count() const {
    size_t count = 0;
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        if (rs_entries[i] == nullptr) {
            count++;
        }
    }
    return count;
}

bool ReservationStation::is_execution_unit_available(ExecutionUnitType unit_type) const {
    switch (unit_type) {
        case ExecutionUnitType::ALU:
            return std::find(alu_units_busy.begin(), alu_units_busy.end(), false) != alu_units_busy.end();
        case ExecutionUnitType::BRANCH:
            return std::find(branch_units_busy.begin(), branch_units_busy.end(), false) != branch_units_busy.end();
        case ExecutionUnitType::LOAD:
            return std::find(load_units_busy.begin(), load_units_busy.end(), false) != load_units_busy.end();
        case ExecutionUnitType::STORE:
            return std::find(store_units_busy.begin(), store_units_busy.end(), false) != store_units_busy.end();
        default:
            return false;
    }
}

int ReservationStation::allocate_execution_unit(ExecutionUnitType unit_type) {
    switch (unit_type) {
        case ExecutionUnitType::ALU:
            for (int i = 0; i < MAX_ALU_UNITS; ++i) {
                if (!alu_units_busy[i]) {
                    alu_units_busy[i] = true;
                    return i;
                }
            }
            break;
        case ExecutionUnitType::BRANCH:
            for (int i = 0; i < MAX_BRANCH_UNITS; ++i) {
                if (!branch_units_busy[i]) {
                    branch_units_busy[i] = true;
                    return i;
                }
            }
            break;
        case ExecutionUnitType::LOAD:
            for (int i = 0; i < MAX_LOAD_UNITS; ++i) {
                if (!load_units_busy[i]) {
                    load_units_busy[i] = true;
                    return i;
                }
            }
            break;
        case ExecutionUnitType::STORE:
            for (int i = 0; i < MAX_STORE_UNITS; ++i) {
                if (!store_units_busy[i]) {
                    store_units_busy[i] = true;
                    return i;
                }
            }
            break;
    }
    return -1;  // 分配失败
}

void ReservationStation::release_execution_unit(ExecutionUnitType unit_type, int unit_id) {
    switch (unit_type) {
        case ExecutionUnitType::ALU:
            if (unit_id >= 0 && unit_id < MAX_ALU_UNITS) {
                alu_units_busy[unit_id] = false;
            }
            break;
        case ExecutionUnitType::BRANCH:
            if (unit_id >= 0 && unit_id < MAX_BRANCH_UNITS) {
                branch_units_busy[unit_id] = false;
            }
            break;
        case ExecutionUnitType::LOAD:
            if (unit_id >= 0 && unit_id < MAX_LOAD_UNITS) {
                load_units_busy[unit_id] = false;
            }
            break;
        case ExecutionUnitType::STORE:
            if (unit_id >= 0 && unit_id < MAX_STORE_UNITS) {
                store_units_busy[unit_id] = false;
            }
            break;
    }
}

void ReservationStation::get_statistics(uint64_t& issued, uint64_t& dispatched, uint64_t& stalls) const {
    issued = issued_count;
    dispatched = dispatched_count;
    stalls = stall_count;
}

void ReservationStation::dump_reservation_station() const {
    std::cout << "=== 保留站状态转储 ===" << std::endl;
    
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        if (rs_entries[i]) {
            std::cout << "[RS" << std::setw(2) << i << "] " 
                     << rs_entries[i]->to_string() << std::endl;
        }
    }
    
    std::cout << "===================" << std::endl;
}

void ReservationStation::dump_execution_units() const {
    std::cout << "=== 执行单元状态 ===" << std::endl;
    
    std::cout << "ALU Units: ";
    for (int i = 0; i < MAX_ALU_UNITS; ++i) {
        std::cout << (alu_units_busy[i] ? "BUSY " : "FREE ");
    }
    std::cout << std::endl;
    
    std::cout << "Branch Units: ";
    for (int i = 0; i < MAX_BRANCH_UNITS; ++i) {
        std::cout << (branch_units_busy[i] ? "BUSY " : "FREE ");
    }
    std::cout << std::endl;
    
    std::cout << "Load Units: ";
    for (int i = 0; i < MAX_LOAD_UNITS; ++i) {
        std::cout << (load_units_busy[i] ? "BUSY " : "FREE ");
    }
    std::cout << std::endl;
    
    std::cout << "Store Units: ";
    for (int i = 0; i < MAX_STORE_UNITS; ++i) {
        std::cout << (store_units_busy[i] ? "BUSY " : "FREE ");
    }
    std::cout << std::endl;
    
    std::cout << "=================" << std::endl;
}

DynamicInstPtr ReservationStation::get_entry(RSEntry rs_entry) const {
    if (rs_entry >= MAX_RS_ENTRIES) return nullptr;
    return rs_entries[rs_entry];
}

bool ReservationStation::is_entry_ready(RSEntry rs_entry) const {
    if (rs_entry >= MAX_RS_ENTRIES) return false;
    DynamicInstPtr inst = rs_entries[rs_entry];
    return inst && is_instruction_ready(inst);
}

// ========== 私有方法实现 ==========
RSEntry ReservationStation::allocate_entry() {
    // 寻找真正空闲的条目
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        if (rs_entries[i] == nullptr) {
            return i;
        }
    }
    
    // 没有空闲条目
    return MAX_RS_ENTRIES;
}

bool ReservationStation::is_instruction_ready(DynamicInstPtr instruction) const {
    if (!instruction) return false;
    return instruction->is_ready_to_execute();
}

RSEntry ReservationStation::select_ready_instruction() const {
    RSEntry best_entry = MAX_RS_ENTRIES;  // 无效值
    int best_priority = INT_MAX;
    
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        if (rs_entries[i]) {
            bool ready = is_instruction_ready(rs_entries[i]);
            bool is_executing = (rs_entries[i]->get_status() == DynamicInst::Status::EXECUTING);
            
            dprintf(RS, "RS[%d] Inst#%lu PC=0x%x ready=%s src1_ready=%s src2_ready=%s status=%s", 
                   i, rs_entries[i]->get_instruction_id(), rs_entries[i]->get_pc(),
                   ready ? "是" : "否",
                   rs_entries[i]->is_src1_ready() ? "是" : "否",
                   rs_entries[i]->is_src2_ready() ? "是" : "否",
                   rs_entries[i]->status_to_string(rs_entries[i]->get_status()));
            
            // 只调度准备好且没有在执行的指令
            if (ready && !is_executing) {
                int priority = calculate_priority(rs_entries[i]);
                if (priority < best_priority) {
                    best_priority = priority;
                    best_entry = i;
                }
            }
        }
    }
    
    dprintf(RS, "select_ready_instruction: 选择 RS%d", (int)best_entry);
    return best_entry;
}

int ReservationStation::calculate_priority(DynamicInstPtr instruction) const {
    if (!instruction) return INT_MAX;
    
    // 优先级计算：指令ID越小，优先级越高（程序顺序）
    // 可以根据需要调整优先级策略
    return static_cast<int>(instruction->get_instruction_id());
}

} // namespace riscv