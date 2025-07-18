#include "cpu/ooo/ooo_types.h"
#include "common/debug_types.h"
#include "common/types.h"
#include <iostream>
#include <iomanip>

namespace riscv {

void StoreBuffer::add_store(uint32_t address, uint32_t value, uint8_t size, uint64_t instruction_id, uint32_t pc) {
    // 找到下一个可用的条目（可能覆盖旧条目）
    StoreBufferEntry& entry = entries[next_allocate_index];
    
    entry.valid = true;
    entry.address = address;
    entry.value = value;
    entry.size = size;
    entry.instruction_id = instruction_id;
    entry.pc = pc;
    
    dprintf(EXECUTE, "Store Buffer添加条目[%d]: 地址=0x%x, 值=0x%x, 大小=%d, Inst#%llu, PC=0x%x", 
            next_allocate_index, address, value, size, instruction_id, pc);
    
    // 移动到下一个分配位置（循环）
    next_allocate_index = (next_allocate_index + 1) % MAX_ENTRIES;
}

bool StoreBuffer::forward_load(uint32_t address, uint8_t size, uint32_t& result_value) const {
    // 从最新的Store开始向前搜索（最近的Store优先）
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        // 从最近分配的位置开始向前搜索
        int index = (next_allocate_index - 1 - i + MAX_ENTRIES) % MAX_ENTRIES;
        const StoreBufferEntry& entry = entries[index];
        
        if (!entry.valid) continue;
        
        // 检查地址是否有重叠
        if (addresses_overlap(entry.address, entry.size, address, size)) {
            // 如果完全匹配，可以直接转发
            if (entry.address == address && entry.size == size) {
                result_value = entry.value;
                dprintf(EXECUTE, "Store-to-Load Forwarding: 完全匹配 地址=0x%x, 大小=%d, 转发值=0x%x (来自Inst#%llu)", 
                        address, size, result_value, entry.instruction_id);
                return true;
            }
            
            // 部分重叠的情况 - 需要提取正确的数据
            if (can_extract_load_data(entry, address, size)) {
                result_value = extract_load_data(entry, address, size);
                dprintf(EXECUTE, "Store-to-Load Forwarding: 部分匹配 Load地址=0x%x, Load大小=%d, Store地址=0x%x, Store大小=%d, 转发值=0x%x (来自Inst#%llu)", 
                        address, size, entry.address, entry.size, result_value, entry.instruction_id);
                return true;
            } else {
                // 有重叠但无法转发（如部分字节写入）- 这种情况下Load必须等待Store提交到内存
                dprintf(EXECUTE, "Store-to-Load Forwarding: 地址重叠但无法转发 Load地址=0x%x, Load大小=%d, Store地址=0x%x, Store大小=%d (来自Inst#%llu)", 
                        address, size, entry.address, entry.size, entry.instruction_id);
                return false; // 无法转发，Load需要等待
            }
        }
    }
    
    // 没有找到匹配的Store，Load可以直接从内存读取
    dprintf(EXECUTE, "Store-to-Load Forwarding: 没有找到匹配的Store，地址=0x%x, 大小=%d", address, size);
    return false; // 表示没有匹配，不是转发失败
}

void StoreBuffer::retire_stores_before(uint64_t instruction_id) {
    int retired_count = 0;
    
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (entries[i].valid && entries[i].instruction_id <= instruction_id) {
            dprintf(EXECUTE, "Store Buffer退休条目[%d]: Inst#%llu, 地址=0x%x", 
                    i, entries[i].instruction_id, entries[i].address);
            entries[i].valid = false;
            retired_count++;
        }
    }
    
    if (retired_count > 0) {
        dprintf(EXECUTE, "Store Buffer退休了%d个条目，指令ID <= %llu", retired_count, instruction_id);
    }
}

void StoreBuffer::flush() {
    dprintf(EXECUTE, "Store Buffer刷新：清空所有条目");
    
    for (auto& entry : entries) {
        entry.valid = false;
    }
    next_allocate_index = 0;
}

void StoreBuffer::dump() const {
    dprintf(EXECUTE, "Store Buffer状态:");
    dprintf(EXECUTE, "分配索引: %d", next_allocate_index);
    
    bool has_valid = false;
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (entries[i].valid) {
            dprintf(EXECUTE, "  [%d] 地址=0x%x, 值=0x%x, 大小=%d, Inst#%llu, PC=0x%x", 
                    i, entries[i].address, entries[i].value, entries[i].size, 
                    entries[i].instruction_id, entries[i].pc);
            has_valid = true;
        }
    }
    
    if (!has_valid) {
        dprintf(EXECUTE, "  (空)");
    }
}

bool StoreBuffer::addresses_overlap(uint32_t addr1, uint8_t size1, uint32_t addr2, uint8_t size2) const {
    uint32_t end1 = addr1 + size1 - 1;
    uint32_t end2 = addr2 + size2 - 1;
    
    // 两个内存区域有重叠当且仅当：
    // addr1 <= end2 && addr2 <= end1
    return (addr1 <= end2) && (addr2 <= end1);
}

bool StoreBuffer::can_extract_load_data(const StoreBufferEntry& store_entry, uint32_t load_addr, uint8_t load_size) const {
    // 检查Load访问是否完全在Store的范围内
    uint32_t store_end = store_entry.address + store_entry.size - 1;
    uint32_t load_end = load_addr + load_size - 1;
    
    return (load_addr >= store_entry.address) && (load_end <= store_end);
}

uint32_t StoreBuffer::extract_load_data(const StoreBufferEntry& store_entry, uint32_t load_addr, uint8_t load_size) const {
    // 计算Load在Store数据中的偏移
    uint32_t offset = load_addr - store_entry.address;
    
    // 根据偏移和大小提取数据
    uint32_t result = 0;
    
    switch (load_size) {
        case 1: // 字节访问
            result = (store_entry.value >> (offset * 8)) & 0xFF;
            break;
            
        case 2: // 半字访问
            if (offset % 2 != 0) {
                // 非对齐访问，暂时不支持
                return 0;
            }
            result = (store_entry.value >> (offset * 8)) & 0xFFFF;
            break;
            
        case 4: // 字访问
            if (offset != 0) {
                // 字访问必须完全对齐
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