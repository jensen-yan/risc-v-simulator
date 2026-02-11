#include "cpu/ooo/store_buffer.h"
#include "common/debug_types.h"
#include "common/types.h"

namespace riscv {

StoreBuffer::StoreBuffer() : next_allocate_index(0) {
    for (auto& entry : entries) {
        entry.valid = false;
    }
}

void StoreBuffer::add_store(DynamicInstPtr instruction, uint64_t address, uint64_t value, uint8_t size) {
    // 找到下一个可用的条目（可能覆盖旧条目）
    StoreBufferEntry& entry = entries[next_allocate_index];
    
    entry.valid = true;
    entry.instruction = instruction;
    entry.address = address;
    entry.value = value;
    entry.size = size;
    
    LOGT(EXECUTE, "store buffer add[%d]: addr=0x%" PRIx64 ", value=0x%" PRIx64 ", size=%d, inst=%" PRId64 ", pc=0x%" PRIx64,
            next_allocate_index, address, value, size, instruction->get_instruction_id(), instruction->get_pc());
    
    // 移动到下一个分配位置（循环）
    next_allocate_index = (next_allocate_index + 1) % MAX_ENTRIES;
}

bool StoreBuffer::forward_load(uint64_t address, uint8_t size, uint64_t& result_value) const {
    bool blocked = false;
    return forward_load(address, size, result_value, std::numeric_limits<uint64_t>::max(), blocked);
}

bool StoreBuffer::forward_load(uint64_t address, uint8_t size, uint64_t& result_value,
                               uint64_t current_instruction_id, bool& blocked) const {
    blocked = false;
    // 从最新的Store开始向前搜索（最近的Store优先）
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        // 从最近分配的位置开始向前搜索
        int index = (next_allocate_index - 1 - i + MAX_ENTRIES) % MAX_ENTRIES;
        const StoreBufferEntry& entry = entries[index];
        
        if (!entry.valid) continue;
        if (!entry.instruction) continue;
        if (entry.instruction->get_instruction_id() >= current_instruction_id) {
            // 仅允许从更老的Store转发
            continue;
        }
        
        // 检查地址是否有重叠
        if (addresses_overlap(entry.address, entry.size, address, size)) {
            // 如果完全匹配，可以直接转发
            if (entry.address == address && entry.size == size) {
                result_value = entry.value;
                LOGT(EXECUTE, "store-to-load forwarding full match: addr=0x%" PRIx64 ", size=%d, value=0x%" PRIx64 " (inst=%" PRId64 ")",
                        address, size, result_value, entry.instruction->get_instruction_id());
                return true;
            }
            
            // 部分重叠的情况 - 需要提取正确的数据
            if (can_extract_load_data(entry, address, size)) {
                result_value = extract_load_data(entry, address, size);
                LOGT(EXECUTE, "store-to-load forwarding partial match: load_addr=0x%" PRIx64 ", load_size=%d, store_addr=0x%" PRIx64 ", store_size=%d, value=0x%" PRIx64 " (inst=%" PRId64 ")",
                        address, size, entry.address, entry.size, result_value, entry.instruction->get_instruction_id());
                return true;
            } else {
                // 有重叠但无法转发（如部分字节写入）- 这种情况下Load必须等待Store提交到内存
                LOGT(EXECUTE, "store-to-load overlap but cannot forward: load_addr=0x%" PRIx64 ", load_size=%d, store_addr=0x%" PRIx64 ", store_size=%d (inst=%" PRId64 ")",
                        address, size, entry.address, entry.size, entry.instruction->get_instruction_id());
                blocked = true;
                return false; // 无法转发，Load需要等待
            }
        }
    }
    
    // 没有找到匹配的Store，Load可以直接从内存读取
    LOGT(EXECUTE, "store-to-load no matching store: addr=0x%" PRIx64 ", size=%d", address, size);
    return false; // 表示没有匹配，不是转发失败
}

void StoreBuffer::retire_stores_before(uint64_t instruction_id) {
    int retired_count = 0;
    
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (entries[i].valid && entries[i].instruction && entries[i].instruction->get_instruction_id() <= instruction_id) {
            LOGT(EXECUTE, "store buffer retire[%d]: inst=%" PRId64 ", addr=0x%" PRIx64,
                    i, entries[i].instruction->get_instruction_id(), entries[i].address);
            entries[i].valid = false;
            entries[i].instruction = nullptr; // 清除指令指针
            retired_count++;
        }
    }
    
    if (retired_count > 0) {
        LOGT(EXECUTE, "store buffer retired %d entries, instruction_id <= %" PRId64, retired_count, instruction_id);
    }
}

void StoreBuffer::flush() {
    LOGT(EXECUTE, "store buffer flush: clear all entries");
    
    for (auto& entry : entries) {
        entry.valid = false;
        entry.instruction = nullptr; // 清除指令指针
    }
    next_allocate_index = 0;
}

void StoreBuffer::dump() const {
    LOGT(EXECUTE, "store buffer state");
    LOGT(EXECUTE, "next allocation index: %d", next_allocate_index);
    
    bool has_valid = false;
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (entries[i].valid && entries[i].instruction) {
            LOGT(EXECUTE, "  [%d] addr=0x%" PRIx64 ", value=0x%" PRIx64 ", size=%d, inst=%" PRId64 ", pc=0x%" PRIx64,
                    i, entries[i].address, entries[i].value, entries[i].size, 
                    entries[i].instruction->get_instruction_id(), entries[i].instruction->get_pc());
            has_valid = true;
        }
    }
    
    if (!has_valid) {
        LOGT(EXECUTE, "  (empty)");
    }
}

bool StoreBuffer::addresses_overlap(uint64_t addr1, uint8_t size1, uint64_t addr2, uint8_t size2) const {
    uint64_t end1 = addr1 + size1 - 1;
    uint64_t end2 = addr2 + size2 - 1;
    
    // 两个内存区域有重叠当且仅当：
    // addr1 <= end2 && addr2 <= end1
    return (addr1 <= end2) && (addr2 <= end1);
}

bool StoreBuffer::can_extract_load_data(const StoreBufferEntry& store_entry, uint64_t load_addr, uint8_t load_size) const {
    // 检查Load访问是否完全在Store的范围内
    uint64_t store_end = store_entry.address + store_entry.size - 1;
    uint64_t load_end = load_addr + load_size - 1;
    
    return (load_addr >= store_entry.address) && (load_end <= store_end);
}

uint64_t StoreBuffer::extract_load_data(const StoreBufferEntry& store_entry, uint64_t load_addr, uint8_t load_size) const {
    // 计算Load在Store数据中的偏移
    uint64_t offset = load_addr - store_entry.address;
    
    // 根据偏移和大小提取数据
    uint64_t result = 0;
    
    switch (load_size) {
        case 1: // 字节访问
            result = (store_entry.value >> (offset * 8)) & 0xFF;
            break;
            
        case 2: // 半字访问
            // 从任何偏移都可以提取2字节数据，不需要对齐限制
            result = (store_entry.value >> (offset * 8)) & 0xFFFF;
            break;
            
        case 4: // 字访问
            // 从任何偏移都可以提取4字节数据，不需要对齐限制
            result = (store_entry.value >> (offset * 8)) & 0xFFFFFFFF;
            break;
            
        case 8: // 双字访问 (64位)
            if (offset != 0) {
                // 双字访问必须完全对齐
                return 0;
            }
            result = store_entry.value;
            break;
            
        default:
            result = 0;
            break;
    }
    
    return result;
}

} // namespace riscv 
