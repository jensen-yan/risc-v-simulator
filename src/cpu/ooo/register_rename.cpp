#include "cpu/ooo/register_rename.h"
#include "common/debug_types.h"
#include <cassert>
#include <set> // Added for std::set

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
        LOGW(RENAME, "rename failed: need dst x%d but no free physical register",
                (int)instruction.rd);
        LOGT(RENAME, "free physical registers: %zu/%d", free_list.size(), NUM_PHYSICAL_REGS);
        return result;
    }
    
    // 重命名源寄存器
    if (instruction.rs1 < NUM_LOGICAL_REGS) {
        result.src1_reg = rename_table[instruction.rs1].physical_reg;
        result.src1_ready = physical_registers[result.src1_reg].ready;
        result.src1_value = physical_registers[result.src1_reg].value;
    }
    
    // 检查指令是否需要第二个源操作数
    bool needs_src2 = false;
    switch (instruction.type) {
        case InstructionType::R_TYPE:
            needs_src2 = true; // R-type指令总是需要两个源操作数
            break;
        case InstructionType::S_TYPE:
            needs_src2 = true; // Store指令需要地址基址(rs1)和数据(rs2)
            break;
        case InstructionType::B_TYPE:
            needs_src2 = true; // 分支指令需要两个比较操作数
            break;
        case InstructionType::I_TYPE:
        case InstructionType::U_TYPE:
        case InstructionType::J_TYPE:
        case InstructionType::SYSTEM_TYPE:
            needs_src2 = false; // 这些类型只需要一个源操作数或不需要源操作数
            break;
        default:
            // UNKNOWN指令类型，应该在解码阶段就被捕获
            LOGW(RENAME, "unknown instruction type in rename, treat as no src2");
            needs_src2 = false;
            break;
    }
    
    if (needs_src2 && instruction.rs2 < NUM_LOGICAL_REGS) {
        result.src2_reg = rename_table[instruction.rs2].physical_reg;
        result.src2_ready = physical_registers[result.src2_reg].ready;
        result.src2_value = physical_registers[result.src2_reg].value;
    } else {
        // 不需要第二个源操作数，设置为就绪状态
        result.src2_reg = 0; // 使用物理寄存器0 (x0)
        result.src2_ready = true;
        result.src2_value = 0;
    }
    
    // 重命名目标寄存器
    if (needs_dest_reg) {
        result.dest_reg = allocate_physical_register();
        
        // 更新重命名表
        PhysRegNum old_physical_reg = rename_table[instruction.rd].physical_reg;
        rename_table[instruction.rd].physical_reg = result.dest_reg;
        
        // 新分配的物理寄存器还没有值
        physical_registers[result.dest_reg].ready = false;
        // 关键修复：处理自依赖情况（源寄存器和目标寄存器相同）
        // 需要确保源寄存器仍然指向旧的物理寄存器
        if (instruction.rs1 == instruction.rd && instruction.rs1 < NUM_LOGICAL_REGS) {
            result.src1_reg = old_physical_reg;
            result.src1_ready = physical_registers[old_physical_reg].ready;
            result.src1_value = physical_registers[old_physical_reg].value;
            LOGT(RENAME, "self-dependency fix: x%d rs1 uses old p%d, dst uses new p%d",
                    (int)instruction.rd, (int)old_physical_reg, (int)result.dest_reg);
        }
        
        // 同样处理rs2的自依赖情况（例如某些R-type指令可能有rs2 == rd）
        if (instruction.rs2 == instruction.rd && instruction.rs2 < NUM_LOGICAL_REGS) {
            result.src2_reg = old_physical_reg;
            result.src2_ready = physical_registers[old_physical_reg].ready;
            result.src2_value = physical_registers[old_physical_reg].value;
            LOGT(RENAME, "self-dependency fix: x%d rs2 uses old p%d, dst uses new p%d",
                    (int)instruction.rd, (int)old_physical_reg, (int)result.dest_reg);
        }
        
        LOGT(RENAME, "rename: x%d from p%d to p%d",
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

void RegisterRenameUnit::update_physical_register(PhysRegNum reg, uint64_t value, ROBEntry rob_entry) {
    if (reg == 0) return;  // x0寄存器不能修改
    
    physical_registers[reg].value = value;
    physical_registers[reg].ready = true;
    physical_registers[reg].producer_rob = rob_entry;
    
    LOGT(RENAME, "update p%d = 0x%" PRIx64, (int)reg, value);
}

void RegisterRenameUnit::release_physical_register(PhysRegNum reg) {
    if (reg < NUM_LOGICAL_REGS) return;  // 不释放初始的32个寄存器
    
    physical_registers[reg].ready = true;
    physical_registers[reg].value = 0;
    free_list.push(reg);
    
    LOGT(RENAME, "release p%d", (int)reg);
}

uint64_t RegisterRenameUnit::get_physical_register_value(PhysRegNum reg) const {
    return physical_registers[reg].value;
}

bool RegisterRenameUnit::is_physical_register_ready(PhysRegNum reg) const {
    return physical_registers[reg].ready;
}

void RegisterRenameUnit::flush_pipeline() {
    // 关键修复：流水线刷新应该只清除推测性状态，保留已提交的架构状态
    // 将rename_table恢复到与arch_map一致的状态，而不是重置到初始状态
    
    for (int i = 0; i < NUM_LOGICAL_REGS; ++i) {
        // 将重命名表恢复到已提交的架构状态
        rename_table[i].physical_reg = arch_map[i];
        rename_table[i].valid = true;
    }
    
    // 释放所有未提交的物理寄存器
    // 保留arch_map中已提交的物理寄存器
    std::set<PhysRegNum> committed_regs;
    for (int i = 0; i < NUM_LOGICAL_REGS; ++i) {
        committed_regs.insert(arch_map[i]);
    }
    
    // 重新初始化空闲列表，排除已提交的寄存器
    while (!free_list.empty()) {
        free_list.pop();
    }
    for (int i = NUM_LOGICAL_REGS; i < NUM_PHYSICAL_REGS; ++i) {
        if (committed_regs.find(i) == committed_regs.end()) {
            free_list.push(i);
        }
    }
    
    LOGT(RENAME, "flush pipeline: restore rename table to committed architectural state");
}

void RegisterRenameUnit::commit_instruction(RegNum logical_reg, PhysRegNum physical_reg) {
    if (logical_reg == 0) return;  // x0寄存器不更新架构状态
    
    PhysRegNum old_arch_reg = arch_map[logical_reg];
    arch_map[logical_reg] = physical_reg;
    
    // 关键修复：如果rename_table中的映射与即将提交的物理寄存器相同，
    // 或者rename_table指向的是旧的架构寄存器，则更新rename_table
    if (rename_table[logical_reg].physical_reg == physical_reg ||
        rename_table[logical_reg].physical_reg == old_arch_reg) {
        rename_table[logical_reg].physical_reg = physical_reg;
        rename_table[logical_reg].valid = true;
        LOGT(RENAME, "on commit update rename_table[%d] -> p%d", (int)logical_reg, (int)physical_reg);
    }
    
    // 释放旧的架构寄存器
    if (old_arch_reg >= NUM_LOGICAL_REGS) {
        release_physical_register(old_arch_reg);
    }
    
    LOGT(RENAME, "commit: architectural x%d -> p%d", (int)logical_reg, (int)physical_reg);
}

void RegisterRenameUnit::get_statistics(uint64_t& renames, uint64_t& stalls) const {
    renames = rename_count;
    stalls = stall_count;
}

void RegisterRenameUnit::update_architecture_register(RegNum logical_reg, uint64_t value) {
    if (logical_reg == 0) return;  // x0寄存器不更新
    
    // 直接更新架构寄存器映射中的值
    // 这确保DiffTest比较时状态一致
    PhysRegNum current_arch_reg = arch_map[logical_reg];
    if (current_arch_reg < NUM_LOGICAL_REGS) {
        // 如果是初始寄存器，直接更新其值
        physical_registers[current_arch_reg].value = value;
    }
    
    LOGT(RENAME, "update architectural x%d = 0x%" PRIx64, (int)logical_reg, value);
}

bool RegisterRenameUnit::has_free_register() const {
    return !free_list.empty();
}

size_t RegisterRenameUnit::get_free_register_count() const {
    return free_list.size();
}

void RegisterRenameUnit::dump_rename_table() const {
    LOGT(RENAME, "rename table");
    for (int i = 0; i < NUM_LOGICAL_REGS; ++i) {
        LOGT(RENAME, "x%d -> p%d", i, (int)rename_table[i].physical_reg);
    }
}

void RegisterRenameUnit::dump_physical_registers() const {
    LOGT(RENAME, "physical register state");
    for (int i = 0; i < NUM_PHYSICAL_REGS && i < 64; ++i) {  // 只显示前64个
        if (physical_registers[i].ready) {
            LOGT(RENAME, "p%d:0x%" PRIx64, i, physical_registers[i].value);
        } else {
            LOGT(RENAME, "p%d: pending", i);
        }
        
        if (i % 4 == 3) {
            LOGT(RENAME, "---");
        }
    }
    LOGT(RENAME, "---");
}

void RegisterRenameUnit::dump_free_list() const {
    LOGT(RENAME, "free register list");
    LOGT(RENAME, "free register count: %zu", free_list.size());
}

} // namespace riscv
