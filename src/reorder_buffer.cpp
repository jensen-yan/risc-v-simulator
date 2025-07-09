#include "reorder_buffer.h"
#include <iostream>
#include <iomanip>
#include <cassert>

namespace riscv {

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
    // 初始化所有ROB表项
    for (int i = 0; i < MAX_ROB_ENTRIES; ++i) {
        rob_entries[i].valid = false;
        rob_entries[i].state = ReorderBufferEntry::State::ALLOCATED;
        rob_entries[i].result_ready = false;
        rob_entries[i].has_exception = false;
        rob_entries[i].pc = 0;
        rob_entries[i].result = 0;
        rob_entries[i].logical_dest = 0;
        rob_entries[i].physical_dest = 0;
    }
    
    // 初始化空闲列表
    for (int i = 0; i < MAX_ROB_ENTRIES; ++i) {
        free_entries.push(i);
    }
    
    head_ptr = 0;
    tail_ptr = 0;
    entry_count = 0;
}

ReorderBuffer::AllocateResult ReorderBuffer::allocate_entry(
    const DecodedInstruction& instruction, uint32_t pc) {
    
    AllocateResult result;
    result.success = false;
    
    if (is_full()) {
        result.error_message = "ROB已满，无法分配表项";
        return result;
    }
    
    // 分配表项
    ROBEntry rob_entry = allocate_rob_entry();
    int index = entry_to_index(rob_entry);
    
    // 初始化表项
    rob_entries[index].instruction = instruction;
    rob_entries[index].pc = pc;
    rob_entries[index].valid = true;
    rob_entries[index].state = ReorderBufferEntry::State::ALLOCATED;
    rob_entries[index].result_ready = false;
    rob_entries[index].has_exception = false;
    rob_entries[index].result = 0;
    rob_entries[index].logical_dest = instruction.rd;
    rob_entries[index].physical_dest = 0;  // 需要从重命名单元获取
    
    result.success = true;
    result.rob_entry = rob_entry;
    allocated_count++;
    
    std::cout << "分配ROB表项 " << rob_entry << ", PC=0x" 
              << std::hex << pc << std::dec << std::endl;
    
    return result;
}

void ReorderBuffer::set_instruction_id(ROBEntry rob_entry, uint64_t instruction_id) {
    int index = entry_to_index(rob_entry);
    assert(is_valid_index(index) && rob_entries[index].valid && "ROB表项无效");
    
    rob_entries[index].instruction_id = instruction_id;
}

ROBEntry ReorderBuffer::get_dispatchable_entry() const {
    // 遍历ROB，找到第一条状态为ALLOCATED的指令
    for (int i = 0; i < MAX_ROB_ENTRIES; ++i) {
        int index = (head_ptr + i) % MAX_ROB_ENTRIES;
        if (rob_entries[index].valid && rob_entries[index].state == ReorderBufferEntry::State::ALLOCATED) {
            return index_to_entry(index);
        }
    }
    return 32;  // 没有可发射的指令
}

void ReorderBuffer::mark_as_dispatched(ROBEntry rob_entry) {
    int index = entry_to_index(rob_entry);
    assert(is_valid_index(index) && rob_entries[index].valid && "ROB表项无效");
    assert(rob_entries[index].state == ReorderBufferEntry::State::ALLOCATED && "指令状态错误");
    
    rob_entries[index].state = ReorderBufferEntry::State::ISSUED;
}

void ReorderBuffer::update_entry(ROBEntry rob_entry, uint32_t result, 
                                bool has_exception, const std::string& exception_msg) {
    
    int index = entry_to_index(rob_entry);
    assert(is_valid_index(index) && rob_entries[index].valid && "ROB表项无效");
    
    rob_entries[index].result = result;
    rob_entries[index].result_ready = true;
    rob_entries[index].has_exception = has_exception;
    rob_entries[index].exception_msg = exception_msg;
    rob_entries[index].state = ReorderBufferEntry::State::COMPLETED;
    
    if (has_exception) {
        exception_count++;
    }
}

ReorderBuffer::CommitResult ReorderBuffer::commit_instruction() {
    CommitResult result;
    result.success = false;
    result.has_more = false;
    
    if (is_empty()) {
        result.error_message = "ROB为空，无法提交";
        return result;
    }
    
    // 检查头部表项
    int head_index = head_ptr;
    ReorderBufferEntry& head_entry = rob_entries[head_index];
    
    if (!head_entry.valid) {
        result.error_message = "头部表项无效";
        return result;
    }
    
    // 检查是否可以提交
    if (head_entry.state != ReorderBufferEntry::State::COMPLETED) {
        result.error_message = "头部指令尚未完成";
        return result;
    }
    
    // 检查是否有异常
    if (head_entry.has_exception) {
        result.success = true;
        result.instruction = head_entry;
        result.error_message = "提交异常指令: " + head_entry.exception_msg;
        
        // 异常指令提交后需要刷新流水线
        std::cout << "提交异常指令 ROB" << index_to_entry(head_index) 
                  << ", PC=0x" << std::hex << head_entry.pc << std::dec 
                  << ", 异常: " << head_entry.exception_msg << std::endl;
        
        // 释放头部表项
        release_entry(index_to_entry(head_index));
        committed_count++;
        
        // 检查是否还有更多可提交的指令
        result.has_more = can_commit();
        return result;
    }
    
    // 正常提交
    result.success = true;
    result.instruction = head_entry;
    
    std::cout << "提交指令 ROB" << index_to_entry(head_index) 
              << ", PC=0x" << std::hex << head_entry.pc << std::dec
              << ", 结果=0x" << std::hex << head_entry.result << std::dec << std::endl;
    
    // 更新状态
    head_entry.state = ReorderBufferEntry::State::RETIRED;
    
    // 释放头部表项
    release_entry(index_to_entry(head_index));
    committed_count++;
    
    // 检查是否还有更多可提交的指令
    result.has_more = can_commit();
    
    return result;
}

bool ReorderBuffer::can_commit() const {
    if (is_empty()) return false;
    
    const ReorderBufferEntry& head_entry = rob_entries[head_ptr];
    return head_entry.valid && head_entry.state == ReorderBufferEntry::State::COMPLETED;
}

void ReorderBuffer::flush_pipeline() {
    std::cout << "刷新整个ROB，丢弃 " << entry_count << " 条指令" << std::endl;
    
    flushed_count += entry_count;
    
    // 清空所有表项
    for (int i = 0; i < MAX_ROB_ENTRIES; ++i) {
        rob_entries[i].valid = false;
    }
    
    // 重新初始化
    initialize_rob();
}

void ReorderBuffer::flush_after_entry(ROBEntry rob_entry) {
    int flush_index = entry_to_index(rob_entry);
    
    std::cout << "刷新ROB从表项 " << rob_entry << " 之后" << std::endl;
    
    // 从指定表项之后开始刷新
    int current = next_index(flush_index);
    int flushed = 0;
    
    while (current != tail_ptr && rob_entries[current].valid) {
        rob_entries[current].valid = false;
        free_entries.push(current);
        current = next_index(current);
        flushed++;
        entry_count--;
    }
    
    // 更新尾指针
    tail_ptr = next_index(flush_index);
    flushed_count += flushed;
    
    std::cout << "刷新了 " << flushed << " 条指令" << std::endl;
}

bool ReorderBuffer::has_free_entry() const {
    return !is_full();
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

const ReorderBufferEntry& ReorderBuffer::get_entry(ROBEntry rob_entry) const {
    int index = entry_to_index(rob_entry);
    assert(is_valid_index(index) && "ROB表项索引无效");
    return rob_entries[index];
}

bool ReorderBuffer::is_entry_valid(ROBEntry rob_entry) const {
    int index = entry_to_index(rob_entry);
    return is_valid_index(index) && rob_entries[index].valid;
}

ROBEntry ReorderBuffer::get_head_entry() const {
    return is_empty() ? MAX_ROB_ENTRIES : index_to_entry(head_ptr);
}

ROBEntry ReorderBuffer::get_tail_entry() const {
    return is_empty() ? MAX_ROB_ENTRIES : index_to_entry(prev_index(tail_ptr));
}

void ReorderBuffer::get_statistics(uint64_t& allocated, uint64_t& committed, 
                                  uint64_t& flushed, uint64_t& exceptions) const {
    allocated = allocated_count;
    committed = committed_count;
    flushed = flushed_count;
    exceptions = exception_count;
}

bool ReorderBuffer::has_pending_exception() const {
    // 检查从头部到尾部是否有异常
    int current = head_ptr;
    for (int i = 0; i < entry_count; ++i) {
        if (rob_entries[current].valid && rob_entries[current].has_exception) {
            return true;
        }
        current = next_index(current);
    }
    return false;
}

ReorderBuffer::ExceptionInfo ReorderBuffer::get_oldest_exception() const {
    ExceptionInfo info;
    info.has_exception = false;
    
    // 从头部开始查找最老的异常
    int current = head_ptr;
    for (int i = 0; i < entry_count; ++i) {
        if (rob_entries[current].valid && rob_entries[current].has_exception) {
            info.has_exception = true;
            info.rob_entry = index_to_entry(current);
            info.exception_message = rob_entries[current].exception_msg;
            info.pc = rob_entries[current].pc;
            break;
        }
        current = next_index(current);
    }
    
    return info;
}

ROBEntry ReorderBuffer::allocate_rob_entry() {
    assert(!is_full() && "ROB已满");
    
    ROBEntry entry = index_to_entry(tail_ptr);
    tail_ptr = next_index(tail_ptr);
    entry_count++;
    
    return entry;
}

void ReorderBuffer::release_entry(ROBEntry rob_entry) {
    int index = entry_to_index(rob_entry);
    assert(is_valid_index(index) && rob_entries[index].valid && "释放无效的ROB表项");
    
    rob_entries[index].valid = false;
    
    // 如果是头部表项，更新头指针
    if (index == head_ptr) {
        head_ptr = next_index(head_ptr);
        entry_count--;
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

void ReorderBuffer::dump_reorder_buffer() const {
    std::cout << "\n=== ROB状态 ===" << std::endl;
    std::cout << "容量: " << entry_count << "/" << MAX_ROB_ENTRIES << std::endl;
    std::cout << "头指针: " << head_ptr << ", 尾指针: " << tail_ptr << std::endl;
    
    if (is_empty()) {
        std::cout << "ROB为空" << std::endl;
        return;
    }
    
    std::cout << "\n有效表项:" << std::endl;
    std::cout << "ROB PC     指令  状态    结果      异常" << std::endl;
    std::cout << "--- ------ ----- ------- --------- ----" << std::endl;
    
    int current = head_ptr;
    for (int i = 0; i < entry_count; ++i) {
        if (rob_entries[current].valid) {
            const auto& entry = rob_entries[current];
            std::cout << std::setw(2) << current << "  "
                      << "0x" << std::hex << std::setw(4) << entry.pc << std::dec << "  "
                      << std::setw(4) << (int)entry.instruction.type << "  ";
            
            // 状态
            switch (entry.state) {
                case ReorderBufferEntry::State::ALLOCATED: std::cout << "已分配"; break;
                case ReorderBufferEntry::State::ISSUED: std::cout << "已发射"; break;
                case ReorderBufferEntry::State::EXECUTING: std::cout << "执行中"; break;
                case ReorderBufferEntry::State::COMPLETED: std::cout << "已完成"; break;
                case ReorderBufferEntry::State::RETIRED: std::cout << "已退休"; break;
            }
            std::cout << "  ";
            
            // 结果
            if (entry.result_ready) {
                std::cout << "0x" << std::hex << std::setw(8) << entry.result << std::dec;
            } else {
                std::cout << "   等待   ";
            }
            std::cout << "  ";
            
            // 异常
            if (entry.has_exception) {
                std::cout << "是";
            } else {
                std::cout << "否";
            }
            
            std::cout << std::endl;
        }
        current = next_index(current);
    }
}

void ReorderBuffer::dump_rob_summary() const {
    std::cout << "\n=== ROB统计信息 ===" << std::endl;
    std::cout << "已分配: " << allocated_count << std::endl;
    std::cout << "已提交: " << committed_count << std::endl; 
    std::cout << "已刷新: " << flushed_count << std::endl;
    std::cout << "异常数: " << exception_count << std::endl;
    std::cout << "当前占用: " << entry_count << "/" << MAX_ROB_ENTRIES << std::endl;
}

} // namespace riscv