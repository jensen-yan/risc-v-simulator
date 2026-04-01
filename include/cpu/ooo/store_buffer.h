#pragma once

#include "common/types.h"
#include "cpu/ooo/dynamic_inst.h"
#include <array>
#include <cstdint>
#include <limits>

namespace riscv {

// Store Buffer 表项，用于 Store-to-Load Forwarding
struct StoreBufferEntry {
    bool valid;                     // 表项是否有效
    DynamicInstPtr instruction;     // 关联的动态指令对象
    uint64_t address;              // 存储地址 (64位)
    uint64_t value;                // 存储值 (64位)
    uint8_t size;                  // 存储大小（1=字节, 2=半字, 4=字, 8=双字）
    
    StoreBufferEntry() : valid(false), instruction(nullptr), address(0), value(0), size(0) {}
};

// Store Buffer，用于实现 Store-to-Load Forwarding
class StoreBuffer {
public:
    // 需覆盖全部在飞store，避免提早发布后因环形覆盖丢失live entry。
    static const int MAX_ENTRIES = 96;

    enum class LoadForwardingKind {
        NoMatch,
        FullMatch,
        PartialMatch,
        BlockedByOverlap,
    };

    struct LoadForwardingInfo {
        LoadForwardingKind kind = LoadForwardingKind::NoMatch;
        uint64_t value = 0;
        uint8_t byte_mask = 0;
        std::array<DynamicInstPtr, MAX_ENTRIES> contributing_stores{};
        size_t contributing_count = 0;
        DynamicInstPtr primary_store = nullptr;
    };

    StoreBuffer();

    // 添加或更新Store条目；同一条动态指令重复发布时保持幂等
    void add_store(DynamicInstPtr instruction, uint64_t address, uint64_t value, uint8_t size);

    // 当store地址和值都已ready时，提前向 younger load 暴露 forwarding 可见性
    bool publish_ready_store(DynamicInstPtr instruction);

    // 查找匹配的Store (返回是否找到，如果找到则通过result_value返回值)
    bool forward_load(uint64_t address, uint8_t size, uint64_t& result_value) const;
    bool forward_load(uint64_t address, uint8_t size, uint64_t& result_value,
                      uint64_t current_instruction_id, bool& blocked) const;
    LoadForwardingKind classify_load_forwarding(uint64_t address,
                                                uint8_t size,
                                                uint64_t& result_value,
                                                uint64_t current_instruction_id) const;
    LoadForwardingKind classify_load_forwarding(uint64_t address,
                                                uint8_t size,
                                                uint64_t& result_value,
                                                uint64_t current_instruction_id,
                                                DynamicInstPtr* matched_store) const;
    LoadForwardingInfo analyze_load_forwarding(uint64_t address,
                                               uint8_t size,
                                               uint64_t current_instruction_id) const;

    // 清除指定指令ID及之前的Store条目（当Store指令提交时调用）
    void retire_stores_before(uint64_t instruction_id);
    void flush_after(uint64_t instruction_id);

    // 清空所有条目（流水线刷新时调用）
    void flush();

    // 调试：打印Store Buffer状态
    void dump() const;
    size_t get_occupied_entry_count() const;
    size_t get_capacity() const { return MAX_ENTRIES; }

private:
    std::array<StoreBufferEntry, MAX_ENTRIES> entries;
    int next_allocate_index; // 下一个分配位置（循环使用）
    
private:
    int find_entry_for_instruction(const DynamicInstPtr& instruction) const;

    // 检查两个内存访问是否有重叠
    bool addresses_overlap(uint64_t addr1, uint8_t size1, uint64_t addr2, uint8_t size2) const;
    
    // 检查是否可以从Store条目中提取Load数据
    bool can_extract_load_data(const StoreBufferEntry& store_entry, uint64_t load_addr, uint8_t load_size) const;
    
    // 从Store Buffer条目中提取Load需要的数据
    uint64_t extract_load_data(const StoreBufferEntry& store_entry, uint64_t load_addr, uint8_t load_size) const;
};

} // namespace riscv
