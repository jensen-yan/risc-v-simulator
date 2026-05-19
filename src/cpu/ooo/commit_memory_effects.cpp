#include "cpu/ooo/commit_memory_effects.h"

#include "common/debug_types.h"
#include "core/instruction_executor.h"

#include <memory>
#include <utility>

namespace riscv {

namespace {

uint8_t atomicWidthToSize(Funct3 width) {
    switch (width) {
        case Funct3::LW:
            return 4;
        case Funct3::LD:
            return 8;
        default:
            throw IllegalInstructionException("A扩展仅支持W/D宽度");
    }
}

void writeAtomicMemoryValue(std::shared_ptr<Memory> memory,
                            uint64_t addr,
                            Funct3 width,
                            uint64_t value) {
    switch (width) {
        case Funct3::LW:
            memory->writeWord(addr, static_cast<uint32_t>(value));
            return;
        case Funct3::LD:
            memory->write64(addr, value);
            return;
        default:
            throw IllegalInstructionException("A扩展仅支持W/D宽度");
    }
}

CommitMemoryEffects::Result failedResult(std::string error_message) {
    CommitMemoryEffects::Result result;
    result.success = false;
    result.applied = false;
    result.error_message = std::move(error_message);
    return result;
}

} // namespace

CommitMemoryEffects::Result CommitMemoryEffects::applyStore(
    CPUState& state,
    const DynamicInstPtr& instruction) {
    const auto& decoded_info = instruction->get_decoded_info();
    const auto& memory_info = instruction->get_memory_info();
    if (!memory_info.address_ready || !memory_info.is_store) {
        return failedResult("store commit missing memory info");
    }

    const uint8_t store_size = memory_info.memory_size != 0
        ? memory_info.memory_size
        : decoded_info.memory_access_size;

    if (state.l1d_cache) {
        state.l1d_cache->commitStore(
            state.memory, memory_info.memory_address, store_size, memory_info.memory_value);
    } else if (decoded_info.opcode == Opcode::STORE_FP) {
        InstructionExecutor::storeFPToMemory(
            state.memory, memory_info.memory_address, memory_info.memory_value, decoded_info.funct3);
    } else {
        InstructionExecutor::storeToMemory(
            state.memory, memory_info.memory_address, memory_info.memory_value, decoded_info.funct3);
    }

    state.reservation_valid = false;
    state.perf_counters.increment(PerfCounterId::STORES_COMMITTED);
    LOGT(COMMIT, "inst=%" PRId64 " commit store addr=0x%" PRIx64 " value=0x%" PRIx64,
         instruction->get_instruction_id(), memory_info.memory_address, memory_info.memory_value);

    Result result;
    result.applied = true;
    return result;
}

CommitMemoryEffects::Result CommitMemoryEffects::applyAmo(
    CPUState& state,
    const DynamicInstPtr& instruction) {
    if (!instruction->has_atomic_execute_info()) {
        return failedResult("amo commit missing execute info");
    }

    const auto& atomic_info = instruction->get_atomic_execute_info();
    if (atomic_info.acquire_reservation) {
        state.reservation_valid = true;
        state.reservation_addr = atomic_info.virtual_address;
    }
    if (atomic_info.release_reservation) {
        state.reservation_valid = false;
    }
    if (atomic_info.do_store) {
        if (state.l1d_cache) {
            const uint8_t store_size = atomicWidthToSize(atomic_info.width);
            state.l1d_cache->commitStore(
                state.memory, atomic_info.physical_address, store_size, atomic_info.store_value);
        } else {
            writeAtomicMemoryValue(
                state.memory,
                atomic_info.physical_address,
                atomic_info.width,
                atomic_info.store_value);
        }
        LOGT(COMMIT, "inst=%" PRId64 " commit amo store addr=0x%" PRIx64 " value=0x%" PRIx64,
             instruction->get_instruction_id(),
             atomic_info.physical_address,
             atomic_info.store_value);
    }
    state.perf_counters.increment(PerfCounterId::AMOS_COMMITTED);

    Result result;
    result.applied = true;
    return result;
}

CommitMemoryEffects::Result CommitMemoryEffects::apply(CPUState& state,
                                                       const DynamicInstPtr& instruction) {
    if (!instruction) {
        return {};
    }

    const auto& decoded_info = instruction->get_decoded_info();
    if (decoded_info.opcode == Opcode::STORE || decoded_info.opcode == Opcode::STORE_FP) {
        return applyStore(state, instruction);
    }
    if (decoded_info.opcode == Opcode::AMO) {
        return applyAmo(state, instruction);
    }
    return {};
}

} // namespace riscv
