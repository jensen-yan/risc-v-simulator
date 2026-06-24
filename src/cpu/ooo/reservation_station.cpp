#include "cpu/ooo/reservation_station.h"
#include "cpu/ooo/store_buffer.h"
#include "common/debug_types.h"
#include <algorithm>

namespace riscv {

ReservationStation::ReservationStation() 
    : rs_entries(MAX_RS_ENTRIES),
      dispatched_count(0),
      stall_count(0) {
    
    initialize_free_list();
}

void ReservationStation::initialize_free_list() {
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        rs_entries[i] = nullptr;  // 初始化为空指针
    }
}

ReservationStation::DispatchResult ReservationStation::dispatch_instruction(DynamicInstPtr dynamic_inst) {
    DispatchResult result;
    result.success = false;
    
    if (!dynamic_inst) {
        result.error_message = "invalid DynamicInst pointer";
        return result;
    }
    
    if (!has_free_entry()) {
        result.error_message = "reservation station full";
        stall_count++;
        return result;
    }
    
    // 分配保留站表项
    RSEntry rs_id = allocate_entry();
    rs_entries[rs_id] = dynamic_inst;
    
    // 设置RS表项编号并更新状态
    dynamic_inst->set_rs_entry(rs_id);
    dynamic_inst->set_status(DynamicInst::Status::DISPATCHED);
    
    result.success = true;
    result.rs_entry = rs_id;
    dispatched_count++;
    
    LOGT(RS, "dispatch to rs[%d], pc=0x%" PRIx64 ", inst=%" PRId64,
           (int)rs_id, dynamic_inst->get_pc(), dynamic_inst->get_instruction_id());
    
    return result;
}

void ReservationStation::update_operands(const CompletionEvent& completion_event, StoreBuffer* store_buffer) {
    if (!completion_event.valid || !completion_event.instruction) return;
    
    auto phys_dest = completion_event.instruction->get_physical_dest();
    auto dest_kind = completion_event.instruction->get_physical_dest_kind();
    auto result = completion_event.instruction->get_result();
    if (dest_kind == RegisterFileKind::None) {
        return;
    }
    
    // 遍历所有保留站表项，更新等待该物理寄存器的操作数
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        if (rs_entries[i]) {
            DynamicInstPtr inst = rs_entries[i];
            
            // 检查源操作数1
            if (!inst->is_src1_ready() &&
                inst->get_physical_src1_kind() == dest_kind &&
                inst->get_physical_src1() == phys_dest) {
                inst->set_src1_ready(true, result);
                LOGT(RS, "rs[%d] src1 ready: p%d = 0x%" PRIx64, i, phys_dest, result);
            }
            
            // 检查源操作数2
            if (!inst->is_src2_ready() &&
                inst->get_physical_src2_kind() == dest_kind &&
                inst->get_physical_src2() == phys_dest) {
                inst->set_src2_ready(true, result);
                LOGT(RS, "rs[%d] src2 ready: p%d = 0x%" PRIx64, i, phys_dest, result);
            }

            if (!inst->is_src3_ready() &&
                inst->get_physical_src3_kind() == dest_kind &&
                inst->get_physical_src3() == phys_dest) {
                inst->set_src3_ready(true, result);
                LOGT(RS, "rs[%d] src3 ready: p%d = 0x%" PRIx64, i, phys_dest, result);
            }

            if (store_buffer) {
                store_buffer->publish_ready_store(inst);
            }
        }
    }
}

void ReservationStation::release_entry(RSEntry rs_entry) {
    if (rs_entry >= MAX_RS_ENTRIES) return;
    
    if (rs_entries[rs_entry]) {
        LOGT(RS, "release rs[%d]", (int)rs_entry);
        rs_entries[rs_entry] = nullptr;
    }
}

void ReservationStation::flush_pipeline() {
    LOGT(RS, "flush reservation station pipeline");
    
    // 清空所有表项
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        rs_entries[i] = nullptr;
    }
    
    initialize_free_list();
}

void ReservationStation::flush_younger_than(uint64_t instruction_id) {
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        if (rs_entries[i] && rs_entries[i]->get_instruction_id() > instruction_id) {
            LOGT(RS, "flush younger rs[%d], inst=%" PRId64,
                 i, rs_entries[i]->get_instruction_id());
            rs_entries[i] = nullptr;
        }
    }
}

bool ReservationStation::has_free_entry() const {
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        if (rs_entries[i] == nullptr) {
            return true;
        }
    }
    return false;
}

size_t ReservationStation::get_free_entry_count() const {
    size_t count = 0;
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        if (rs_entries[i] == nullptr) {
            count++;
        }
    }
    return count;
}

void ReservationStation::get_statistics(uint64_t& dispatched, uint64_t& stalls) const {
    dispatched = dispatched_count;
    stalls = stall_count;
}

void ReservationStation::dump_reservation_station() const {
    std::cout << "=== Reservation Station Dump ===" << std::endl;
    
    // for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
    //     if (rs_entries[i]) {
    //         std::cout << "[RS" << std::setw(2) << i << "] " 
    //                  << rs_entries[i]->to_string() << std::endl;
    //     }
    // }
    
    std::cout << "==============================" << std::endl;
}

DynamicInstPtr ReservationStation::get_entry(RSEntry rs_entry) const {
    if (rs_entry >= MAX_RS_ENTRIES) return nullptr;
    return rs_entries[rs_entry];
}

size_t ReservationStation::get_occupied_entry_count() const {
    size_t occupied = 0;
    for (const auto& entry : rs_entries) {
        if (entry) {
            ++occupied;
        }
    }
    return occupied;
}

size_t ReservationStation::get_ready_entry_count() const {
    size_t ready = 0;
    for (const auto& entry : rs_entries) {
        if (entry &&
            entry->get_status() != DynamicInst::Status::EXECUTING &&
            is_instruction_ready(entry)) {
            ++ready;
        }
    }
    return ready;
}

std::vector<ReservationStation::ReadyEntry> ReservationStation::ready_entries() const {
    std::vector<ReadyEntry> ready;
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        const auto& entry = rs_entries[i];
        if (!entry || entry->get_status() == DynamicInst::Status::EXECUTING ||
            !is_instruction_ready(entry)) {
            continue;
        }
        ready.push_back({static_cast<RSEntry>(i), entry});
    }
    std::sort(ready.begin(), ready.end(), [](const ReadyEntry& lhs, const ReadyEntry& rhs) {
        return lhs.instruction->get_instruction_id() < rhs.instruction->get_instruction_id();
    });
    return ready;
}

bool ReservationStation::is_entry_ready(RSEntry rs_entry) const {
    if (rs_entry >= MAX_RS_ENTRIES) return false;
    DynamicInstPtr inst = rs_entries[rs_entry];
    return inst && is_instruction_ready(inst);
}

// ========== 私有方法实现 ==========
RSEntry ReservationStation::allocate_entry() {
    // 寻找真正空闲的条目
    for (int i = 0; i < MAX_RS_ENTRIES; ++i) {
        if (rs_entries[i] == nullptr) {
            return i;
        }
    }
    
    // 没有空闲条目
    return MAX_RS_ENTRIES;
}

bool ReservationStation::is_instruction_ready(DynamicInstPtr instruction) const {
    if (!instruction) return false;
    return instruction->is_ready_to_execute();
}

} // namespace riscv
