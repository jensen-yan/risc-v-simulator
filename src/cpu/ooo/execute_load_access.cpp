#include "cpu/ooo/execute_load_access.h"
#include "cpu/ooo/execute_dcache_access.h"
#include "cpu/ooo/execute_load_value.h"
#include "common/debug_types.h"

#include <algorithm>

namespace riscv {

ExecuteLoadAccess::Result ExecuteLoadAccess::perform(ExecutionUnit& unit, CPUState& state) {
    const auto& inst = unit.instruction->get_decoded_info();
    uint64_t addr = unit.load_address;
    uint8_t access_size = unit.load_size;
    auto& memory_info = unit.instruction->get_memory_info();
    const uint8_t full_forward_mask =
        static_cast<uint8_t>(access_size == 8 ? 0xFFu : ((1u << access_size) - 1u));

    if (!unit.dcache.request_sent) {
        const auto forwarding_info = state.store_buffer->analyze_load_forwarding(
            addr, access_size, unit.instruction->get_instruction_id());
        const auto forwarding_kind = forwarding_info.kind;
        const uint64_t forwarded_value = forwarding_info.value;
        const bool needs_memory_merge =
            forwarding_kind == StoreBuffer::LoadForwardingKind::PartialMatch &&
            forwarding_info.byte_mask != 0 &&
            forwarding_info.byte_mask != full_forward_mask;

        if (forwarding_kind == StoreBuffer::LoadForwardingKind::FullMatch ||
            (forwarding_kind == StoreBuffer::LoadForwardingKind::PartialMatch && !needs_memory_merge)) {
            memory_info.store_forwarded = true;
            state.perf_counters.increment(PerfCounterId::LOADS_FORWARDED);
            if (forwarding_kind == StoreBuffer::LoadForwardingKind::FullMatch) {
                state.perf_counters.increment(PerfCounterId::LOADS_FORWARDED_FULL_MATCH);
                memory_info.load_final_source = DynamicInst::MemoryInfo::LoadFinalSource::ForwardedFull;
                for (size_t idx = 0; idx < forwarding_info.contributing_count; ++idx) {
                    forwarding_info.contributing_stores[idx]->get_memory_info().caused_forwarded_full_count++;
                }
            } else {
                state.perf_counters.increment(PerfCounterId::LOADS_FORWARDED_PARTIAL_MATCH);
                memory_info.load_final_source = DynamicInst::MemoryInfo::LoadFinalSource::ForwardedPartial;
                for (size_t idx = 0; idx < forwarding_info.contributing_count; ++idx) {
                    forwarding_info.contributing_stores[idx]->get_memory_info().caused_forwarded_partial_count++;
                }
            }
            if (inst.opcode == Opcode::LOAD_FP) {
                unit.result = ExecuteLoadValue::format(inst, access_size, forwarded_value);
                LOGT(EXECUTE, "fp store-to-load forwarding hit: addr=0x%" PRIx64 " value=0x%" PRIx64,
                     addr, unit.result);
                memory_info.memory_value = unit.result;
                return Result::Forwarded;
            }

            unit.result = ExecuteLoadValue::format(inst, access_size, forwarded_value);

            LOGT(EXECUTE, "store-to-load forwarding hit: addr=0x%" PRIx64 " value=0x%" PRIx64 " %s-extended",
                 addr, unit.result, inst.is_signed_load ? "sign" : "zero");
            memory_info.memory_value = unit.result;
            return Result::Forwarded;
        }

        memory_info.store_forwarded = false;
        memory_info.load_final_source =
            needs_memory_merge ? DynamicInst::MemoryInfo::LoadFinalSource::ForwardedPartial
                               : DynamicInst::MemoryInfo::LoadFinalSource::FromMemory;
        try {
            uint64_t raw_value = 0;
            CacheAccessResult cache_result{};
            if (state.l1d_cache) {
                cache_result = state.l1d_cache->load(state.memory, addr, access_size, raw_value);
            } else {
                switch (access_size) {
                    case 1:
                        raw_value = state.memory->readByte(addr);
                        break;
                    case 2:
                        raw_value = state.memory->readHalfWord(addr);
                        break;
                    case 4:
                        raw_value = state.memory->readWord(addr);
                        break;
                    case 8:
                        raw_value = state.memory->read64(addr);
                        break;
                    default:
                        throw SimulatorException("unsupported load size: " + std::to_string(access_size));
                }
                cache_result.hit = true;
                cache_result.latency_cycles = 1;
            }

            if (cache_result.blocked) {
                unit.dcache.waiting = true;
                unit.remaining_cycles = 1;
                if (cache_result.blocked_by_outstanding_limit) {
                    state.perf_counters.increment(PerfCounterId::CACHE_L1D_BLOCKED_BY_OUTSTANDING_LIMIT);
                }
                if (cache_result.blocked_hit) {
                    state.perf_counters.increment(PerfCounterId::CACHE_L1D_HIT_BLOCKED_BY_OUTSTANDING_LIMIT);
                }
                state.perf_counters.increment(PerfCounterId::CACHE_L1D_STALL_CYCLES_LOAD);
                return Result::WaitingForCache;
            }

            ExecuteDCacheAccess::recordResult(state, CacheAccessType::Read, cache_result);

            if (needs_memory_merge) {
                for (uint8_t byte_index = 0; byte_index < access_size; ++byte_index) {
                    const uint8_t bit = static_cast<uint8_t>(1u << byte_index);
                    if ((forwarding_info.byte_mask & bit) == 0) {
                        continue;
                    }
                    const uint64_t byte_mask = 0xFFull << (byte_index * 8);
                    raw_value &= ~byte_mask;
                    raw_value |= forwarded_value & byte_mask;
                }
                memory_info.store_forwarded = true;
                state.perf_counters.increment(PerfCounterId::LOADS_FORWARDED);
                state.perf_counters.increment(PerfCounterId::LOADS_FORWARDED_PARTIAL_MATCH);
                for (size_t idx = 0; idx < forwarding_info.contributing_count; ++idx) {
                    forwarding_info.contributing_stores[idx]
                        ->get_memory_info()
                        .caused_forwarded_partial_count++;
                }
            } else if (forwarding_kind == StoreBuffer::LoadForwardingKind::BlockedByOverlap) {
                if (forwarding_info.primary_store) {
                    forwarding_info.primary_store->get_memory_info().caused_store_buffer_overlap_block_count++;
                }
                state.perf_counters.increment(PerfCounterId::LOADS_BLOCKED_BY_STORE);
                return Result::BlockedByStore;
            }

            unit.result = ExecuteLoadValue::format(inst, access_size, raw_value);

            memory_info.memory_value = unit.result;
            state.perf_counters.increment(PerfCounterId::LOADS_FROM_MEMORY);
            unit.dcache.request_sent = true;
            unit.dcache.waiting = true;

            const int extra_cycles = std::max(0, cache_result.latency_cycles - 1);
            if (extra_cycles > 0) {
                unit.remaining_cycles = extra_cycles;
                state.perf_counters.increment(
                    PerfCounterId::CACHE_L1D_STALL_CYCLES_LOAD, static_cast<uint64_t>(extra_cycles));
                return Result::WaitingForCache;
            }

            unit.dcache.waiting = false;
            return Result::LoadedFromMemory;
        } catch (const SimulatorException& e) {
            unit.has_exception = true;
            unit.exception_msg = e.what();
            unit.result = 0;
            unit.dcache.waiting = false;
            return Result::Exception;
        }
    }

    unit.dcache.waiting = false;
    return Result::LoadedFromMemory;
}

} // namespace riscv
