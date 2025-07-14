#include "register_rename.h"
#include "debug_types.h"
#include <iostream>
#include <iomanip>
#include <cassert>

namespace riscv {

RegisterRenameUnit::RegisterRenameUnit() 
    : rename_table(NUM_LOGICAL_REGS), 
      physical_registers(NUM_PHYSICAL_REGS),
      arch_map(NUM_LOGICAL_REGS),
      rename_count(0), 
      stall_count(0) {
    
    initialize_physical_registers();
    initialize_rename_table();
    initialize_free_list();
}

void RegisterRenameUnit::initialize_physical_registers() {
    for (int i = 0; i < NUM_PHYSICAL_REGS; ++i) {
        physical_registers[i] = PhysicalRegister();
        physical_registers[i].ready = true;
        physical_registers[i].value = 0;
    }
    
    // x0寄存器永远是0
    physical_registers[0].value = 0;
    physical_registers[0].ready = true;
}

void RegisterRenameUnit::initialize_rename_table() {
    // 初始状态：逻辑寄存器直接映射到对应的物理寄存器
    for (int i = 0; i < NUM_LOGICAL_REGS; ++i) {
        rename_table[i].physical_reg = i;
        rename_table[i].valid = true;
        arch_map[i] = i;  // 架构状态映射
    }
}

void RegisterRenameUnit::initialize_free_list() {
    // 物理寄存器32-127是可分配的
    for (int i = NUM_LOGICAL_REGS; i < NUM_PHYSICAL_REGS; ++i) {
        free_list.push(i);
    }
}

RegisterRenameUnit::RenameResult RegisterRenameUnit::rename_instruction(
    const DecodedInstruction& instruction) {
    
    RenameResult result;
    result.success = false;
    
    // 检查是否需要分配新的物理寄存器
    bool needs_dest_reg = (instruction.rd != 0);  // x0不需要分配
    
    if (needs_dest_reg && free_list.empty()) {
        // 没有空闲的物理寄存器，发生停顿
        stall_count++;
        return result;
    }
    
    // 重命名源寄存器
    if (instruction.rs1 < NUM_LOGICAL_REGS) {
        result.src1_reg = rename_table[instruction.rs1].physical_reg;
        result.src1_ready = physical_registers[result.src1_reg].ready;
        result.src1_value = physical_registers[result.src1_reg].value;
    }
    
    if (instruction.rs2 < NUM_LOGICAL_REGS) {
        result.src2_reg = rename_table[instruction.rs2].physical_reg;
        result.src2_ready = physical_registers[result.src2_reg].ready;
        result.src2_value = physical_registers[result.src2_reg].value;
    }
    
    // 重命名目标寄存器
    if (needs_dest_reg) {
        result.dest_reg = allocate_physical_register();
        
        // 更新重命名表
        PhysRegNum old_physical_reg = rename_table[instruction.rd].physical_reg;
        rename_table[instruction.rd].physical_reg = result.dest_reg;
        
        // 新分配的物理寄存器还没有值
        physical_registers[result.dest_reg].ready = false;
        
        dprintf(RENAME, "重命名: x%d 从 p%d 重命名到 p%d", 
                (int)instruction.rd, (int)old_physical_reg, (int)result.dest_reg);
    } else {
        result.dest_reg = 0;  // x0寄存器
    }
    
    result.success = true;
    rename_count++;
    
    return result;
}

PhysRegNum RegisterRenameUnit::allocate_physical_register() {
    assert(!free_list.empty() && "没有空闲的物理寄存器");
    
    PhysRegNum reg = free_list.front();
    free_list.pop();
    
    return reg;
}

void RegisterRenameUnit::update_physical_register(PhysRegNum reg, uint32_t value, ROBEntry rob_entry) {
    if (reg == 0) return;  // x0寄存器不能修改
    
    physical_registers[reg].value = value;
    physical_registers[reg].ready = true;
    physical_registers[reg].producer_rob = rob_entry;
    
    dprintf(RENAME, "更新物理寄存器 p%d = 0x%x", (int)reg, value);
}

void RegisterRenameUnit::release_physical_register(PhysRegNum reg) {
    if (reg < NUM_LOGICAL_REGS) return;  // 不释放初始的32个寄存器
    
    physical_registers[reg].ready = true;
    physical_registers[reg].value = 0;
    free_list.push(reg);
    
    dprintf(RENAME, "释放物理寄存器 p%d", (int)reg);
}

uint32_t RegisterRenameUnit::get_physical_register_value(PhysRegNum reg) const {
    return physical_registers[reg].value;
}

bool RegisterRenameUnit::is_physical_register_ready(PhysRegNum reg) const {
    return physical_registers[reg].ready;
}

void RegisterRenameUnit::flush_pipeline() {
    // 恢复重命名表到架构状态
    for (int i = 0; i < NUM_LOGICAL_REGS; ++i) {
        rename_table[i].physical_reg = arch_map[i];
    }
    
    // 重新初始化空闲列表
    while (!free_list.empty()) {
        free_list.pop();
    }
    initialize_free_list();
    
    dprintf(RENAME, "流水线刷新：重命名表恢复到架构状态");
}

void RegisterRenameUnit::commit_instruction(RegNum logical_reg, PhysRegNum physical_reg) {
    if (logical_reg == 0) return;  // x0寄存器不更新架构状态
    
    PhysRegNum old_arch_reg = arch_map[logical_reg];
    arch_map[logical_reg] = physical_reg;
    
    // 释放旧的架构寄存器
    if (old_arch_reg >= NUM_LOGICAL_REGS) {
        release_physical_register(old_arch_reg);
    }
    
    dprintf(RENAME, "提交指令: x%d 架构状态更新为 p%d", (int)logical_reg, (int)physical_reg);
}

void RegisterRenameUnit::get_statistics(uint64_t& renames, uint64_t& stalls) const {
    renames = rename_count;
    stalls = stall_count;
}

bool RegisterRenameUnit::has_free_register() const {
    return !free_list.empty();
}

size_t RegisterRenameUnit::get_free_register_count() const {
    return free_list.size();
}

void RegisterRenameUnit::dump_rename_table() const {
    dprintf(RENAME, "重命名表");
    for (int i = 0; i < NUM_LOGICAL_REGS; ++i) {
        dprintf(RENAME, "x%d -> p%d", i, (int)rename_table[i].physical_reg);
    }
}

void RegisterRenameUnit::dump_physical_registers() const {
    dprintf(RENAME, "物理寄存器状态");
    for (int i = 0; i < NUM_PHYSICAL_REGS && i < 64; ++i) {  // 只显示前64个
        if (physical_registers[i].ready) {
            dprintf(RENAME, "p%d:0x%x", i, physical_registers[i].value);
        } else {
            dprintf(RENAME, "p%d:  等待中  ", i);
        }
        
        if (i % 4 == 3) dprintf(RENAME, "");
        else dprintf(RENAME, "  ");
    }
    dprintf(RENAME, "");
}

void RegisterRenameUnit::dump_free_list() const {
    dprintf(RENAME, "空闲寄存器列表");
    dprintf(RENAME, "空闲寄存器数量: %zu", free_list.size());
}

} // namespace riscv