#include "cpu/ooo/stages/fetch_stage.h"
#include "cpu/ooo/branch_predictor.h"
#include "common/debug_types.h"
#include <algorithm>
#include <fmt/format.h>

namespace riscv {

namespace {

[[noreturn]] void throwTranslationFault(Address virtual_address,
                                       size_t size,
                                       const TranslationResult& result) {
    throw MemoryException("instruction fetch translation failed for va=0x" +
                          std::to_string(virtual_address) + " size=" + std::to_string(size) +
                          ": " + result.message);
}

Address translateInstructionAddress(const FetchStage::Context& context, Address virtual_address, size_t size) {
    return context.translateInstructionAddress(virtual_address, size);
}

Instruction fetchTranslatedInstruction(const FetchStage::Context& context, Address virtual_pc) {
    const Address first_half_pa = translateInstructionAddress(context, virtual_pc, /*size=*/2);
    const uint16_t first_half = context.memory()->readHalfWord(first_half_pa);
    if ((first_half & 0x03U) != 0x03U) {
        return static_cast<Instruction>(first_half);
    }

    const Address second_half_pa = translateInstructionAddress(context, virtual_pc + 2, /*size=*/2);
    const uint16_t second_half = context.memory()->readHalfWord(second_half_pa);
    return static_cast<Instruction>(first_half) | (static_cast<Instruction>(second_half) << 16);
}

CacheAccessResult fetchInstructionFromCache(const FetchStage::Context& context,
                                            Address virtual_pc,
                                            Instruction& instruction) {
    uint64_t first_half_raw = 0;
    auto* l1i_cache = context.l1iCache();
    const Address first_half_pa = translateInstructionAddress(context, virtual_pc, /*size=*/2);
    auto first_result = l1i_cache->load(context.memory(), first_half_pa, /*size=*/2, first_half_raw);
    if (first_result.blocked) {
        return first_result;
    }

    const uint16_t first_half = static_cast<uint16_t>(first_half_raw & 0xFFFFU);
    if ((first_half & 0x03U) != 0x03U) {
        instruction = static_cast<Instruction>(first_half);
        return first_result;
    }

    uint64_t second_half_raw = 0;
    const Address second_half_pa = translateInstructionAddress(context, virtual_pc + 2, /*size=*/2);
    auto second_result = l1i_cache->load(context.memory(), second_half_pa, /*size=*/2, second_half_raw);
    if (second_result.blocked) {
        const uint64_t first_line = first_half_pa / l1i_cache->getConfig().line_size_bytes;
        const uint64_t second_line = second_half_pa / l1i_cache->getConfig().line_size_bytes;
        // 第一拍 miss 已把整条 cache line 装入 I$。如果第二个半字仍在同一条 line，
        // 则无需把整次取指记成 blocked；直接功能性回读高 16 位即可保持 miss 记账正确。
        if (!first_result.hit && first_line == second_line) {
            second_half_raw = context.memory()->readHalfWord(second_half_pa);
        } else {
            return second_result;
        }
    }

    const uint16_t second_half = static_cast<uint16_t>(second_half_raw & 0xFFFFU);
    instruction = static_cast<Instruction>(first_half) | (static_cast<Instruction>(second_half) << 16U);

    CacheAccessResult merged{};
    merged.hit = first_result.hit && second_result.hit;
    merged.latency_cycles = std::max(first_result.latency_cycles, second_result.latency_cycles);
    merged.blocked = false;
    merged.dirty_eviction = first_result.dirty_eviction || second_result.dirty_eviction;
    return merged;
}

} // namespace

FetchStage::FetchStage() {
    // 构造函数：初始化取指阶段
}

bool FetchStage::Context::anyExecutionUnitBusy() const {
    const auto has_busy_unit = [](const auto& units) {
        return std::any_of(units.begin(), units.end(), [](const ExecutionUnit& unit) {
            return unit.busy;
        });
    };

    return has_busy_unit(state_.alu_units) ||
           has_busy_unit(state_.fp_units) ||
           has_busy_unit(state_.branch_units) ||
           has_busy_unit(state_.load_units) ||
           has_busy_unit(state_.store_units);
}

Address FetchStage::Context::translateInstructionAddress(Address virtual_address, size_t size) const {
    const auto result = state_.address_translation->translateInstructionAddress(virtual_address, size);
    if (!result.success) {
        throwTranslationFault(virtual_address, size, result);
    }
    return result.physical_address;
}

void FetchStage::execute(Context& context) {
    // 如果已经停机，不再取指
    if (context.isHalted()) {
        LOGT(FETCH, "cpu halted, skip fetch");
        return;
    }

    // I$ miss等待：倒计时到0的这个周期允许继续取指，避免多等一个周期。
    if (context.hasIcacheMissWait()) {
        const bool still_waiting = context.advanceIcacheMissWaitCycle();
        context.incrementCounter(PerfCounterId::CACHE_L1I_STALL_CYCLES);
        if (still_waiting) {
            LOGT(FETCH, "icache waiting, remaining=%d", context.remainingIcacheWaitCycles());
            return;
        }
        LOGT(FETCH, "icache wait completed, resume fetch");
    }

    context.incrementCounter(PerfCounterId::FETCH_SLOTS, OOOPipelineConfig::FETCH_WIDTH);

    auto try_halt_if_drained = [&]() {
        if (context.reorderBufferEmpty() &&
            context.fetchBufferEmpty() &&
            context.cdbQueueEmpty()) {
            context.setHalted(true);
            LOGT(FETCH, "pipeline drained, program finished");
        }
    };

    if (context.fetchBufferSize() >= MAX_FETCH_BUFFER_SIZE) {
        LOGT(FETCH, "fetch buffer full(size=%zu), skip fetch", context.fetchBufferSize());
        context.recordPipelineStall(PerfCounterId::STALL_FETCH_BUFFER_FULL);
    } else {
        uint64_t next_fetch_pc = context.pc();
        size_t fetched_this_cycle = 0;

        for (size_t slot = 0;
             slot < OOOPipelineConfig::FETCH_WIDTH && context.fetchBufferSize() < MAX_FETCH_BUFFER_SIZE;
             ++slot) {
            try {
                const uint64_t fetch_pc = next_fetch_pc;
                Instruction raw_inst = 0;
                const bool use_pending_icache_request = context.consumePendingIcacheIfMatch(fetch_pc, raw_inst);
                if (use_pending_icache_request) {
                    LOGT(FETCH, "reuse resolved icache miss request, pc=0x%" PRIx64, fetch_pc);
                } else if (context.l1iCache()) {
                    Instruction fetched_inst = 0;
                    const auto cache_result = fetchInstructionFromCache(context, fetch_pc, fetched_inst);
                    if (cache_result.blocked) {
                        context.incrementCounter(PerfCounterId::CACHE_L1I_STALL_CYCLES);
                        context.setPc(next_fetch_pc);
                        LOGT(FETCH, "icache blocked by in-flight miss, pc=0x%" PRIx64, fetch_pc);
                        return;
                    }

                    context.incrementCounter(PerfCounterId::CACHE_L1I_ACCESSES);
                    if (cache_result.hit) {
                        context.incrementCounter(PerfCounterId::CACHE_L1I_HITS);
                    } else {
                        context.incrementCounter(PerfCounterId::CACHE_L1I_MISSES);
                        context.startIcacheMissWait(fetch_pc, fetched_inst, cache_result.latency_cycles);
                        context.setPc(next_fetch_pc);
                        LOGT(FETCH, "icache miss: pc=0x%" PRIx64 ", latency=%d, wait=%d",
                             fetch_pc, cache_result.latency_cycles, context.remainingIcacheWaitCycles());
                        if (context.hasIcacheMissWait()) {
                            return;
                        }
                    }

                    raw_inst = fetched_inst;
                    context.resetIcache();
                } else {
                    raw_inst = fetchTranslatedInstruction(context, fetch_pc);
                }

                if (raw_inst == 0) {
                    context.setPc(next_fetch_pc);
                    LOGT(FETCH, "fetched zero instruction, stop fetching and wait for pipeline drain");
                    try_halt_if_drained();
                    return;
                }

                FetchedInstruction fetched;
                fetched.pc = fetch_pc;
                fetched.instruction = raw_inst;
                fetched.has_branch_meta = false;
                fetched.is_compressed = ((raw_inst & 0x03) != 0x03);

                const uint64_t fallthrough = fetch_pc + (fetched.is_compressed ? 2ULL : 4ULL);
                uint64_t predicted_next_pc = fallthrough;

                bool decoded_ok = false;
                DecodedInstruction decoded{};
                try {
                    if (fetched.is_compressed) {
                        decoded = context.decoder().decodeCompressed(
                            static_cast<uint16_t>(raw_inst), context.enabledExtensions());
                    } else {
                        decoded = context.decoder().decode(raw_inst, context.enabledExtensions());
                    }
                    decoded_ok = true;
                } catch (const SimulatorException&) {
                    decoded_ok = false;
                }

                if (decoded_ok && context.branchPredictor()) {
                    if (decoded.opcode == Opcode::BRANCH || decoded.opcode == Opcode::JALR) {
                        fetched.ras_checkpoint = context.branchPredictor()->captureRasCheckpoint();
                        fetched.has_ras_checkpoint = true;
                    }
                    const auto pred = context.branchPredictor()->predict(fetch_pc, decoded, fallthrough);
                    predicted_next_pc = pred.next_pc;

                    if (decoded.opcode == Opcode::JALR) {
                        if (pred.ras_used) {
                            context.incrementCounter(PerfCounterId::PREDICTOR_RAS_LOOKUPS);
                            if (pred.ras_hit) {
                                context.incrementCounter(PerfCounterId::PREDICTOR_RAS_HITS);
                            } else {
                                context.incrementCounter(PerfCounterId::PREDICTOR_RAS_MISSES);
                            }
                        }
                        if (pred.btb_used) {
                            context.incrementCounter(PerfCounterId::PREDICTOR_BTB_LOOKUPS);
                            if (pred.btb_hit) {
                                context.incrementCounter(PerfCounterId::PREDICTOR_BTB_HITS);
                            } else {
                                context.incrementCounter(PerfCounterId::PREDICTOR_BTB_MISSES);
                            }
                        }
                    } else if (decoded.opcode == Opcode::BRANCH) {
                        context.incrementCounter(PerfCounterId::PREDICTOR_BHT_LOOKUPS);
                        fetched.branch_meta = pred.branch_meta;
                        fetched.has_branch_meta = pred.branch_meta.valid;
                        if (fetched.has_branch_meta) {
                            context.incrementCounter(PerfCounterId::PREDICTOR_TOURNAMENT_LOCAL_LOOKUPS);
                            context.incrementCounter(PerfCounterId::PREDICTOR_TOURNAMENT_GLOBAL_LOOKUPS);
                            if (pred.branch_meta.loop_prediction_available) {
                                context.incrementCounter(PerfCounterId::PREDICTOR_LOOP_LOOKUPS);
                            }
                            if (pred.branch_meta.loop_override_used) {
                                context.incrementCounter(PerfCounterId::PREDICTOR_LOOP_OVERRIDES);
                            }
                            if (pred.branch_meta.chooser_use_global) {
                                context.incrementCounter(PerfCounterId::PREDICTOR_TOURNAMENT_CHOOSE_GLOBAL);
                            } else {
                                context.incrementCounter(PerfCounterId::PREDICTOR_TOURNAMENT_CHOOSE_LOCAL);
                            }
                        }
                    }
                }

                fetched.predicted_next_pc = predicted_next_pc;
                fetched.fetch_cycle = context.cycleCount();
                next_fetch_pc = predicted_next_pc;

                LOGT(FETCH, "fetch: slot=%zu pc=0x%" PRIx64 " inst=0x%" PRIx32 " (%s) predicted_next_pc=0x%" PRIx64,
                     slot, fetched.pc, raw_inst,
                     (fetched.is_compressed ? "compressed" : "normal"),
                     fetched.predicted_next_pc);

                context.pushFetchedInstruction(fetched);
                context.incrementCounter(PerfCounterId::FETCHED_INSTRUCTIONS);
                fetched_this_cycle++;

                if (predicted_next_pc != fallthrough) {
                    break;
                }
            } catch (const MemoryException& e) {
                context.setPc(next_fetch_pc);
                LOGW(FETCH, "fetch failed, stop fetching and wait for pipeline drain: %s", e.what());
                try_halt_if_drained();
                return;
            }
        }

        context.setPc(next_fetch_pc);
        context.incrementCounter(PerfCounterId::FETCH_UTILIZED_SLOTS, fetched_this_cycle);
    }
    
    // 每个周期结束时仅在流水线完全排空时停机。
    // 不能再用 pc >= memory_size 之类的物理地址假设，因为 Sv39/高虚拟地址
    // checkpoint 在合法执行过程中也会出现远大于 memory_size 的 PC。
    if (context.reorderBufferEmpty() &&
        context.fetchBufferEmpty() &&
        context.cdbQueueEmpty() &&
        !context.hasIcacheMissWait()) {
        if (!context.anyExecutionUnitBusy()) {
            context.setHalted(true);
            LOGT(FETCH, "all instructions completed, halt cpu");
        }
    }
}

} // namespace riscv 
