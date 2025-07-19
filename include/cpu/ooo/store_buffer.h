#pragma once

#include "common/types.h"
#include <array>
#include <cstdint>
#include <iostream>

namespace riscv {

// Store Buffer 表项，用于 Store-to-Load Forwarding
struct StoreBufferEntry {
    bool valid;              // 表项是否有效
    uint32_t address;        // 存储地址
    uint32_t value;          // 存储值
    uint8_t size;            // 存储大小（1=字节, 2=半字, 4=字）
    uint64_t instruction_id; // 指令ID，用于调试
    uint32_t pc;            // 指令PC，用于调试
    
    StoreBufferEntry() : valid(false), address(0), value(0), size(0), instruction_id(0), pc(0) {}
};

// Store Buffer，用于实现 Store-to-Load Forwarding
class StoreBuffer {
private:
    static const int MAX_ENTRIES = 8; // Store Buffer大小
    std::array<StoreBufferEntry, MAX_ENTRIES> entries;
    int next_allocate_index; // 下一个分配位置（循环使用）

public:
    StoreBuffer();
    
    // 添加Store条目
    void add_store(uint32_t address, uint32_t value, uint8_t size, uint64_t instruction_id, uint32_t pc);
    
    // 查找匹配的Store (返回是否找到，如果找到则通过result_value返回值)
    bool forward_load(uint32_t address, uint8_t size, uint32_t& result_value) const;
    
    // 清除指定指令ID及之前的Store条目（当Store指令提交时调用）
    void retire_stores_before(uint64_t instruction_id);
    
    // 清空所有条目（流水线刷新时调用）
    void flush();
    
    // 调试：打印Store Buffer状态
    void dump() const;
    
private:
    // 检查两个内存访问是否有重叠
    bool addresses_overlap(uint32_t addr1, uint8_t size1, uint32_t addr2, uint8_t size2) const;
    
    // 检查是否可以从Store条目中提取Load数据
    bool can_extract_load_data(const StoreBufferEntry& store_entry, uint32_t load_addr, uint8_t load_size) const;
    
    // 从Store Buffer条目中提取Load需要的数据
    uint32_t extract_load_data(const StoreBufferEntry& store_entry, uint32_t load_addr, uint8_t load_size) const;
};

} // namespace riscv
