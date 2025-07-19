#pragma once

#include "cpu/ooo/ooo_types.h"
#include "cpu/ooo/dynamic_inst.h"
#include <vector>
#include <string>

namespace riscv {

// 注意：原来的 ReservationStationEntry 已被 DynamicInst 替代
// 保留这个别名以便于向后兼容和渐进式迁移
using ReservationStationEntry = DynamicInst;

/**
 * 保留站调度单元
 * 
 * 功能：
 * 1. 管理等待执行的指令
 * 2. 监听公共数据总线(CDB)，更新操作数状态
 * 3. 选择准备好的指令发射到执行单元
 * 4. 支持不同类型的执行单元（ALU、分支、访存）
 */
class ReservationStation {
private:
    // 配置参数
    static const int MAX_RS_ENTRIES = 16;        // 保留站最大容量
    static const int MAX_ALU_UNITS = 2;          // ALU执行单元数量
    static const int MAX_BRANCH_UNITS = 1;       // 分支执行单元数量
    static const int MAX_LOAD_UNITS = 1;         // 加载执行单元数量
    static const int MAX_STORE_UNITS = 1;        // 存储执行单元数量
    
    // 保留站表项
    std::vector<DynamicInstPtr> rs_entries;
    
    
    // 执行单元忙碌状态
    std::vector<bool> alu_units_busy;
    std::vector<bool> branch_units_busy;
    std::vector<bool> load_units_busy;
    std::vector<bool> store_units_busy;
    
    // 统计信息
    uint64_t issued_count;
    uint64_t dispatched_count;
    uint64_t stall_count;
    
public:
    ReservationStation();
    
    // 发射操作结果
    struct IssueResult {
        bool success;
        RSEntry rs_entry;
        std::string error_message;
    };
    
    // 调度操作结果
    struct DispatchResult {
        bool success;
        RSEntry rs_entry;
        ExecutionUnitType unit_type;
        int unit_id;
        DynamicInstPtr instruction;
        std::string error_message;
    };
    
    // 发射指令到保留站（使用DynamicInst）
    IssueResult issue_instruction(DynamicInstPtr dynamic_inst);
    
    // 尝试调度一条准备好的指令
    DispatchResult dispatch_instruction();
    
    // 更新操作数（来自CDB）
    void update_operands(const CommonDataBusEntry& cdb_entry);
    
    // 释放保留站表项
    void release_entry(RSEntry rs_entry);
    
    // 刷新保留站（分支预测错误时）
    void flush_pipeline();
    
    // 检查是否有空闲保留站表项
    bool has_free_entry() const;
    
    // 获取空闲表项数量
    size_t get_free_entry_count() const;
    
    // 检查执行单元是否可用
    bool is_execution_unit_available(ExecutionUnitType unit_type) const;
    
    // 分配执行单元
    int allocate_execution_unit(ExecutionUnitType unit_type);
    
    // 释放执行单元
    void release_execution_unit(ExecutionUnitType unit_type, int unit_id);
    
    // 获取统计信息
    void get_statistics(uint64_t& issued, uint64_t& dispatched, uint64_t& stalls) const;
    
    // 调试输出
    void dump_reservation_station() const;
    void dump_execution_units() const;
    
    // 获取指定表项的详细信息
    DynamicInstPtr get_entry(RSEntry rs_entry) const;
    
    // 检查表项是否准备好执行
    bool is_entry_ready(RSEntry rs_entry) const;
    
private:
    // 分配保留站表项
    RSEntry allocate_entry();
    
    // 初始化空闲列表
    void initialize_free_list();
    
    // 初始化执行单元
    void initialize_execution_units();
    
    // 检查指令是否准备好执行
    bool is_instruction_ready(DynamicInstPtr instruction) const;
    
    // 选择优先级最高的准备好的指令
    RSEntry select_ready_instruction() const;
    
    // 计算指令优先级（越小优先级越高）
    int calculate_priority(DynamicInstPtr instruction) const;
};

} // namespace riscv