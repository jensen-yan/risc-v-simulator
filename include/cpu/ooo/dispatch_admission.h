#pragma once

#include "cpu/ooo/dynamic_inst.h"
#include "cpu/ooo/register_rename.h"
#include "cpu/ooo/reservation_station.h"
#include "cpu/ooo/store_forwarding_buffer.h"
#include "cpu/ooo/store_queue.h"

#include <unordered_map>

namespace riscv {

class DispatchAdmission {
public:
    enum class Status {
        Admitted,
        InvalidInstruction,
        RenameStall,
        ReservationStationFull,
    };

    struct Result {
        Status status = Status::InvalidInstruction;
        RSEntry rs_entry = 0;
        bool ready_store_published = false;
        bool rename_checkpoint_saved = false;

        bool admitted() const { return status == Status::Admitted; }
    };

    DispatchAdmission(
        RegisterRenameUnit& register_rename,
        ReservationStation& reservation_station,
        StoreQueue& store_queue,
        StoreForwardingBuffer& store_forwarding_buffer,
        std::unordered_map<uint64_t, RegisterRenameUnit::Checkpoint>& rename_checkpoints);

    Result tryAdmit(const DynamicInstPtr& instruction,
                    uint64_t cycle,
                    bool save_rename_checkpoint);

private:
    static void bindRenameResult(DynamicInst& instruction,
                                 const RegisterRenameUnit::RenameResult& rename_result);

    RegisterRenameUnit& register_rename_;
    ReservationStation& reservation_station_;
    StoreQueue& store_queue_;
    StoreForwardingBuffer& store_forwarding_buffer_;
    std::unordered_map<uint64_t, RegisterRenameUnit::Checkpoint>& rename_checkpoints_;
};

} // namespace riscv
