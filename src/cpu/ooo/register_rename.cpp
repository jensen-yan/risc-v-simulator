#include "cpu/ooo/register_rename.h"
#include "common/debug_types.h"
#include "core/instruction_executor.h"
#include <cassert>
#include <set>

namespace riscv {

namespace {

constexpr uint8_t kFpFunct5Shift = 2;

uint8_t fpFunct5(const DecodedInstruction& decoded_info) {
    return static_cast<uint8_t>(static_cast<uint8_t>(decoded_info.funct7) >> kFpFunct5Shift);
}

bool isFusedFpInstruction(const DecodedInstruction& decoded_info) {
    return decoded_info.opcode == Opcode::FMADD ||
           decoded_info.opcode == Opcode::FMSUB ||
           decoded_info.opcode == Opcode::FNMSUB ||
           decoded_info.opcode == Opcode::FNMADD;
}

RegisterFileKind fpSource1Kind(const DecodedInstruction& decoded_info) {
    if (decoded_info.opcode == Opcode::LOAD_FP || decoded_info.opcode == Opcode::STORE_FP) {
        return RegisterFileKind::Integer;
    }
    if (isFusedFpInstruction(decoded_info)) {
        return RegisterFileKind::FloatingPoint;
    }
    if (decoded_info.opcode != Opcode::OP_FP) {
        return RegisterFileKind::None;
    }
    switch (fpFunct5(decoded_info)) {
        case 0x1A:
        case 0x1E:
            return RegisterFileKind::Integer;
        default:
            return RegisterFileKind::FloatingPoint;
    }
}

RegisterFileKind fpSource2Kind(const DecodedInstruction& decoded_info) {
    if (decoded_info.opcode == Opcode::STORE_FP || isFusedFpInstruction(decoded_info)) {
        return RegisterFileKind::FloatingPoint;
    }
    if (decoded_info.opcode != Opcode::OP_FP) {
        return RegisterFileKind::None;
    }
    switch (fpFunct5(decoded_info)) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x14:
            return RegisterFileKind::FloatingPoint;
        default:
            return RegisterFileKind::None;
    }
}

RegisterFileKind fpSource3Kind(const DecodedInstruction& decoded_info) {
    return isFusedFpInstruction(decoded_info)
               ? RegisterFileKind::FloatingPoint
               : RegisterFileKind::None;
}

RegisterFileKind fpDestinationKind(const DecodedInstruction& decoded_info) {
    if (decoded_info.opcode == Opcode::STORE_FP) {
        return RegisterFileKind::None;
    }
    return InstructionExecutor::isFPIntegerDestination(decoded_info)
               ? RegisterFileKind::Integer
               : RegisterFileKind::FloatingPoint;
}

}  // namespace

RegisterRenameUnit::RegisterRenameUnit()
    : rename_table(NUM_LOGICAL_REGS),
      fp_rename_table(NUM_LOGICAL_FP_REGS),
      physical_registers(NUM_PHYSICAL_REGS),
      fp_physical_registers(NUM_PHYSICAL_REGS),
      arch_map(NUM_LOGICAL_REGS),
      fp_arch_map(NUM_LOGICAL_FP_REGS),
      rename_count(0),
      stall_count(0) {
    initialize_physical_registers();
    initialize_rename_table();
    initialize_free_list();
}

std::vector<RenameEntry>& RegisterRenameUnit::table_for_kind(RegisterFileKind kind) {
    return kind == RegisterFileKind::FloatingPoint ? fp_rename_table : rename_table;
}

const std::vector<RenameEntry>& RegisterRenameUnit::table_for_kind(RegisterFileKind kind) const {
    return kind == RegisterFileKind::FloatingPoint ? fp_rename_table : rename_table;
}

std::vector<PhysicalRegister>& RegisterRenameUnit::physicals_for_kind(RegisterFileKind kind) {
    return kind == RegisterFileKind::FloatingPoint ? fp_physical_registers : physical_registers;
}

const std::vector<PhysicalRegister>& RegisterRenameUnit::physicals_for_kind(RegisterFileKind kind) const {
    return kind == RegisterFileKind::FloatingPoint ? fp_physical_registers : physical_registers;
}

std::queue<PhysRegNum>& RegisterRenameUnit::free_list_for_kind(RegisterFileKind kind) {
    return kind == RegisterFileKind::FloatingPoint ? fp_free_list : free_list;
}

std::vector<PhysRegNum>& RegisterRenameUnit::arch_map_for_kind(RegisterFileKind kind) {
    return kind == RegisterFileKind::FloatingPoint ? fp_arch_map : arch_map;
}

const std::vector<PhysRegNum>& RegisterRenameUnit::arch_map_for_kind(RegisterFileKind kind) const {
    return kind == RegisterFileKind::FloatingPoint ? fp_arch_map : arch_map;
}

void RegisterRenameUnit::initialize_physical_registers() {
    for (int i = 0; i < NUM_PHYSICAL_REGS; ++i) {
        physical_registers[i] = PhysicalRegister();
        physical_registers[i].ready = true;
        physical_registers[i].value = 0;

        fp_physical_registers[i] = PhysicalRegister();
        fp_physical_registers[i].ready = true;
        fp_physical_registers[i].value = 0;
    }

    physical_registers[0].value = 0;
    physical_registers[0].ready = true;
}

void RegisterRenameUnit::initialize_rename_table() {
    for (int i = 0; i < NUM_LOGICAL_REGS; ++i) {
        rename_table[i].physical_reg = i;
        rename_table[i].valid = true;
        arch_map[i] = i;
    }

    for (int i = 0; i < NUM_LOGICAL_FP_REGS; ++i) {
        fp_rename_table[i].physical_reg = i;
        fp_rename_table[i].valid = true;
        fp_arch_map[i] = i;
    }
}

void RegisterRenameUnit::initialize_free_list() {
    for (int i = NUM_LOGICAL_REGS; i < NUM_PHYSICAL_REGS; ++i) {
        free_list.push(static_cast<PhysRegNum>(i));
        fp_free_list.push(static_cast<PhysRegNum>(i));
    }
}

void RegisterRenameUnit::rebuild_free_list_excluding(const std::set<PhysRegNum>& reserved_regs) {
    rebuild_free_list_excluding(RegisterFileKind::Integer, reserved_regs);
}

void RegisterRenameUnit::rebuild_free_list_excluding(RegisterFileKind kind,
                                                     const std::set<PhysRegNum>& reserved_regs) {
    auto& free_regs = free_list_for_kind(kind);
    while (!free_regs.empty()) {
        free_regs.pop();
    }

    for (int i = NUM_LOGICAL_REGS; i < NUM_PHYSICAL_REGS; ++i) {
        if (reserved_regs.find(static_cast<PhysRegNum>(i)) == reserved_regs.end()) {
            free_regs.push(static_cast<PhysRegNum>(i));
        }
    }
}

RegisterRenameUnit::SourceLookupResult RegisterRenameUnit::lookup_source(RegisterFileKind kind,
                                                                        RegNum logical_reg) const {
    SourceLookupResult result{};
    if (kind == RegisterFileKind::None) {
        return result;
    }

    const auto& rename_ref = table_for_kind(kind);
    const auto& physical_ref = physicals_for_kind(kind);
    if (logical_reg >= rename_ref.size()) {
        return result;
    }

    result.kind = kind;
    result.physical_reg = rename_ref[logical_reg].physical_reg;
    result.ready = physical_ref[result.physical_reg].ready;
    result.value = physical_ref[result.physical_reg].value;
    return result;
}

RegisterRenameUnit::DestinationAllocateResult RegisterRenameUnit::allocate_destination(
    RegisterFileKind kind, RegNum logical_reg) {
    DestinationAllocateResult result{};
    if (kind == RegisterFileKind::None) {
        result.success = true;
        return result;
    }
    if (kind == RegisterFileKind::Integer && logical_reg == 0) {
        result.success = true;
        return result;
    }

    auto& free_regs = free_list_for_kind(kind);
    if (free_regs.empty()) {
        stall_count++;
        LOGW(RENAME, "rename failed: need dst %s%d but no free physical register",
             kind == RegisterFileKind::FloatingPoint ? "f" : "x",
             static_cast<int>(logical_reg));
        return result;
    }

    result.dest_reg = allocate_physical_register(kind);
    auto& rename_ref = table_for_kind(kind);
    auto& physical_ref = physicals_for_kind(kind);
    rename_ref[logical_reg].physical_reg = result.dest_reg;
    rename_ref[logical_reg].valid = true;
    physical_ref[result.dest_reg].ready = false;
    result.success = true;
    return result;
}

RegisterRenameUnit::RenameResult RegisterRenameUnit::rename_instruction(
    const DecodedInstruction& instruction) {
    RenameResult result{};
    result.success = false;

    if (InstructionExecutor::isFloatingPointInstruction(instruction)) {
        result.src1 = lookup_source(fpSource1Kind(instruction), instruction.rs1);
        result.src2 = lookup_source(fpSource2Kind(instruction), instruction.rs2);
        result.src3 = lookup_source(fpSource3Kind(instruction), instruction.rs3);
        result.dest_kind = fpDestinationKind(instruction);

        const auto dest = allocate_destination(result.dest_kind, instruction.rd);
        if (!dest.success) {
            LOGT(RENAME, "rename failed for fp instruction: no free destination register");
            return result;
        }
        result.dest_reg = dest.dest_reg;
        result.success = true;
        rename_count++;
        return result;
    }

    const auto src1 = lookup_source(RegisterFileKind::Integer, instruction.rs1);
    result.src1 = src1;

    bool needs_src2 = false;
    switch (instruction.type) {
        case InstructionType::R_TYPE:
        case InstructionType::S_TYPE:
        case InstructionType::B_TYPE:
            needs_src2 = true;
            break;
        case InstructionType::I_TYPE:
        case InstructionType::U_TYPE:
        case InstructionType::J_TYPE:
        case InstructionType::SYSTEM_TYPE:
            needs_src2 = false;
            break;
        default:
            LOGW(RENAME, "unknown instruction type in rename, treat as no src2");
            needs_src2 = false;
            break;
    }

    if (needs_src2) {
        const auto src2 = lookup_source(RegisterFileKind::Integer, instruction.rs2);
        result.src2 = src2;
    } else {
        result.src2 = OperandBinding{RegisterFileKind::Integer, 0, true, 0};
    }
    result.src3 = OperandBinding{RegisterFileKind::None, 0, true, 0};

    const bool needs_dest_reg = (instruction.rd != 0);
    if (needs_dest_reg) {
        result.dest_kind = RegisterFileKind::Integer;
        const PhysRegNum old_physical_reg = rename_table[instruction.rd].physical_reg;
        const auto dest = allocate_destination(RegisterFileKind::Integer, instruction.rd);
        if (!dest.success) {
            LOGT(RENAME, "free physical registers: %zu/%d", free_list.size(), NUM_PHYSICAL_REGS);
            return result;
        }
        result.dest_reg = dest.dest_reg;

        if (instruction.rs1 == instruction.rd && instruction.rs1 < NUM_LOGICAL_REGS) {
            result.src1.kind = RegisterFileKind::Integer;
            result.src1.physical_reg = old_physical_reg;
            result.src1.ready = physical_registers[old_physical_reg].ready;
            result.src1.value = physical_registers[old_physical_reg].value;
            LOGT(RENAME, "self-dependency fix: x%d rs1 uses old p%d, dst uses new p%d",
                 static_cast<int>(instruction.rd),
                 static_cast<int>(old_physical_reg),
                 static_cast<int>(result.dest_reg));
        }
        if (instruction.rs2 == instruction.rd && instruction.rs2 < NUM_LOGICAL_REGS) {
            result.src2.kind = RegisterFileKind::Integer;
            result.src2.physical_reg = old_physical_reg;
            result.src2.ready = physical_registers[old_physical_reg].ready;
            result.src2.value = physical_registers[old_physical_reg].value;
            LOGT(RENAME, "self-dependency fix: x%d rs2 uses old p%d, dst uses new p%d",
                 static_cast<int>(instruction.rd),
                 static_cast<int>(old_physical_reg),
                 static_cast<int>(result.dest_reg));
        }

        LOGT(RENAME, "rename: x%d from p%d to p%d",
             static_cast<int>(instruction.rd),
             static_cast<int>(old_physical_reg),
             static_cast<int>(result.dest_reg));
    } else {
        result.dest_kind = RegisterFileKind::None;
        result.dest_reg = 0;
    }

    result.success = true;
    rename_count++;
    return result;
}

PhysRegNum RegisterRenameUnit::allocate_physical_register() {
    return allocate_physical_register(RegisterFileKind::Integer);
}

PhysRegNum RegisterRenameUnit::allocate_physical_register(RegisterFileKind kind) {
    auto& free_regs = free_list_for_kind(kind);
    assert(!free_regs.empty() && "没有空闲的物理寄存器");

    const PhysRegNum reg = free_regs.front();
    free_regs.pop();
    return reg;
}

void RegisterRenameUnit::update_physical_register(PhysRegNum reg, uint64_t value, ROBEntry rob_entry) {
    update_physical_register(RegisterFileKind::Integer, reg, value, rob_entry);
}

void RegisterRenameUnit::update_physical_register(RegisterFileKind kind,
                                                  PhysRegNum reg,
                                                  uint64_t value,
                                                  ROBEntry rob_entry) {
    if (kind == RegisterFileKind::None) {
        return;
    }
    if (kind == RegisterFileKind::Integer && reg == 0) {
        return;
    }

    auto& physical_ref = physicals_for_kind(kind);
    physical_ref[reg].value = value;
    physical_ref[reg].ready = true;
    physical_ref[reg].producer_rob = rob_entry;

    LOGT(RENAME, "update %s%d = 0x%" PRIx64,
         kind == RegisterFileKind::FloatingPoint ? "fp" : "p",
         static_cast<int>(reg),
         value);
}

void RegisterRenameUnit::release_physical_register(PhysRegNum reg) {
    release_physical_register(RegisterFileKind::Integer, reg);
}

void RegisterRenameUnit::release_physical_register(RegisterFileKind kind, PhysRegNum reg) {
    if (kind == RegisterFileKind::None) {
        return;
    }
    if (reg < NUM_LOGICAL_REGS) {
        return;
    }

    auto& physical_ref = physicals_for_kind(kind);
    auto& free_regs = free_list_for_kind(kind);
    physical_ref[reg].ready = true;
    physical_ref[reg].value = 0;
    free_regs.push(reg);

    LOGT(RENAME, "release %s%d",
         kind == RegisterFileKind::FloatingPoint ? "fp" : "p",
         static_cast<int>(reg));
}

uint64_t RegisterRenameUnit::get_physical_register_value(PhysRegNum reg) const {
    return get_physical_register_value(RegisterFileKind::Integer, reg);
}

uint64_t RegisterRenameUnit::get_physical_register_value(RegisterFileKind kind, PhysRegNum reg) const {
    return physicals_for_kind(kind)[reg].value;
}

bool RegisterRenameUnit::is_physical_register_ready(PhysRegNum reg) const {
    return is_physical_register_ready(RegisterFileKind::Integer, reg);
}

bool RegisterRenameUnit::is_physical_register_ready(RegisterFileKind kind, PhysRegNum reg) const {
    return physicals_for_kind(kind)[reg].ready;
}

void RegisterRenameUnit::flush_pipeline() {
    for (int i = 0; i < NUM_LOGICAL_REGS; ++i) {
        rename_table[i].physical_reg = arch_map[i];
        rename_table[i].valid = true;
    }
    for (int i = 0; i < NUM_LOGICAL_FP_REGS; ++i) {
        fp_rename_table[i].physical_reg = fp_arch_map[i];
        fp_rename_table[i].valid = true;
    }

    std::set<PhysRegNum> committed_regs;
    std::set<PhysRegNum> committed_fp_regs;
    for (const auto reg : arch_map) {
        committed_regs.insert(reg);
    }
    for (const auto reg : fp_arch_map) {
        committed_fp_regs.insert(reg);
    }

    rebuild_free_list_excluding(RegisterFileKind::Integer, committed_regs);
    rebuild_free_list_excluding(RegisterFileKind::FloatingPoint, committed_fp_regs);
    LOGT(RENAME, "flush pipeline: restore rename tables to committed architectural state");
}

RegisterRenameUnit::Checkpoint RegisterRenameUnit::capture_checkpoint() const {
    Checkpoint checkpoint;
    checkpoint.rename_table = rename_table;
    checkpoint.fp_rename_table = fp_rename_table;
    checkpoint.free_list = free_list;
    checkpoint.fp_free_list = fp_free_list;
    return checkpoint;
}

void RegisterRenameUnit::restore_checkpoint(const Checkpoint& checkpoint) {
    rename_table = checkpoint.rename_table;
    fp_rename_table = checkpoint.fp_rename_table;

    std::set<PhysRegNum> live_regs;
    std::set<PhysRegNum> live_fp_regs;
    for (const auto& entry : rename_table) {
        if (entry.valid) {
            live_regs.insert(entry.physical_reg);
        }
    }
    for (const auto& entry : fp_rename_table) {
        if (entry.valid) {
            live_fp_regs.insert(entry.physical_reg);
        }
    }
    for (const auto reg : arch_map) {
        live_regs.insert(reg);
    }
    for (const auto reg : fp_arch_map) {
        live_fp_regs.insert(reg);
    }

    rebuild_free_list_excluding(RegisterFileKind::Integer, live_regs);
    rebuild_free_list_excluding(RegisterFileKind::FloatingPoint, live_fp_regs);
    LOGT(RENAME, "restore speculative rename checkpoints and rebuild free lists");
}

void RegisterRenameUnit::restore_checkpoint(const Checkpoint& checkpoint,
                                            const std::vector<PhysRegNum>& live_physical_regs) {
    restore_checkpoint(checkpoint, live_physical_regs, {});
}

void RegisterRenameUnit::restore_checkpoint(const Checkpoint& checkpoint,
                                            const std::vector<PhysRegNum>& live_physical_regs,
                                            const std::vector<PhysRegNum>& live_fp_physical_regs) {
    rename_table = checkpoint.rename_table;
    fp_rename_table = checkpoint.fp_rename_table;

    std::set<PhysRegNum> live_regs;
    std::set<PhysRegNum> live_fp_regs;
    for (const auto reg : arch_map) {
        live_regs.insert(reg);
    }
    for (const auto reg : fp_arch_map) {
        live_fp_regs.insert(reg);
    }
    for (const auto& entry : rename_table) {
        if (entry.valid) {
            live_regs.insert(entry.physical_reg);
        }
    }
    for (const auto& entry : fp_rename_table) {
        if (entry.valid) {
            live_fp_regs.insert(entry.physical_reg);
        }
    }
    for (const auto reg : live_physical_regs) {
        if (reg != 0) {
            live_regs.insert(reg);
        }
    }
    for (const auto reg : live_fp_physical_regs) {
        live_fp_regs.insert(reg);
    }

    rebuild_free_list_excluding(RegisterFileKind::Integer, live_regs);
    rebuild_free_list_excluding(RegisterFileKind::FloatingPoint, live_fp_regs);
    LOGT(RENAME, "restore speculative rename checkpoint and rebuild both free lists from surviving live regs");
}

void RegisterRenameUnit::commit_instruction(RegNum logical_reg, PhysRegNum physical_reg) {
    commit_instruction(RegisterFileKind::Integer, logical_reg, physical_reg);
}

void RegisterRenameUnit::commit_instruction(RegisterFileKind kind,
                                            RegNum logical_reg,
                                            PhysRegNum physical_reg) {
    if (kind == RegisterFileKind::None) {
        return;
    }
    if (kind == RegisterFileKind::Integer && logical_reg == 0) {
        return;
    }

    auto& arch_ref = arch_map_for_kind(kind);
    auto& rename_ref = table_for_kind(kind);
    const PhysRegNum old_arch_reg = arch_ref[logical_reg];
    arch_ref[logical_reg] = physical_reg;

    if (rename_ref[logical_reg].physical_reg == physical_reg ||
        rename_ref[logical_reg].physical_reg == old_arch_reg) {
        rename_ref[logical_reg].physical_reg = physical_reg;
        rename_ref[logical_reg].valid = true;
        LOGT(RENAME, "on commit update %s table[%d] -> %s%d",
             kind == RegisterFileKind::FloatingPoint ? "fp" : "int",
             static_cast<int>(logical_reg),
             kind == RegisterFileKind::FloatingPoint ? "fp" : "p",
             static_cast<int>(physical_reg));
    }

    if (old_arch_reg >= NUM_LOGICAL_REGS) {
        release_physical_register(kind, old_arch_reg);
    }

    LOGT(RENAME, "commit: architectural %s%d -> %s%d",
         kind == RegisterFileKind::FloatingPoint ? "f" : "x",
         static_cast<int>(logical_reg),
         kind == RegisterFileKind::FloatingPoint ? "fp" : "p",
         static_cast<int>(physical_reg));
}

void RegisterRenameUnit::get_statistics(uint64_t& renames, uint64_t& stalls) const {
    renames = rename_count;
    stalls = stall_count;
}

void RegisterRenameUnit::update_architecture_register(RegNum logical_reg, uint64_t value) {
    update_architecture_register(RegisterFileKind::Integer, logical_reg, value);
}

void RegisterRenameUnit::update_architecture_register(RegisterFileKind kind,
                                                      RegNum logical_reg,
                                                      uint64_t value) {
    if (kind == RegisterFileKind::None) {
        return;
    }
    if (kind == RegisterFileKind::Integer && logical_reg == 0) {
        return;
    }

    const PhysRegNum current_arch_reg = arch_map_for_kind(kind)[logical_reg];
    auto& physical_ref = physicals_for_kind(kind);
    if (current_arch_reg < NUM_LOGICAL_REGS) {
        physical_ref[current_arch_reg].value = value;
    }

    LOGT(RENAME, "update architectural %s%d = 0x%" PRIx64,
         kind == RegisterFileKind::FloatingPoint ? "f" : "x",
         static_cast<int>(logical_reg),
         value);
}

bool RegisterRenameUnit::has_free_register() const {
    return !free_list.empty();
}

size_t RegisterRenameUnit::get_free_register_count() const {
    return free_list.size();
}

void RegisterRenameUnit::dump_rename_table() const {
    LOGT(RENAME, "rename table");
    for (int i = 0; i < NUM_LOGICAL_REGS; ++i) {
        LOGT(RENAME, "x%d -> p%d", i, static_cast<int>(rename_table[i].physical_reg));
    }
    for (int i = 0; i < NUM_LOGICAL_FP_REGS; ++i) {
        LOGT(RENAME, "f%d -> fp%d", i, static_cast<int>(fp_rename_table[i].physical_reg));
    }
}

void RegisterRenameUnit::dump_physical_registers() const {
    LOGT(RENAME, "integer physical register state");
    for (int i = 0; i < NUM_PHYSICAL_REGS && i < 64; ++i) {
        if (physical_registers[i].ready) {
            LOGT(RENAME, "p%d:0x%" PRIx64, i, physical_registers[i].value);
        } else {
            LOGT(RENAME, "p%d: pending", i);
        }
        if (i % 4 == 3) {
            LOGT(RENAME, "---");
        }
    }

    LOGT(RENAME, "floating-point physical register state");
    for (int i = 0; i < NUM_PHYSICAL_REGS && i < 64; ++i) {
        if (fp_physical_registers[i].ready) {
            LOGT(RENAME, "fp%d:0x%" PRIx64, i, fp_physical_registers[i].value);
        } else {
            LOGT(RENAME, "fp%d: pending", i);
        }
        if (i % 4 == 3) {
            LOGT(RENAME, "---");
        }
    }
    LOGT(RENAME, "---");
}

void RegisterRenameUnit::dump_free_list() const {
    LOGT(RENAME, "free integer register count: %zu", free_list.size());
    LOGT(RENAME, "free floating-point register count: %zu", fp_free_list.size());
}

} // namespace riscv
