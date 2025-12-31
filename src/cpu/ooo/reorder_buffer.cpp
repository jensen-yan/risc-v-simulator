#include "cpu/ooo/reorder_buffer.h"
#include "common/debug_types.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cassert>

namespace riscv {

// 定义静态常量
const int ReorderBuffer::MAX_ROB_ENTRIES;

ReorderBuffer::ReorderBuffer() 
    : rob_entries(MAX_ROB_ENTRIES),
      head_ptr(0),
      tail_ptr(0), 
      entry_count(0),
      allocated_count(0),
      committed_count(0),
      flushed_count(0),
      exception_count(0) {
    
    initialize_rob();
}

void ReorderBuffer::initialize_rob() {
    // 初始化所有ROB表项为空指针
    for (int i = 0; i < MAX_ROB_ENTRIES; ++i) {
        rob_entries[i] = nullptr;
    }
    
    // 清空现有的空闲列表（这很重要！）
    while (!free_entries.empty()) {
        free_entries.pop();
    }
    
    // 重新初始化空闲列表
    for (int i = 0; i < MAX_ROB_ENTRIES; ++i) {
        free_entries.push(i);
    }
    
    head_ptr = 0;
    tail_ptr = 0;
    entry_count = 0;
}

DynamicInstPtr ReorderBuffer::allocate_entry(
    const DecodedInstruction& instruction, uint64_t pc, uint64_t instruction_id) {
    
    if (is_full()) {
        dprintf(ROB, "ROB已满，无法分配表项");
        return nullptr;
    }
    
    // 分配表项
    ROBEntry rob_entry = allocate_rob_entry();
    int index = entry_to_index(rob_entry);
    
    // 创建新的DynamicInst对象
    DynamicInstPtr dynamic_inst = create_dynamic_inst(instruction, pc, instruction_id);
    dynamic_inst->set_rob_entry(rob_entry);
    dynamic_inst->set_status(DynamicInst::Status::ALLOCATED);
    
    // 将指令存储在ROB中
    rob_entries[index] = dynamic_inst;
    allocated_count++;
    
    // 使用新的dprintf宏 - 类似GEM5风格
    dprintf(ROB, "分配ROB表项 %d, Inst#%" PRId64 ", PC=0x%" PRIx64,
        rob_entry, instruction_id, pc);
    
    return dynamic_inst;
}

void ReorderBuffer::update_entry(DynamicInstPtr inst, uint64_t result, bool has_exception, 
                                const std::string& exception_msg, bool is_jump, 
                                uint64_t jump_target) {
    if (!inst) {
        dprintf(ROB, "无效的DynamicInst指针");
        return;
    }
    
    // 更新指令结果
    inst->set_result(result);
    
    // 更新异常信息
    if (has_exception) {
        inst->set_exception(exception_msg);
        exception_count++;
    }
    
    // 更新跳转信息
    inst->set_jump_info(is_jump, jump_target);
    
    // 标记为执行完成
    inst->set_status(DynamicInst::Status::COMPLETED);
    
    dprintf(ROB, "更新ROB表项 %d 执行结果: 0x%" PRIx64, inst->get_rob_entry(), result);
}

void ReorderBuffer::update_entry_by_index(ROBEntry rob_entry, uint64_t result, bool has_exception, 
                                         const std::string& exception_msg, bool is_jump, 
                                         uint64_t jump_target) {
    DynamicInstPtr inst = get_entry(rob_entry);
    if (inst) {
        update_entry(inst, result, has_exception, exception_msg, is_jump, jump_target);
    }
}

DynamicInstPtr ReorderBuffer::get_dispatchable_entry() const {
    // 遍历ROB，找到第一条状态为ALLOCATED的指令
    for (int i = 0; i < entry_count; ++i) {
        int index = (head_ptr + i) % MAX_ROB_ENTRIES;
        if (rob_entries[index] && rob_entries[index]->is_allocated()) {
            return rob_entries[index];
        }
    }
    return nullptr;
}

void ReorderBuffer::mark_as_dispatched(DynamicInstPtr inst) {
    if (!inst) return;
    
    inst->set_status(DynamicInst::Status::ISSUED);
    dprintf(ROB, "标记指令 %" PRId64 " 已发射到保留站", inst->get_instruction_id());
}

ReorderBuffer::CommitResult ReorderBuffer::commit_instruction() {
    CommitResult result;
    result.success = false;
    result.has_more = false;
    
    if (is_empty()) {
        result.error_message = "ROB为空，没有指令可提交";
        return result;
    }
    
    // 检查头部指令是否可以提交
    int head_index = head_ptr;
    DynamicInstPtr head_inst = rob_entries[head_index];
    
    if (!head_inst) {
        result.error_message = "头部指令为空";
        return result;
    }
    
    if (!head_inst->is_completed()) {
        result.error_message = "头部指令尚未完成执行";
        return result;
    }
    
    // 检查是否有异常
    if (head_inst->has_exception()) {
        result.success = true;
        result.instruction = head_inst;
        result.error_message = head_inst->get_exception_message();
        
        // 异常指令也需要提交（用于异常处理）
        head_inst->set_status(DynamicInst::Status::RETIRED);
        committed_count++;
        
        // 释放表项
        rob_entries[head_index] = nullptr;
        free_entries.push(head_index);
        head_ptr = next_index(head_ptr);
        entry_count--;
        
        dprintf(ROB, "提交异常指令 %" PRId64 ", PC=0x%" PRIx64,
            head_inst->get_instruction_id(), head_inst->get_pc());
        
        return result;
    }
    
    // 正常指令提交
    head_inst->set_status(DynamicInst::Status::RETIRED);
    committed_count++;
    
    result.success = true;
    result.instruction = head_inst;
    result.has_more = (entry_count > 1);
    
    // 释放表项
    rob_entries[head_index] = nullptr;
    free_entries.push(head_index);
    head_ptr = next_index(head_ptr);
    entry_count--;
    
    dprintf(ROB, "提交指令 %" PRId64 ", PC=0x%" PRIx64 ", 结果=0x%" PRIx64,
        head_inst->get_instruction_id(), head_inst->get_pc(), head_inst->get_result());
    
    return result;
}

bool ReorderBuffer::can_commit() const {
    if (is_empty()) return false;
    
    DynamicInstPtr head_inst = rob_entries[head_ptr];
    return head_inst && head_inst->is_completed();
}

void ReorderBuffer::flush_pipeline() {
    dprintf(ROB, "刷新整个ROB流水线");
    
    flushed_count += entry_count;
    
    // 清空所有表项
    for (int i = 0; i < MAX_ROB_ENTRIES; ++i) {
        rob_entries[i] = nullptr;
    }
    
    // 重新初始化
    initialize_rob();
}

void ReorderBuffer::flush_after_entry(ROBEntry rob_entry) {
    int target_index = entry_to_index(rob_entry);
    int flushed = 0;
    
    // 从目标位置后的第一个位置开始刷新到尾部
    int start_index = next_index(target_index);
    
    while (start_index != tail_ptr) {
        if (rob_entries[start_index]) {
            rob_entries[start_index] = nullptr;
            free_entries.push(start_index);
            flushed++;
        }
        start_index = next_index(start_index);
    }
    
    // 更新尾指针和计数
    tail_ptr = next_index(target_index);
    entry_count -= flushed;
    flushed_count += flushed;
    
    dprintf(ROB, "部分刷新ROB，从表项 %d 之后刷新了 %d 条指令", rob_entry, flushed);
}

bool ReorderBuffer::has_free_entry() const {
    return entry_count < MAX_ROB_ENTRIES;
}

size_t ReorderBuffer::get_free_entry_count() const {
    return MAX_ROB_ENTRIES - entry_count;
}

bool ReorderBuffer::is_empty() const {
    return entry_count == 0;
}

bool ReorderBuffer::is_full() const {
    return entry_count >= MAX_ROB_ENTRIES;
}

DynamicInstPtr ReorderBuffer::get_entry(ROBEntry rob_entry) const {
    int index = entry_to_index(rob_entry);
    if (!is_valid_index(index)) {
        return nullptr;
    }
    return rob_entries[index];
}

bool ReorderBuffer::is_entry_valid(ROBEntry rob_entry) const {
    int index = entry_to_index(rob_entry);
    return is_valid_index(index) && rob_entries[index] != nullptr;
}

ROBEntry ReorderBuffer::get_head_entry() const {
    if (is_empty()) return 0;
    return index_to_entry(head_ptr);
}

ROBEntry ReorderBuffer::get_tail_entry() const {
    if (is_empty()) return 0;
    return index_to_entry(prev_index(tail_ptr));
}

void ReorderBuffer::get_statistics(uint64_t& allocated, uint64_t& committed, 
                                  uint64_t& flushed, uint64_t& exceptions) const {
    allocated = allocated_count;
    committed = committed_count;
    flushed = flushed_count;
    exceptions = exception_count;
}

void ReorderBuffer::dump_reorder_buffer() const {
    std::cout << "=== ROB状态转储 ===" << std::endl;
    std::cout << "头指针: " << head_ptr << ", 尾指针: " << tail_ptr 
              << ", 表项数: " << entry_count << std::endl;
    
    for (int i = 0; i < MAX_ROB_ENTRIES; ++i) {
        if (rob_entries[i]) {
            std::cout << "[" << std::setw(2) << i << "] " 
                     << rob_entries[i]->to_string() << std::endl;
        }
    }
    std::cout << "=================" << std::endl;
}

void ReorderBuffer::dump_rob_summary() const {
    std::cout << "ROB统计: 分配=" << allocated_count 
              << ", 提交=" << committed_count 
              << ", 刷新=" << flushed_count 
              << ", 异常=" << exception_count 
              << ", 当前=" << entry_count << "/" << MAX_ROB_ENTRIES << std::endl;
}

bool ReorderBuffer::has_pending_exception() const {
    for (int i = 0; i < entry_count; ++i) {
        int index = (head_ptr + i) % MAX_ROB_ENTRIES;
        if (rob_entries[index] && rob_entries[index]->has_exception()) {
            return true;
        }
    }
    return false;
}

ReorderBuffer::ExceptionInfo ReorderBuffer::get_oldest_exception() const {
    ExceptionInfo info;
    info.has_exception = false;
    
    for (int i = 0; i < entry_count; ++i) {
        int index = (head_ptr + i) % MAX_ROB_ENTRIES;
        if (rob_entries[index] && rob_entries[index]->has_exception()) {
            info.has_exception = true;
            info.instruction = rob_entries[index];
            info.exception_message = rob_entries[index]->get_exception_message();
            info.pc = rob_entries[index]->get_pc();
            break;
        }
    }
    
    return info;
}

bool ReorderBuffer::has_earlier_store_pending(uint64_t current_instruction_id) const {
    for (int i = 0; i < entry_count; ++i) {
        int index = (head_ptr + i) % MAX_ROB_ENTRIES;
        if (rob_entries[index]) {
            DynamicInstPtr inst = rob_entries[index];
            if (inst->get_instruction_id() >= current_instruction_id) {
                break;  // 已经检查到当前指令或之后的指令
            }
            if (inst->is_store_instruction() && !inst->is_completed()) {
                return true;
            }
        }
    }
    return false;
}

// ========== 私有方法实现 ==========
ROBEntry ReorderBuffer::allocate_rob_entry() {
    if (free_entries.empty()) {
        return 0;  // 错误情况，调用者应该先检查 is_full()
    }
    
    ROBEntry entry = free_entries.front();
    free_entries.pop();
    
    dprintf(ROB, "[ALLOC_DEBUG] 分配表项%d, 头部指针=%d, 尾部指针=%d->%d", 
            entry, head_ptr, tail_ptr, next_index(tail_ptr));
    
    tail_ptr = next_index(tail_ptr);
    entry_count++;
    
    return entry;
}

void ReorderBuffer::release_entry(ROBEntry rob_entry) {
    int index = entry_to_index(rob_entry);
    if (is_valid_index(index)) {
        rob_entries[index] = nullptr;
        free_entries.push(rob_entry);
    }
}

int ReorderBuffer::next_index(int index) const {
    return (index + 1) % MAX_ROB_ENTRIES;
}

int ReorderBuffer::prev_index(int index) const {
    return (index - 1 + MAX_ROB_ENTRIES) % MAX_ROB_ENTRIES;
}

bool ReorderBuffer::is_valid_index(int index) const {
    return index >= 0 && index < MAX_ROB_ENTRIES;
}

ROBEntry ReorderBuffer::index_to_entry(int index) const {
    return static_cast<ROBEntry>(index);
}

int ReorderBuffer::entry_to_index(ROBEntry rob_entry) const {
    return static_cast<int>(rob_entry);
}

} // namespace riscv
