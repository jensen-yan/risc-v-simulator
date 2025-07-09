#include "reservation_station.h"
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
        free_entries.push(i);
        rs_entries[i].valid = false;
    }
}

void ReservationStation::initialize_execution_units() {
    std::fill(alu_units_busy.begin(), alu_units_busy.end(), false);
    std::fill(branch_units_busy.begin(), branch_units_busy.end(), false);
    std::fill(load_units_busy.begin(), load_units_busy.end(), false);
    std::fill(store_units_busy.begin(), store_units_busy.end(), false);
}

ReservationStation::IssueResult ReservationStation::issue_instruction(
    const ReservationStationEntry& entry) {
    
    IssueResult result;
    result.success = false;
    
    if (free_entries.empty()) {
        result.error_message = "保留站已满，无法发射指令";
        stall_count++;
        return result;
    }
    
    // 分配保留站表项
    RSEntry rs_id = allocate_entry();
    rs_entries[rs_id] = entry;
    rs_entries[rs_id].valid = true;
    
    result.success = true;
    result.rs_entry = rs_id;
    issued_count++;
    
    std::cout << "发射指令到保留站 RS" << (int)rs_id 
              << ", PC=0x" << std::hex << entry.pc << std::dec << std::endl;
    
    return result;
}

ReservationStation::DispatchResult ReservationStation::dispatch_instruction() {
    DispatchResult result;
    result.success = false;
    
    // 选择准备好的指令
    RSEntry ready_rs = select_ready_instruction();
    if (ready_rs == MAX_RS_ENTRIES) {
        // 没有准备好的指令
        return result;
    }
    
    const auto& entry = rs_entries[ready_rs];
    ExecutionUnitType unit_type = get_required_execution_unit(entry.instruction);
    
    // 检查执行单元是否可用
    if (!is_execution_unit_available(unit_type)) {
        // 执行单元忙碌
        return result;
    }
    
    // 分配执行单元
    int unit_id = allocate_execution_unit(unit_type);
    if (unit_id < 0) {
        return result;
    }
    
    // 准备调度结果
    result.success = true;
    result.rs_entry = ready_rs;
    result.unit_type = unit_type;
    result.unit_id = unit_id;
    result.instruction = entry;
    
    // 释放保留站表项
    release_entry(ready_rs);
    dispatched_count++;
    
    std::cout << "调度指令 RS" << (int)ready_rs << " 到执行单元 ";
    switch (unit_type) {
        case ExecutionUnitType::ALU: std::cout << "ALU" << unit_id; break;
        case ExecutionUnitType::BRANCH: std::cout << "BRANCH" << unit_id; break;
        case ExecutionUnitType::LOAD: std::cout << "LOAD" << unit_id; break;
        case ExecutionUnitType::STORE: std::cout << "STORE" << unit_id; break;
    }
    std::cout << std::endl;
    
    return result;
}

void ReservationStation::update_operands(const CommonDataBusEntry& cdb_entry) {
    if (!cdb_entry.valid) return;
    
    int updated_count = 0;
    
    // 遍历所有有效的保留站表项
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        if (!rs_entries[i].valid) continue;
        
        // 更新源操作数1
        if (!rs_entries[i].src1_ready && rs_entries[i].src1_reg == cdb_entry.dest_reg) {
            rs_entries[i].src1_ready = true;
            rs_entries[i].src1_value = cdb_entry.value;
            updated_count++;
        }
        
        // 更新源操作数2
        if (!rs_entries[i].src2_ready && rs_entries[i].src2_reg == cdb_entry.dest_reg) {
            rs_entries[i].src2_ready = true;
            rs_entries[i].src2_value = cdb_entry.value;
            updated_count++;
        }
    }
    
    if (updated_count > 0) {
        std::cout << "CDB更新: p" << (int)cdb_entry.dest_reg 
                  << " = 0x" << std::hex << cdb_entry.value << std::dec
                  << ", 更新了 " << updated_count << " 个操作数" << std::endl;
    }
}

void ReservationStation::release_entry(RSEntry rs_entry) {
    assert(rs_entry < MAX_RS_ENTRIES && "保留站表项ID无效");
    
    rs_entries[rs_entry].valid = false;
    free_entries.push(rs_entry);
}

void ReservationStation::flush_pipeline() {
    // 清空所有保留站表项
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        rs_entries[i].valid = false;
    }
    
    // 重新初始化空闲列表
    while (!free_entries.empty()) {
        free_entries.pop();
    }
    initialize_free_list();
    
    // 释放所有执行单元
    initialize_execution_units();
    
    std::cout << "保留站刷新：清空所有表项和执行单元" << std::endl;
}

bool ReservationStation::has_free_entry() const {
    return !free_entries.empty();
}

size_t ReservationStation::get_free_entry_count() const {
    return free_entries.size();
}

bool ReservationStation::is_execution_unit_available(ExecutionUnitType unit_type) const {
    switch (unit_type) {
        case ExecutionUnitType::ALU:
            return std::find(alu_units_busy.begin(), alu_units_busy.end(), false) 
                   != alu_units_busy.end();
        case ExecutionUnitType::BRANCH:
            return std::find(branch_units_busy.begin(), branch_units_busy.end(), false) 
                   != branch_units_busy.end();
        case ExecutionUnitType::LOAD:
            return std::find(load_units_busy.begin(), load_units_busy.end(), false) 
                   != load_units_busy.end();
        case ExecutionUnitType::STORE:
            return std::find(store_units_busy.begin(), store_units_busy.end(), false) 
                   != store_units_busy.end();
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
    return -1; // 没有可用的执行单元
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

RSEntry ReservationStation::allocate_entry() {
    assert(!free_entries.empty() && "没有空闲的保留站表项");
    
    RSEntry entry = free_entries.front();
    free_entries.pop();
    return entry;
}

ExecutionUnitType ReservationStation::get_required_execution_unit(
    const DecodedInstruction& instruction) const {
    
    switch (instruction.type) {
        case InstructionType::R_TYPE:
            // R型指令使用ALU
            return ExecutionUnitType::ALU;
            
        case InstructionType::I_TYPE:
            // I型指令：根据操作码区分是否为加载指令
            if (instruction.opcode == Opcode::LOAD) {
                return ExecutionUnitType::LOAD;
            } else {
                return ExecutionUnitType::ALU;
            }
            
        case InstructionType::S_TYPE:
            // S型指令是存储指令
            return ExecutionUnitType::STORE;
            
        case InstructionType::B_TYPE:
            // 分支指令使用分支单元
            return ExecutionUnitType::BRANCH;
            
        case InstructionType::J_TYPE:
        case InstructionType::U_TYPE:
        default:
            // 其他指令默认使用ALU
            return ExecutionUnitType::ALU;
    }
}

bool ReservationStation::is_instruction_ready(const ReservationStationEntry& entry) const {
    return entry.valid && entry.src1_ready && entry.src2_ready;
}

RSEntry ReservationStation::select_ready_instruction() const {
    RSEntry best_entry = MAX_RS_ENTRIES;
    int best_priority = INT_MAX;
    
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        if (!rs_entries[i].valid) continue;
        
        if (is_instruction_ready(rs_entries[i])) {
            int priority = calculate_priority(rs_entries[i]);
            if (priority < best_priority) {
                best_priority = priority;
                best_entry = i;
            }
        }
    }
    
    return best_entry;
}

int ReservationStation::calculate_priority(const ReservationStationEntry& entry) const {
    // 简单的优先级策略：按照ROB表项编号，越小优先级越高
    // 这样保证程序顺序的相对保持
    return entry.rob_entry;
}

const ReservationStationEntry& ReservationStation::get_entry(RSEntry rs_entry) const {
    assert(rs_entry < MAX_RS_ENTRIES && "保留站表项ID无效");
    return rs_entries[rs_entry];
}

bool ReservationStation::is_entry_ready(RSEntry rs_entry) const {
    assert(rs_entry < MAX_RS_ENTRIES && "保留站表项ID无效");
    return is_instruction_ready(rs_entries[rs_entry]);
}

void ReservationStation::dump_reservation_station() const {
    std::cout << "\n=== 保留站状态 ===" << std::endl;
    std::cout << "空闲表项: " << free_entries.size() << "/" << MAX_RS_ENTRIES << std::endl;
    
    std::cout << "\n有效表项:" << std::endl;
    std::cout << "ID  PC     指令  Src1  Src2  Dest  Ready ROB" << std::endl;
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        if (rs_entries[i].valid) {
            const auto& entry = rs_entries[i];
            std::cout << std::setw(2) << i << "  "
                      << "0x" << std::hex << std::setw(4) << entry.pc << std::dec << "  "
                      << std::setw(4) << (int)entry.instruction.type << "  "
                      << "p" << std::setw(2) << (int)entry.src1_reg << "   "
                      << "p" << std::setw(2) << (int)entry.src2_reg << "   "
                      << "p" << std::setw(2) << (int)entry.dest_reg << "   "
                      << (entry.src1_ready && entry.src2_ready ? "是" : "否") << "   "
                      << std::setw(3) << entry.rob_entry << std::endl;
        }
    }
}

void ReservationStation::dump_execution_units() const {
    std::cout << "\n=== 执行单元状态 ===" << std::endl;
    
    std::cout << "ALU单元: ";
    for (int i = 0; i < MAX_ALU_UNITS; ++i) {
        std::cout << "ALU" << i << "(" << (alu_units_busy[i] ? "忙" : "闲") << ") ";
    }
    std::cout << std::endl;
    
    std::cout << "分支单元: ";
    for (int i = 0; i < MAX_BRANCH_UNITS; ++i) {
        std::cout << "BR" << i << "(" << (branch_units_busy[i] ? "忙" : "闲") << ") ";
    }
    std::cout << std::endl;
    
    std::cout << "加载单元: ";
    for (int i = 0; i < MAX_LOAD_UNITS; ++i) {
        std::cout << "LD" << i << "(" << (load_units_busy[i] ? "忙" : "闲") << ") ";
    }
    std::cout << std::endl;
    
    std::cout << "存储单元: ";
    for (int i = 0; i < MAX_STORE_UNITS; ++i) {
        std::cout << "ST" << i << "(" << (store_units_busy[i] ? "忙" : "闲") << ") ";
    }
    std::cout << std::endl;
}

} // namespace riscv