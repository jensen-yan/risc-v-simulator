#pragma once

#include "cpu/ooo/ooo_types.h"
#include "cpu/ooo/dynamic_inst.h"
#include <vector>
#include <queue>
#include <deque>

namespace riscv {

// 注意：原来的 ReorderBufferEntry 已被 DynamicInst 替代
// 保留这个别名以便于向后兼容和渐进式迁移
using ReorderBufferEntry = DynamicInst;

/**
 * 重排序缓冲单元(Reorder Buffer)
 * 
 * 功能：
 * 1. 维护程序顺序，确保指令按顺序提交
 * 2. 支持精确异常处理
 * 3. 管理投机执行状态
 * 4. 处理分支预测错误恢复
 * 5. 跟踪指令执行状态和结果
 */
class ReorderBuffer {
public:
    // 配置参数
    static const int MAX_ROB_ENTRIES = 32;       // ROB最大容量

private:
    // ROB表项存储（使用循环队列）
    std::vector<DynamicInstPtr> rob_entries;
    
    // 循环队列管理
    int head_ptr;           // 头指针（最老的未提交指令）
    int tail_ptr;           // 尾指针（下一个分配位置）
    int entry_count;        // 当前表项数量
    
    // 空闲表项队列
    std::queue<ROBEntry> free_entries;
    
    // 统计信息
    uint64_t allocated_count;
    uint64_t committed_count;
    uint64_t flushed_count;
    uint64_t exception_count;
    
public:
    ReorderBuffer();
    
    // 分配操作结果
    struct AllocateResult {
        bool success;
        ROBEntry rob_entry;
        std::string error_message;
    };
    
    // 提交操作结果
    struct CommitResult {
        bool success;
        bool has_more;          // 是否还有更多可提交的指令
        DynamicInstPtr instruction;
        std::string error_message;
    };
    
    // 分配ROB表项（返回DynamicInst指针）
    DynamicInstPtr allocate_entry(const DecodedInstruction& instruction, uint64_t pc, uint64_t instruction_id);
    
    // 更新ROB表项执行结果（通过DynamicInst指针）
    void update_entry(DynamicInstPtr inst, uint64_t result, bool has_exception = false, 
                     const std::string& exception_msg = "", bool is_jump = false, 
                     uint64_t jump_target = 0);
    
    // 通过ROB索引更新（兼容性接口）
    void update_entry_by_index(ROBEntry rob_entry, uint64_t result, bool has_exception = false, 
                              const std::string& exception_msg = "", bool is_jump = false, 
                              uint64_t jump_target = 0);
    
    // 获取可以发射的指令
    DynamicInstPtr get_dispatchable_entry() const;
    
    // 标记指令为已发射到保留站
    void mark_as_dispatched(DynamicInstPtr inst);
    
    // 尝试提交一条指令
    CommitResult commit_instruction();
    
    // 检查是否可以提交
    bool can_commit() const;
    
    // 刷新ROB（分支预测错误时）
    void flush_pipeline();
    
    // 刷新ROB到指定表项（部分刷新）
    void flush_after_entry(ROBEntry rob_entry);
    
    // 检查是否有空闲表项
    bool has_free_entry() const;
    
    // 获取空闲表项数量
    size_t get_free_entry_count() const;
    
    // 检查ROB是否为空
    bool is_empty() const;
    
    // 检查ROB是否已满
    bool is_full() const;
    
    // 获取ROB表项
    DynamicInstPtr get_entry(ROBEntry rob_entry) const;
    
    // 检查表项是否有效
    bool is_entry_valid(ROBEntry rob_entry) const;
    
    // 获取头部表项（最老的未提交指令）
    ROBEntry get_head_entry() const;
    
    // 获取尾部表项（最新分配的指令）
    ROBEntry get_tail_entry() const;
    
    // 获取统计信息
    void get_statistics(uint64_t& allocated, uint64_t& committed, 
                       uint64_t& flushed, uint64_t& exceptions) const;
    
    // 调试输出
    void dump_reorder_buffer() const;
    void dump_rob_summary() const;
    
    // 检查是否有异常待处理
    bool has_pending_exception() const;
    
    // 获取最老的异常信息
    struct ExceptionInfo {
        bool has_exception;
        DynamicInstPtr instruction;
        std::string exception_message;
        uint64_t pc;
    };
    ExceptionInfo get_oldest_exception() const;
    
    // 检查是否有更早的未完成Store指令（用于Load-Store依赖检查）
    bool has_earlier_store_pending(uint64_t current_instruction_id) const;
    
private:
    // 分配ROB表项ID
    ROBEntry allocate_rob_entry();
    
    // 释放ROB表项
    void release_entry(ROBEntry rob_entry);
    
    // 循环队列索引递增
    int next_index(int index) const;
    
    // 循环队列索引递减
    int prev_index(int index) const;
    
    // 检查索引是否有效
    bool is_valid_index(int index) const;
    
    // 将ROB索引转换为ROBEntry
    ROBEntry index_to_entry(int index) const;
    
    // 将ROBEntry转换为ROB索引
    int entry_to_index(ROBEntry rob_entry) const;
    
    // 初始化ROB
    void initialize_rob();
};

} // namespace riscv