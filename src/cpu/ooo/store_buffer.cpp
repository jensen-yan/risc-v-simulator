#include "cpu/ooo/store_buffer.h"
#include "common/debug_types.h"
#include "common/types.h"
#include <algorithm>

namespace riscv {

StoreBuffer::StoreBuffer() : next_allocate_index(0) {
    for (auto& entry : entries) {
        entry.valid = false;
    }
}

int StoreBuffer::find_entry_for_instruction(const DynamicInstPtr& instruction) const {
    if (!instruction) {
        return -1;
    }

    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (entries[i].valid && entries[i].instruction == instruction) {
            return i;
        }
    }
    return -1;
}

void StoreBuffer::add_store(DynamicInstPtr instruction, uint64_t address, uint64_t value, uint8_t size) {
    const int existing_index = find_entry_for_instruction(instruction);
    int target_index = existing_index;

    if (target_index < 0) {
        for (int i = 0; i < MAX_ENTRIES; ++i) {
            const int candidate = (next_allocate_index + i) % MAX_ENTRIES;
            if (!entries[candidate].valid) {
                target_index = candidate;
                break;
            }
        }
    }

    if (target_index < 0) {
        throw SimulatorException("store buffer is full while publishing a live store");
    }

    // 找到下一个可用的条目或更新现有条目
    StoreBufferEntry& entry = entries[target_index];

    entry.valid = true;
    entry.instruction = instruction;
    entry.address = address;
    entry.value = value;
    entry.size = size;

    if (instruction) {
        instruction->get_memory_info().store_buffer_published = true;
    }

    LOGT(EXECUTE, "store buffer add[%d]: addr=0x%" PRIx64 ", value=0x%" PRIx64 ", size=%d, inst=%" PRId64 ", pc=0x%" PRIx64,
            target_index, address, value, size, instruction->get_instruction_id(), instruction->get_pc());

    if (existing_index < 0) {
        // 移动到下一个分配位置（从下一个位置继续找空槽）
        next_allocate_index = (target_index + 1) % MAX_ENTRIES;
    }
}

bool StoreBuffer::publish_ready_store(DynamicInstPtr instruction) {
    if (!instruction || !instruction->is_store_instruction()) {
        return false;
    }

    auto& memory_info = instruction->get_memory_info();
    if (!memory_info.is_store || !memory_info.address_ready || memory_info.memory_size == 0 ||
        !instruction->is_src2_ready()) {
        return false;
    }

    add_store(instruction, memory_info.memory_address, memory_info.memory_value, memory_info.memory_size);
    return true;
}

bool StoreBuffer::forward_load(uint64_t address, uint8_t size, uint64_t& result_value) const {
    const auto kind =
        classify_load_forwarding(address, size, result_value, std::numeric_limits<uint64_t>::max());
    return kind == LoadForwardingKind::FullMatch || kind == LoadForwardingKind::PartialMatch;
}

bool StoreBuffer::forward_load(uint64_t address, uint8_t size, uint64_t& result_value,
                               uint64_t current_instruction_id, bool& blocked) const {
    const auto kind = classify_load_forwarding(address, size, result_value, current_instruction_id);
    blocked = (kind == LoadForwardingKind::BlockedByOverlap);
    return kind == LoadForwardingKind::FullMatch || kind == LoadForwardingKind::PartialMatch;
}

StoreBuffer::LoadForwardingInfo StoreBuffer::analyze_load_forwarding(uint64_t address,
                                                                    uint8_t size,
                                                                    uint64_t current_instruction_id) const {
    LoadForwardingInfo info;
    if (size == 0 || size > 8) {
        return info;
    }

    const uint8_t full_mask = static_cast<uint8_t>(size == 8 ? 0xFFu : ((1u << size) - 1u));
    bool saw_overlap = false;

    for (int i = 0; i < MAX_ENTRIES; ++i) {
        int index = (next_allocate_index - 1 - i + MAX_ENTRIES) % MAX_ENTRIES;
        const StoreBufferEntry& entry = entries[index];

        if (!entry.valid || !entry.instruction) {
            continue;
        }
        if (entry.instruction->get_instruction_id() >= current_instruction_id) {
            continue;
        }
        if (!addresses_overlap(entry.address, entry.size, address, size)) {
            continue;
        }

        saw_overlap = true;
        if (!info.primary_store) {
            info.primary_store = entry.instruction;
        }

        bool contributed = false;
        const uint64_t load_end = address + size;
        const uint64_t store_end = entry.address + entry.size;
        const uint64_t overlap_begin = std::max(address, entry.address);
        const uint64_t overlap_end = std::min(load_end, store_end);
        for (uint64_t byte_addr = overlap_begin; byte_addr < overlap_end; ++byte_addr) {
            const uint8_t load_byte_index = static_cast<uint8_t>(byte_addr - address);
            const uint8_t bit = static_cast<uint8_t>(1u << load_byte_index);
            if ((info.byte_mask & bit) != 0) {
                continue;
            }

            const uint8_t store_byte_index = static_cast<uint8_t>(byte_addr - entry.address);
            const uint64_t byte_value = (entry.value >> (store_byte_index * 8)) & 0xFFu;
            info.value &= ~(0xFFull << (load_byte_index * 8));
            info.value |= byte_value << (load_byte_index * 8);
            info.byte_mask |= bit;
            contributed = true;
        }

        if (contributed && info.contributing_count < info.contributing_stores.size()) {
            info.contributing_stores[info.contributing_count++] = entry.instruction;
        }

        if (entry.address == address && entry.size == size) {
            info.kind = LoadForwardingKind::FullMatch;
            info.value = entry.value;
            info.byte_mask = full_mask;
            info.contributing_stores.fill(nullptr);
            info.contributing_stores[0] = entry.instruction;
            info.contributing_count = 1;
            info.primary_store = entry.instruction;
            return info;
        }
    }

    if (!saw_overlap) {
        return info;
    }

    if (info.byte_mask == 0) {
        info.kind = LoadForwardingKind::BlockedByOverlap;
        return info;
    }

    info.kind = LoadForwardingKind::PartialMatch;
    return info;
}

StoreBuffer::LoadForwardingKind StoreBuffer::classify_load_forwarding(uint64_t address,
                                                                      uint8_t size,
                                                                      uint64_t& result_value,
                                                                      uint64_t current_instruction_id) const {
    return classify_load_forwarding(address, size, result_value, current_instruction_id, nullptr);
}

StoreBuffer::LoadForwardingKind StoreBuffer::classify_load_forwarding(uint64_t address,
                                                                      uint8_t size,
                                                                      uint64_t& result_value,
                                                                      uint64_t current_instruction_id,
                                                                      DynamicInstPtr* matched_store) const {
    const auto info = analyze_load_forwarding(address, size, current_instruction_id);
    if (matched_store) {
        *matched_store = info.primary_store;
    }

    const uint8_t full_mask = static_cast<uint8_t>(size == 8 ? 0xFFu : ((1u << size) - 1u));
    switch (info.kind) {
        case LoadForwardingKind::FullMatch:
            result_value = info.value;
            LOGT(EXECUTE, "store-to-load forwarding full match: addr=0x%" PRIx64 ", size=%d, value=0x%" PRIx64,
                 address, size, result_value);
            return LoadForwardingKind::FullMatch;
        case LoadForwardingKind::PartialMatch:
            if (info.byte_mask == full_mask) {
                result_value = info.value;
                LOGT(EXECUTE, "store-to-load forwarding partial/full-coverage: addr=0x%" PRIx64 ", size=%d, value=0x%" PRIx64 ", mask=0x%x",
                     address, size, result_value, info.byte_mask);
                return LoadForwardingKind::PartialMatch;
            }
            LOGT(EXECUTE, "store-to-load overlap requires memory merge: addr=0x%" PRIx64 ", size=%d, mask=0x%x",
                 address, size, info.byte_mask);
            return LoadForwardingKind::BlockedByOverlap;
        case LoadForwardingKind::BlockedByOverlap:
            LOGT(EXECUTE, "store-to-load overlap but cannot forward: addr=0x%" PRIx64 ", size=%d",
                 address, size);
            return LoadForwardingKind::BlockedByOverlap;
        case LoadForwardingKind::NoMatch:
        default:
            LOGT(EXECUTE, "store-to-load no matching store: addr=0x%" PRIx64 ", size=%d", address, size);
            return LoadForwardingKind::NoMatch;
    }
}

void StoreBuffer::retire_stores_before(uint64_t instruction_id) {
    int retired_count = 0;
    
    for (int i = 0; i < MAX_ENTRIES; ++i) {
        if (entries[i].valid && entries[i].instruction && entries[i].instruction->get_instruction_id() <= instruction_id) {
            LOGT(EXECUTE, "store buffer retire[%d]: inst=%" PRId64 ", addr=0x%" PRIx64,
                    i, entries[i].instruction->get_instruction_id(), entries[i].address);
            entries[i].instruction->get_memory_info().store_buffer_published = false;
            entries[i].valid = false;
            entries[i].instruction = nullptr; // 清除指令指针
            retired_count++;
        }
    }
    
    if (retired_count > 0) {
        LOGT(EXECUTE, "store buffer retired %d entries, instruction_id <= %" PRId64, retired_count, instruction_id);
    }
}

void StoreBuffer::flush_after(uint64_t instruction_id) {
    int flushed_count = 0;
    for (auto& entry : entries) {
        if (entry.valid && entry.instruction &&
            entry.instruction->get_instruction_id() > instruction_id) {
            LOGT(EXECUTE, "store buffer flush younger: inst=%" PRId64 ", addr=0x%" PRIx64,
                 entry.instruction->get_instruction_id(), entry.address);
            entry.instruction->get_memory_info().store_buffer_published = false;
            entry.valid = false;
            entry.instruction = nullptr;
            flushed_count++;
        }
    }

    if (flushed_count > 0) {
        LOGT(EXECUTE, "store buffer flushed %d younger entries after inst=%" PRId64,
             flushed_count, instruction_id);
    }
}

void StoreBuffer::flush() {
    LOGT(EXECUTE, "store buffer flush: clear all entries");
    
    for (auto& entry : entries) {
        if (entry.valid && entry.instruction) {
            entry.instruction->get_memory_info().store_buffer_published = false;
        }
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

size_t StoreBuffer::get_occupied_entry_count() const {
    size_t occupied = 0;
    for (const auto& entry : entries) {
        if (entry.valid && entry.instruction) {
            occupied++;
        }
    }
    return occupied;
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
