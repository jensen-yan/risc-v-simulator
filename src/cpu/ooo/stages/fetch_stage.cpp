#include "cpu/ooo/stages/fetch_stage.h"
#include "cpu/ooo/branch_predictor.h"
#include "common/debug_types.h"
#include <algorithm>
#include <fmt/format.h>

namespace riscv {

FetchStage::FetchStage() {
    // 构造函数：初始化取指阶段
}

void FetchStage::execute(CPUState& state) {
    // 如果已经停机，不再取指
    if (state.halted) {
        LOGT(FETCH, "cpu halted, skip fetch");
        return;
    }

    // I$ miss等待：倒计时到0的这个周期允许继续取指，避免多等一个周期。
    if (state.icache.wait_cycles > 0) {
        --state.icache.wait_cycles;
        state.perf_counters.increment(PerfCounterId::CACHE_L1I_STALL_CYCLES);
        if (state.icache.wait_cycles > 0) {
            LOGT(FETCH, "icache waiting, remaining=%d", state.icache.wait_cycles);
            return;
        }
        LOGT(FETCH, "icache wait completed, resume fetch");
    }
    
    // 如果取指缓冲区有空间，取指令
    if (state.fetch_buffer.size() < MAX_FETCH_BUFFER_SIZE) {
        try {
            const uint64_t fetch_pc = state.pc;
            Instruction raw_inst = 0;
            const bool use_pending_icache_request = state.icache.consumeIfMatch(fetch_pc, raw_inst);
            if (use_pending_icache_request) {
                LOGT(FETCH, "reuse resolved icache miss request, pc=0x%" PRIx64, fetch_pc);
            } else if (state.l1i_cache) {
                Instruction fetched_inst = 0;
                const auto cache_result = state.l1i_cache->fetchInstruction(state.memory, fetch_pc, fetched_inst);
                if (cache_result.blocked) {
                    state.perf_counters.increment(PerfCounterId::CACHE_L1I_STALL_CYCLES);
                    LOGT(FETCH, "icache blocked by in-flight miss, pc=0x%" PRIx64, fetch_pc);
                    return;
                }

                state.perf_counters.increment(PerfCounterId::CACHE_L1I_ACCESSES);
                if (cache_result.hit) {
                    state.perf_counters.increment(PerfCounterId::CACHE_L1I_HITS);
                } else {
                    state.perf_counters.increment(PerfCounterId::CACHE_L1I_MISSES);
                    state.icache.startMissWait(fetch_pc, fetched_inst, cache_result.latency_cycles);
                    LOGT(FETCH, "icache miss: pc=0x%" PRIx64 ", latency=%d, wait=%d",
                         fetch_pc, cache_result.latency_cycles, state.icache.wait_cycles);
                    if (state.icache.wait_cycles > 0) {
                        return;
                    }
                }

                raw_inst = fetched_inst;
                state.icache.reset();
            } else {
                raw_inst = state.memory->fetchInstruction(fetch_pc);
            }
            
            // 如果指令为0，可能表明程序结束，但不要立即停机
            // 要等待流水线中的指令全部完成提交
            if (raw_inst == 0) {
                LOGT(FETCH, "fetched zero instruction, stop fetching and wait for pipeline drain");
                
                // 检查是否还有未完成的指令
                if (state.reorder_buffer->is_empty() && 
                    state.fetch_buffer.empty() && 
                    state.cdb_queue.empty()) {
                    state.halted = true;
                    LOGT(FETCH, "pipeline drained, program finished");
                }
                return;
            }
            
            FetchedInstruction fetched;
            fetched.pc = fetch_pc;
            fetched.instruction = raw_inst;
            
            // 检查是否为压缩指令
            if ((raw_inst & 0x03) != 0x03) {
                fetched.is_compressed = true;
            } else {
                fetched.is_compressed = false;
            }

            const uint64_t fallthrough = fetch_pc + (fetched.is_compressed ? 2ULL : 4ULL);
            uint64_t predicted_next_pc = fallthrough;

            // 最小解码：复用Decoder来识别控制流并计算预测next PC。
            // 注意：这里捕获异常，保持与原先“非法指令在decode阶段暴露”的行为一致。
            bool decoded_ok = false;
            DecodedInstruction decoded{};
            try {
                if (fetched.is_compressed) {
                    decoded = state.decoder.decodeCompressed(static_cast<uint16_t>(raw_inst), state.enabled_extensions);
                } else {
                    decoded = state.decoder.decode(raw_inst, state.enabled_extensions);
                }
                decoded_ok = true;
            } catch (const SimulatorException&) {
                decoded_ok = false;
            }

            if (decoded_ok && state.branch_predictor) {
                const auto pred = state.branch_predictor->predict(fetch_pc, decoded, fallthrough);
                predicted_next_pc = pred.next_pc;

                if (decoded.opcode == Opcode::JALR) {
                    state.perf_counters.increment(PerfCounterId::PREDICTOR_BTB_LOOKUPS);
                    if (pred.btb_hit) {
                        state.perf_counters.increment(PerfCounterId::PREDICTOR_BTB_HITS);
                    } else {
                        state.perf_counters.increment(PerfCounterId::PREDICTOR_BTB_MISSES);
                    }
                } else if (decoded.opcode == Opcode::BRANCH) {
                    state.perf_counters.increment(PerfCounterId::PREDICTOR_BHT_LOOKUPS);
                }
            }

            fetched.predicted_next_pc = predicted_next_pc;
            state.pc = predicted_next_pc;

            LOGT(FETCH, "fetch: pc=0x%" PRIx64 " inst=0x%" PRIx32 " (%s) predicted_next_pc=0x%" PRIx64,
                fetched.pc, raw_inst,
                (fetched.is_compressed ? "compressed" : "normal"),
                fetched.predicted_next_pc);
            
            state.fetch_buffer.push(fetched);
            state.perf_counters.increment(PerfCounterId::FETCHED_INSTRUCTIONS);
            
        } catch (const MemoryException& e) {
            // 取指失败，停止取指但等待流水线清空
            LOGW(FETCH, "fetch failed, stop fetching and wait for pipeline drain: %s", e.what());
            
            // 检查是否还有未完成的指令
            if (state.reorder_buffer->is_empty() && 
                state.fetch_buffer.empty() && 
                state.cdb_queue.empty()) {
                state.halted = true;
                LOGT(FETCH, "pipeline drained, program finished");
            }
            return;
        }
    } else {
        LOGT(FETCH, "fetch buffer full(size=%zu), skip fetch", state.fetch_buffer.size());
        state.recordPipelineStall(PerfCounterId::STALL_FETCH_BUFFER_FULL);
    }
    
    // 每个周期结束时检查是否应该停机
    // 如果没有更多指令可取，且流水线为空，则停机
    if (state.pc >= state.memory->getSize() || 
        (state.reorder_buffer->is_empty() && 
         state.fetch_buffer.empty() && 
         state.cdb_queue.empty())) {
        
        // 检查是否还有任何正在执行的指令
        bool has_busy_units = false;
        for (const auto& unit : state.alu_units) {
            if (unit.busy) has_busy_units = true;
        }
        for (const auto& unit : state.branch_units) {
            if (unit.busy) has_busy_units = true;
        }
        for (const auto& unit : state.load_units) {
            if (unit.busy) has_busy_units = true;
        }
        for (const auto& unit : state.store_units) {
            if (unit.busy) has_busy_units = true;
        }
        
        if (!has_busy_units && state.reorder_buffer->is_empty()) {
            state.halted = true;
            LOGT(FETCH, "all instructions completed, halt cpu");
        }
    }
}

void FetchStage::flush() {
    // 刷新取指阶段状态（例如：清空预取缓冲区等）
    // 在简单实现中，取指缓冲区的清空由主控制器处理
    LOGT(FETCH, "fetch stage flushed");
}

void FetchStage::reset() {
    // 重置取指阶段到初始状态
    LOGT(FETCH, "fetch stage reset");
}

} // namespace riscv 
