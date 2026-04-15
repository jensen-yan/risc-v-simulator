#pragma once

#include "cpu/ooo/ooo_types.h"
#include <queue>
#include <set>
#include <vector>

namespace riscv {

// 物理寄存器状态
struct PhysicalRegister {
    uint64_t value;
    bool ready;             // 是否准备好
    ROBEntry producer_rob;  // 产生这个值的ROB表项
    
    PhysicalRegister() : value(0), ready(true), producer_rob(0) {}
};

// 重命名表项
struct RenameEntry {
    PhysRegNum physical_reg;
    bool valid;
    
    RenameEntry() : physical_reg(0), valid(false) {}
};

/**
 * 寄存器重命名单元
 * 
 * 功能：
 * 1. 维护逻辑寄存器到物理寄存器的映射
 * 2. 消除WAW和WAR冒险
 * 3. 管理物理寄存器的分配和释放
 */
class RegisterRenameUnit {
public:
    // 配置参数
    static constexpr int NUM_LOGICAL_REGS = 32;     // 逻辑寄存器数量
    static constexpr int NUM_LOGICAL_FP_REGS = 32;  // 逻辑浮点寄存器数量
    static constexpr int NUM_PHYSICAL_REGS = 128;   // 物理寄存器数量
    
private:
    // 重命名表：逻辑寄存器 -> 物理寄存器
    std::vector<RenameEntry> rename_table;
    std::vector<RenameEntry> fp_rename_table;
    
    // 物理寄存器文件
    std::vector<PhysicalRegister> physical_registers;
    std::vector<PhysicalRegister> fp_physical_registers;
    
    // 空闲物理寄存器列表
    std::queue<PhysRegNum> free_list;
    std::queue<PhysRegNum> fp_free_list;
    
    // 架构状态映射表（用于异常恢复）
    std::vector<PhysRegNum> arch_map;
    std::vector<PhysRegNum> fp_arch_map;
    
    // 统计信息
    uint64_t rename_count;
    uint64_t stall_count;
    
public:
    RegisterRenameUnit();
    
    // 重命名操作
    struct RenameResult {
        PhysRegNum src1_reg;
        PhysRegNum src2_reg;  
        PhysRegNum dest_reg;
        bool success;
        bool src1_ready;
        bool src2_ready;
        uint64_t src1_value;
        uint64_t src2_value;
    };

    struct SourceLookupResult {
        PhysRegNum reg = 0;
        bool ready = true;
        uint64_t value = 0;
    };

    struct DestinationAllocateResult {
        PhysRegNum dest_reg = 0;
        bool success = false;
    };

    struct Checkpoint {
        std::vector<RenameEntry> rename_table;
        std::vector<RenameEntry> fp_rename_table;
        std::queue<PhysRegNum> free_list;
        std::queue<PhysRegNum> fp_free_list;
    };
    
    // 对指令进行重命名
    RenameResult rename_instruction(const DecodedInstruction& instruction);
    SourceLookupResult lookup_source(RegisterFileKind kind, RegNum logical_reg) const;
    DestinationAllocateResult allocate_destination(RegisterFileKind kind, RegNum logical_reg);
    
    // 更新物理寄存器值
    void update_physical_register(PhysRegNum reg, uint64_t value, ROBEntry rob_entry);
    void update_physical_register(RegisterFileKind kind, PhysRegNum reg, uint64_t value, ROBEntry rob_entry);
    
    // 释放物理寄存器
    void release_physical_register(PhysRegNum reg);
    void release_physical_register(RegisterFileKind kind, PhysRegNum reg);
    
    // 获取物理寄存器值
    uint64_t get_physical_register_value(PhysRegNum reg) const;
    uint64_t get_physical_register_value(RegisterFileKind kind, PhysRegNum reg) const;
    
    // 检查物理寄存器是否准备好
    bool is_physical_register_ready(PhysRegNum reg) const;
    bool is_physical_register_ready(RegisterFileKind kind, PhysRegNum reg) const;
    
    // 刷新流水线（分支预测错误时）
    void flush_pipeline();

    // 捕获/恢复投机检查点（用于控制流早恢复）
    Checkpoint capture_checkpoint() const;
    void restore_checkpoint(const Checkpoint& checkpoint);
    void restore_checkpoint(const Checkpoint& checkpoint, const std::vector<PhysRegNum>& live_physical_regs);
    void restore_checkpoint(const Checkpoint& checkpoint,
                            const std::vector<PhysRegNum>& live_physical_regs,
                            const std::vector<PhysRegNum>& live_fp_physical_regs);
    
    // 提交指令（更新架构状态）
    void commit_instruction(RegNum logical_reg, PhysRegNum physical_reg);
    void commit_instruction(RegisterFileKind kind, RegNum logical_reg, PhysRegNum physical_reg);
    
    // 获取统计信息
    void get_statistics(uint64_t& renames, uint64_t& stalls) const;
    
    // 更新架构寄存器值（用于DiffTest同步）
    void update_architecture_register(RegNum logical_reg, uint64_t value);
    void update_architecture_register(RegisterFileKind kind, RegNum logical_reg, uint64_t value);
    
    // 调试输出
    void dump_rename_table() const;
    void dump_physical_registers() const;
    void dump_free_list() const;
    
    // 检查是否有空闲物理寄存器
    bool has_free_register() const;
    
    // 获取空闲物理寄存器数量
    size_t get_free_register_count() const;
    
private:
    // 分配物理寄存器
    PhysRegNum allocate_physical_register();
    PhysRegNum allocate_physical_register(RegisterFileKind kind);
    
    // 初始化重命名表
    void initialize_rename_table();
    
    // 初始化物理寄存器
    void initialize_physical_registers();
    
    // 初始化空闲列表
    void initialize_free_list();

    void rebuild_free_list_excluding(const std::set<PhysRegNum>& reserved_regs);
    void rebuild_free_list_excluding(RegisterFileKind kind, const std::set<PhysRegNum>& reserved_regs);

    std::vector<RenameEntry>& table_for_kind(RegisterFileKind kind);
    const std::vector<RenameEntry>& table_for_kind(RegisterFileKind kind) const;
    std::vector<PhysicalRegister>& physicals_for_kind(RegisterFileKind kind);
    const std::vector<PhysicalRegister>& physicals_for_kind(RegisterFileKind kind) const;
    std::queue<PhysRegNum>& free_list_for_kind(RegisterFileKind kind);
    const std::vector<PhysRegNum>& arch_map_for_kind(RegisterFileKind kind) const;
    std::vector<PhysRegNum>& arch_map_for_kind(RegisterFileKind kind);
};

} // namespace riscv
