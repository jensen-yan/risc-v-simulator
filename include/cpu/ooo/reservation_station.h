#pragma once

#include "cpu/ooo/ooo_types.h"
#include "cpu/ooo/dynamic_inst.h"
#include <vector>
#include <string>

namespace riscv {

class StoreBuffer;

// 注意：原来的 ReservationStationEntry 已被 DynamicInst 替代
// 保留这个别名以便于向后兼容和渐进式迁移
using ReservationStationEntry = DynamicInst;

/**
 * 保留站 / 简化 Issue Queue
 * 
 * 功能：
 * 1. 管理等待执行的动态指令
 * 2. 监听完成事件，更新操作数状态
 * 3. 暴露按程序顺序排列的 ready entries，供 IssueReadySelect 裁决
 */
class ReservationStation {
private:
    // 配置参数
    static const int MAX_RS_ENTRIES = static_cast<int>(OOOPipelineConfig::RS_ENTRIES);
    // 保留站表项
    std::vector<DynamicInstPtr> rs_entries;

    // 统计信息
    uint64_t dispatched_count;
    uint64_t stall_count;
    
public:
    ReservationStation();
    
    // 派发到保留站的结果
    struct DispatchResult {
        bool success;
        RSEntry rs_entry;
        std::string error_message;
    };
    
    struct ReadyEntry {
        RSEntry rs_entry;
        DynamicInstPtr instruction;
    };
    
    // 派发指令到保留站（使用DynamicInst）
    DispatchResult dispatch_instruction(DynamicInstPtr dynamic_inst);
    
    // 获取当前可参与 Issue / Ready Select 的 ready 表项，按程序顺序返回。
    std::vector<ReadyEntry> ready_entries() const;
    
    // 更新操作数（来自完成事件）
    void update_operands(const CompletionEvent& completion_event, StoreBuffer* store_buffer);
    
    // 释放保留站表项
    void release_entry(RSEntry rs_entry);
    
    // 刷新保留站（分支预测错误时）
    void flush_pipeline();
    void flush_younger_than(uint64_t instruction_id);
    
    // 检查是否有空闲保留站表项
    bool has_free_entry() const;
    
    // 获取空闲表项数量
    size_t get_free_entry_count() const;
    
    // 获取统计信息
    void get_statistics(uint64_t& dispatched, uint64_t& stalls) const;
    
    // 调试输出
    void dump_reservation_station() const;
    
    // 获取指定表项的详细信息
    DynamicInstPtr get_entry(RSEntry rs_entry) const;

    // 统计查询（用于性能分析）
    size_t get_occupied_entry_count() const;
    size_t get_ready_entry_count() const;
    
    // 检查表项是否准备好执行
    bool is_entry_ready(RSEntry rs_entry) const;
    
private:
    // 分配保留站表项
    RSEntry allocate_entry();
    
    // 初始化空闲列表
    void initialize_free_list();
    
    // 检查指令是否准备好执行
    bool is_instruction_ready(DynamicInstPtr instruction) const;
};

} // namespace riscv
