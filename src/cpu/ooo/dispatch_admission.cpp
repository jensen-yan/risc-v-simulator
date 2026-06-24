#include "cpu/ooo/dispatch_admission.h"

namespace riscv {

DispatchAdmission::DispatchAdmission(
    RegisterRenameUnit& register_rename,
    ReservationStation& reservation_station,
    StoreBuffer& store_buffer,
    std::unordered_map<uint64_t, RegisterRenameUnit::Checkpoint>& rename_checkpoints)
    : register_rename_(register_rename),
      reservation_station_(reservation_station),
      store_buffer_(store_buffer),
      rename_checkpoints_(rename_checkpoints) {}

DispatchAdmission::Result DispatchAdmission::tryAdmit(
    const DynamicInstPtr& instruction,
    uint64_t cycle,
    bool save_rename_checkpoint) {
    Result result;

    if (!instruction) {
        result.status = Status::InvalidInstruction;
        return result;
    }

    if (!reservation_station_.has_free_entry()) {
        result.status = Status::ReservationStationFull;
        return result;
    }

    auto rename_result = register_rename_.rename_instruction(instruction->get_decoded_info());
    if (!rename_result.success) {
        result.status = Status::RenameStall;
        return result;
    }

    const auto dispatch_result = reservation_station_.dispatch_instruction(instruction);
    if (!dispatch_result.success) {
        register_rename_.rollback_rename(rename_result);
        result.status = Status::ReservationStationFull;
        return result;
    }

    bindRenameResult(*instruction, rename_result);
    instruction->set_dispatch_cycle(cycle);
    instruction->set_status(DynamicInst::Status::DISPATCHED);

    if (save_rename_checkpoint) {
        rename_checkpoints_[instruction->get_instruction_id()] =
            register_rename_.capture_checkpoint();
        result.rename_checkpoint_saved = true;
    }

    result.ready_store_published = store_buffer_.publish_ready_store(instruction);
    result.status = Status::Admitted;
    result.rs_entry = dispatch_result.rs_entry;
    return result;
}

void DispatchAdmission::bindRenameResult(
    DynamicInst& instruction,
    const RegisterRenameUnit::RenameResult& rename_result) {
    instruction.bind_src1_operand(rename_result.src1);
    instruction.bind_src2_operand(rename_result.src2);
    instruction.bind_src3_operand(rename_result.src3);
    instruction.set_physical_dest_kind(rename_result.dest_kind);
    instruction.set_physical_dest(rename_result.dest_reg);
}

} // namespace riscv
