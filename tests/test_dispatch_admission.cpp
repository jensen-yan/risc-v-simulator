#include <gtest/gtest.h>

#include "cpu/ooo/dispatch_admission.h"

#include <unordered_map>

namespace riscv {
namespace {

DecodedInstruction makeAddiInstruction(RegNum rd, RegNum rs1, int32_t imm) {
    DecodedInstruction decoded;
    decoded.type = InstructionType::I_TYPE;
    decoded.opcode = Opcode::OP_IMM;
    decoded.rd = rd;
    decoded.rs1 = rs1;
    decoded.rs2 = 0;
    decoded.imm = imm;
    decoded.execution_cycles = 1;
    return decoded;
}

DecodedInstruction makeStoreInstruction(RegNum rs1, RegNum rs2, uint8_t size) {
    DecodedInstruction decoded;
    decoded.type = InstructionType::S_TYPE;
    decoded.opcode = Opcode::STORE;
    decoded.rd = 0;
    decoded.rs1 = rs1;
    decoded.rs2 = rs2;
    decoded.memory_access_size = size;
    decoded.execution_cycles = 1;
    return decoded;
}

DynamicInstPtr makeDynamicInst(const DecodedInstruction& decoded, uint64_t id) {
    return create_dynamic_inst(decoded, 0x80000000 + id * 4, id);
}

} // namespace

class DispatchAdmissionTest : public ::testing::Test {
protected:
    RegisterRenameUnit rename_unit;
    ReservationStation reservation_station;
    StoreBuffer store_buffer;
    std::unordered_map<uint64_t, RegisterRenameUnit::Checkpoint> rename_checkpoints;

    DispatchAdmission admission() {
        return DispatchAdmission(rename_unit, reservation_station, store_buffer, rename_checkpoints);
    }
};

TEST_F(DispatchAdmissionTest, AdmitsInstructionBindsOperandsAndPlacesInReservationStation) {
    rename_unit.update_architecture_register(1, 0x1234);
    auto inst = makeDynamicInst(makeAddiInstruction(2, 1, 7), 1);

    auto result = admission().tryAdmit(inst, 42, false);

    ASSERT_TRUE(result.admitted());
    EXPECT_EQ(result.status, DispatchAdmission::Status::Admitted);
    EXPECT_EQ(result.rs_entry, 0);
    EXPECT_EQ(reservation_station.get_occupied_entry_count(), 1u);
    EXPECT_EQ(reservation_station.get_entry(result.rs_entry), inst);
    EXPECT_EQ(inst->get_status(), DynamicInst::Status::DISPATCHED);
    EXPECT_EQ(inst->get_dispatch_cycle(), 42u);
    EXPECT_EQ(inst->get_physical_src1_kind(), RegisterFileKind::Integer);
    EXPECT_EQ(inst->get_physical_src1(), 1);
    EXPECT_TRUE(inst->is_src1_ready());
    EXPECT_EQ(inst->get_src1_value(), 0x1234u);
    EXPECT_EQ(inst->get_physical_dest_kind(), RegisterFileKind::Integer);
    EXPECT_NE(inst->get_physical_dest(), 0);
}

TEST_F(DispatchAdmissionTest, ReservationStationFullStallsBeforeRenameSideEffects) {
    for (size_t i = 0; i < OOOPipelineConfig::RS_ENTRIES; ++i) {
        auto filler = makeDynamicInst(makeAddiInstruction(0, 0, 0), i + 1);
        ASSERT_TRUE(reservation_station.dispatch_instruction(filler).success);
    }
    ASSERT_FALSE(reservation_station.has_free_entry());

    const auto free_before = rename_unit.get_free_register_count();
    const auto old_mapping = rename_unit.lookup_source(RegisterFileKind::Integer, 3).physical_reg;
    auto inst = makeDynamicInst(makeAddiInstruction(3, 0, 1), 1000);

    auto result = admission().tryAdmit(inst, 77, false);

    EXPECT_EQ(result.status, DispatchAdmission::Status::ReservationStationFull);
    EXPECT_FALSE(result.admitted());
    EXPECT_EQ(rename_unit.get_free_register_count(), free_before);
    EXPECT_EQ(rename_unit.lookup_source(RegisterFileKind::Integer, 3).physical_reg, old_mapping);
    EXPECT_EQ(inst->get_status(), DynamicInst::Status::ALLOCATED);
    EXPECT_EQ(inst->get_physical_dest_kind(), RegisterFileKind::None);
}

TEST_F(DispatchAdmissionTest, PublishesReadyStoreWhenAddressAndValueAreReadyAtAdmission) {
    rename_unit.update_architecture_register(1, 0x1000);
    rename_unit.update_architecture_register(2, 0xdeadbeef);
    auto inst = makeDynamicInst(makeStoreInstruction(1, 2, 4), 2);
    auto& memory_info = inst->get_memory_info();
    memory_info.address_ready = true;
    memory_info.memory_address = 0x1000;

    auto result = admission().tryAdmit(inst, 80, false);

    ASSERT_TRUE(result.admitted());
    EXPECT_TRUE(result.ready_store_published);
    EXPECT_EQ(store_buffer.get_occupied_entry_count(), 1u);
    EXPECT_TRUE(memory_info.store_buffer_published);
    EXPECT_EQ(memory_info.memory_size, 4);
    EXPECT_EQ(memory_info.memory_value, 0xdeadbeefu);
}

TEST_F(DispatchAdmissionTest, SavesRenameCheckpointWhenRequested) {
    auto inst = makeDynamicInst(makeAddiInstruction(5, 0, 1), 5);

    auto result = admission().tryAdmit(inst, 90, true);

    ASSERT_TRUE(result.admitted());
    EXPECT_TRUE(result.rename_checkpoint_saved);
    EXPECT_NE(rename_checkpoints.find(inst->get_instruction_id()), rename_checkpoints.end());
}

} // namespace riscv
