#include "cpu/ooo/cache/blocking_cache.h"

#include "core/memory.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace riscv {

namespace {

bool isPowerOfTwo(size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

bool rangesOverlap(uint64_t lhs_addr, uint64_t lhs_size, uint64_t rhs_addr, uint64_t rhs_size) {
    const uint64_t lhs_end = lhs_addr + lhs_size - 1;
    const uint64_t rhs_end = rhs_addr + rhs_size - 1;
    return lhs_addr <= rhs_end && rhs_addr <= lhs_end;
}

} // namespace

BlockingCache::BlockingCache(const BlockingCacheConfig& config) : config_(config) {
    if (config_.line_size_bytes == 0 || config_.associativity == 0 || config_.size_bytes == 0) {
        throw std::invalid_argument("cache config cannot contain zero values");
    }
    if ((config_.size_bytes % (config_.line_size_bytes * config_.associativity)) != 0) {
        throw std::invalid_argument("cache size must be divisible by line_size * associativity");
    }
    if (config_.hit_latency <= 0 || config_.miss_penalty < 0 ||
        config_.max_outstanding_misses == 0) {
        throw std::invalid_argument("cache latency config is invalid");
    }

    set_count_ = config_.size_bytes / (config_.line_size_bytes * config_.associativity);
    if (!isPowerOfTwo(set_count_)) {
        throw std::invalid_argument("set count must be power of two");
    }

    sets_.resize(set_count_);
    for (auto& set : sets_) {
        set.resize(config_.associativity);
        for (auto& line : set) {
            line.data.assign(config_.line_size_bytes, 0);
        }
    }
}

CacheAccessResult BlockingCache::access(std::shared_ptr<Memory> memory,
                                        uint64_t address,
                                        uint8_t size,
                                        CacheAccessType access_type) {
    return ensureResident(std::move(memory), address, size, access_type, /*model_timing=*/true);
}

CacheAccessResult BlockingCache::load(std::shared_ptr<Memory> memory,
                                      uint64_t address,
                                      uint8_t size,
                                      uint64_t& value) {
    value = 0;
    if (size == 0) {
        return CacheAccessResult{};
    }

    if (isBypassAccess(memory, address, size)) {
        value = readMemoryValue(memory, address, size);
        CacheAccessResult bypass{};
        bypass.hit = true;
        bypass.latency_cycles = config_.hit_latency;
        return bypass;
    }

    auto result = ensureResident(memory, address, size, CacheAccessType::Read, /*model_timing=*/true);
    if (result.blocked) {
        return result;
    }

    for (uint8_t i = 0; i < size; ++i) {
        value |= static_cast<uint64_t>(readCacheOrPendingByte(address + i)) << (8U * i);
    }
    return result;
}

CacheAccessResult BlockingCache::fetchInstruction(std::shared_ptr<Memory> memory,
                                                  uint64_t address,
                                                  Instruction& instruction) {
    if ((address & 0x1ULL) != 0) {
        throw MemoryException("指令地址必须2字节对齐: 0x" + std::to_string(address));
    }

    uint64_t first_half_raw = 0;
    // 先取低16位：既能直接得到压缩指令，也能判断是否需要再取高16位。
    auto first_result = load(memory, address, /*size=*/2, first_half_raw);
    if (first_result.blocked) {
        return first_result;
    }

    const uint16_t first_half = static_cast<uint16_t>(first_half_raw & 0xFFFFU);
    // RISC-V C扩展约定：最低2位 != 0b11 表示16位压缩指令。
    if ((first_half & 0x03U) != 0x03U) {
        instruction = static_cast<uint32_t>(first_half);
        return first_result;
    }

    uint64_t second_half_raw = 0;
    auto second_result = load(memory, address + 2, /*size=*/2, second_half_raw);
    if (second_result.blocked) {
        const uint64_t first_line = address / config_.line_size_bytes;
        const uint64_t second_line = (address + 2) / config_.line_size_bytes;
        // 特例：第一拍是 miss 且两半指令其实同一条 cache line。
        // 此时第一拍 miss 已把整行装入cache，第二拍若仅因 in-flight miss 被阻塞，可直接回读高16位。
        if (!first_result.hit && first_line == second_line) {
            const uint16_t recovered_second_half = static_cast<uint16_t>(
                static_cast<uint16_t>(readCacheOrPendingByte(address + 2)) |
                (static_cast<uint16_t>(readCacheOrPendingByte(address + 3)) << 8U));
            instruction = static_cast<uint32_t>(first_half) |
                          (static_cast<uint32_t>(recovered_second_half) << 16U);
            return first_result;
        }
        return second_result;
    }

    const uint16_t second_half = static_cast<uint16_t>(second_half_raw & 0xFFFFU);
    instruction = static_cast<uint32_t>(first_half) | (static_cast<uint32_t>(second_half) << 16U);

    CacheAccessResult merged{};
    merged.hit = first_result.hit && second_result.hit;
    // 一条32位指令由两次2B访问组成，总延迟取两者较慢者，命中需两次都命中。
    merged.latency_cycles = std::max(first_result.latency_cycles, second_result.latency_cycles);
    merged.dirty_eviction = first_result.dirty_eviction || second_result.dirty_eviction;
    return merged;
}

void BlockingCache::commitStore(std::shared_ptr<Memory> memory, uint64_t address, uint8_t size, uint64_t value) {
    if (size == 0) {
        return;
    }

    if (isBypassAccess(memory, address, size)) {
        writeMemoryValue(memory, address, size, value);
        return;
    }

    completePendingFills(enumerateLineAddresses(address, size));

    // 提交阶段写入不应引入流水线阻塞，因此采用非时序驻留检查。
    (void)ensureResident(memory, address, size, CacheAccessType::Write, /*model_timing=*/false);

    for (uint8_t i = 0; i < size; ++i) {
        const uint8_t byte_value = static_cast<uint8_t>((value >> (8U * i)) & 0xFFU);
        writeCachedByte(address + i, byte_value, /*mark_dirty=*/true);
    }

    // 当前阶段保持主存镜像一致，避免其他尚未接入cache路径读取陈旧值。
    writeMemoryValue(memory, address, size, value);
}

void BlockingCache::invalidateRange(uint64_t address, uint64_t size) {
    if (size == 0) {
        return;
    }

    const uint64_t start_line = address / config_.line_size_bytes;
    const uint64_t end_line = (address + size - 1) / config_.line_size_bytes;
    for (uint64_t line_address = start_line; line_address <= end_line; ++line_address) {
        cancelPendingFill(line_address);
        CacheLine* line = findLine(line_address);
        if (line) {
            line->valid = false;
            line->dirty = false;
            line->prefetched = false;
            line->fill_pending = false;
            line->tag = 0;
            line->lru_stamp = 0;
            std::fill(line->data.begin(), line->data.end(), 0);
        }
    }
}

void BlockingCache::tick() {
    if (mshr_entries_.empty()) {
        return;
    }

    for (auto& entry : mshr_entries_) {
        if (entry.remaining_cycles > 0) {
            --entry.remaining_cycles;
        }
        if (entry.remaining_cycles == 0) {
            completeMshrFill(entry);
        }
    }
    mshr_entries_.erase(
        std::remove_if(mshr_entries_.begin(),
                       mshr_entries_.end(),
                       [](const MshrEntry& entry) { return entry.remaining_cycles == 0; }),
        mshr_entries_.end());
}

void BlockingCache::flushInFlight() {
    for (const auto& entry : mshr_entries_) {
        completeMshrFill(entry);
    }
    mshr_entries_.clear();
}

void BlockingCache::resetStats() {
    stats_ = BlockingCacheStats{};
}

void BlockingCache::reset() {
    flushInFlight();
    lru_clock_ = 0;
    resetStats();
    for (auto& set : sets_) {
        for (auto& line : set) {
            line.valid = false;
            line.dirty = false;
            line.prefetched = false;
            line.fill_pending = false;
            line.tag = 0;
            line.lru_stamp = 0;
            std::fill(line.data.begin(), line.data.end(), 0);
        }
    }
}

bool BlockingCache::isBypassAccess(const std::shared_ptr<Memory>& memory, uint64_t address, uint8_t size) const {
    if (!memory || size == 0) {
        return true;
    }

    // tohost/fromhost 地址属于MMIO，直接旁路缓存。
    if (rangesOverlap(address, size, memory->getTohostAddr(), 8) ||
        rangesOverlap(address, size, memory->getFromhostAddr(), 8)) {
        return true;
    }
    return false;
}

CacheAccessResult BlockingCache::ensureResident(std::shared_ptr<Memory> memory,
                                                uint64_t address,
                                                uint8_t size,
                                                CacheAccessType access_type,
                                                bool model_timing) {
    (void)access_type;
    CacheAccessResult result{};

    if (size == 0) {
        return result;
    }
    if (!memory) {
        throw SimulatorException("cache access requires memory");
    }

    if (isBypassAccess(memory, address, size)) {
        result.hit = true;
        result.latency_cycles = config_.hit_latency;
        return result;
    }

    const auto line_addresses = enumerateLineAddresses(address, size);
    const bool all_lines_hit = wouldReadyHit(address, size);
    const int pending_fill_cycles = model_timing ? pendingFillRemainingCycles(line_addresses) : 0;
    if (pending_fill_cycles > 0) {
        bool dirty_eviction = false;
        for (const uint64_t line_address : line_addresses) {
            MshrEntry* entry = findMshrEntry(line_address);
            if (entry == nullptr || entry->has_demand_waiter) {
                continue;
            }
            entry->has_demand_waiter = true;
            entry->mark_prefetched_on_fill = false;
            if (CacheLine* line = findLine(line_address)) {
                line->prefetched = false;
            } else {
                DeferredWriteback victim_writeback;
                bool line_dirty_eviction = false;
                CacheLine* reserved = installLine(
                    memory,
                    line_address,
                    line_dirty_eviction,
                    /*mark_prefetched=*/false,
                    /*fill_pending=*/true,
                    &victim_writeback);
                if (reserved == nullptr) {
                    result.blocked = true;
                    result.blocked_by_outstanding_limit = true;
                    return result;
                }
                touchLine(*reserved);
                entry->victim_writeback = std::move(victim_writeback);
                dirty_eviction = dirty_eviction || line_dirty_eviction;
            }
            stats_.prefetch_useful_hits++;
        }
        result.hit = false;
        result.latency_cycles = pending_fill_cycles;
        result.merged_pending_fill = true;
        result.dirty_eviction = dirty_eviction;
        return result;
    }

    size_t new_miss_lines = 0;
    if (!all_lines_hit) {
        for (const auto line_address : line_addresses) {
            const CacheLine* line = findLine(line_address);
            if (line != nullptr && !line->fill_pending) {
                continue;
            }

            ++new_miss_lines;
            if (model_timing && !hasAllocatableLine(line_address)) {
                (void)cancelOnePendingPrefetchInSet(lineToSetIndex(line_address));
            }
            if (model_timing && !hasAllocatableLine(line_address)) {
                result.blocked = true;
                result.blocked_by_outstanding_limit = true;
                return result;
            }
        }

        if (model_timing) {
            if (demandMshrCount() + new_miss_lines > config_.max_outstanding_misses) {
                result.blocked = true;
                result.blocked_by_outstanding_limit = true;
                result.blocked_hit = false;
                return result;
            }
        }
    }

    bool overall_hit = true;
    bool dirty_eviction = false;

    for (const auto line_address : line_addresses) {
        CacheLine* line = findLine(line_address);
        if (line) {
            if (line->prefetched) {
                stats_.prefetch_useful_hits++;
                line->prefetched = false;
            }
            touchLine(*line);
            continue;
        }

        overall_hit = false;
        if (model_timing) {
            if (!startLineFill(memory, line_address, dirty_eviction)) {
                result.blocked = true;
                result.blocked_by_outstanding_limit = true;
                return result;
            }
            continue;
        }

        CacheLine* allocated =
            installLine(memory, line_address, dirty_eviction, /*mark_prefetched=*/false, /*fill_pending=*/false);
        if (allocated == nullptr) {
            result.blocked = true;
            result.blocked_by_outstanding_limit = true;
            return result;
        }
        touchLine(*allocated);
    }

    result.hit = overall_hit;
    result.dirty_eviction = dirty_eviction;
    result.latency_cycles = config_.hit_latency + (overall_hit ? 0 : config_.miss_penalty);

    if (model_timing && !overall_hit) {
        maybeIssueNextLinePrefetch(memory, line_addresses.back());
    }
    return result;
}

int BlockingCache::missServiceRemainingCycles() const {
    if (mshr_entries_.empty()) {
        return 0;
    }
    const auto miss_it = std::min_element(
        mshr_entries_.begin(),
        mshr_entries_.end(),
        [](const MshrEntry& lhs, const MshrEntry& rhs) {
            return lhs.remaining_cycles < rhs.remaining_cycles;
        });
    return miss_it->remaining_cycles;
}

std::vector<uint64_t> BlockingCache::enumerateLineAddresses(uint64_t address, uint8_t size) const {
    if (size == 0) {
        throw SimulatorException("cache access size must be non-zero");
    }

    const uint64_t size_minus_one = static_cast<uint64_t>(size) - 1;
    if (address > std::numeric_limits<uint64_t>::max() - size_minus_one) {
        throw SimulatorException("cache access wraps address space");
    }

    const uint64_t start_line = address / config_.line_size_bytes;
    const uint64_t end_line = (address + size_minus_one) / config_.line_size_bytes;

    std::vector<uint64_t> line_addresses;
    line_addresses.reserve(static_cast<size_t>(end_line - start_line + 1));
    for (uint64_t line = start_line; line <= end_line; ++line) {
        line_addresses.push_back(line);
    }
    return line_addresses;
}

size_t BlockingCache::lineToSetIndex(uint64_t line_address) const {
    return static_cast<size_t>(line_address & (set_count_ - 1));
}

uint64_t BlockingCache::lineToTag(uint64_t line_address) const {
    return line_address / set_count_;
}

uint64_t BlockingCache::lineToBaseAddress(uint64_t line_address) const {
    return line_address * config_.line_size_bytes;
}

BlockingCache::CacheLine* BlockingCache::findLine(uint64_t line_address) {
    const size_t set_index = lineToSetIndex(line_address);
    const uint64_t tag = lineToTag(line_address);
    auto& set = sets_[set_index];
    for (auto& line : set) {
        if (line.valid && line.tag == tag) {
            return &line;
        }
    }
    return nullptr;
}

const BlockingCache::CacheLine* BlockingCache::findLine(uint64_t line_address) const {
    const size_t set_index = lineToSetIndex(line_address);
    const uint64_t tag = lineToTag(line_address);
    const auto& set = sets_[set_index];
    for (const auto& line : set) {
        if (line.valid && line.tag == tag) {
            return &line;
        }
    }
    return nullptr;
}

BlockingCache::MshrEntry* BlockingCache::findMshrEntry(uint64_t line_address) {
    auto it = std::find_if(
        mshr_entries_.begin(),
        mshr_entries_.end(),
        [line_address](const MshrEntry& entry) { return entry.line_address == line_address; });
    return it == mshr_entries_.end() ? nullptr : &*it;
}

const BlockingCache::MshrEntry* BlockingCache::findMshrEntry(uint64_t line_address) const {
    auto it = std::find_if(
        mshr_entries_.begin(),
        mshr_entries_.end(),
        [line_address](const MshrEntry& entry) { return entry.line_address == line_address; });
    return it == mshr_entries_.end() ? nullptr : &*it;
}

bool BlockingCache::isLinePendingFill(uint64_t line_address) const {
    const CacheLine* line = findLine(line_address);
    return line != nullptr && line->fill_pending;
}

size_t BlockingCache::demandMshrCount() const {
    return static_cast<size_t>(std::count_if(
        mshr_entries_.begin(),
        mshr_entries_.end(),
        [](const MshrEntry& entry) { return entry.has_demand_waiter; }));
}

size_t BlockingCache::prefetchMshrCount() const {
    return static_cast<size_t>(std::count_if(
        mshr_entries_.begin(),
        mshr_entries_.end(),
        [](const MshrEntry& entry) { return !entry.has_demand_waiter; }));
}

int BlockingCache::pendingFillRemainingCycles(const std::vector<uint64_t>& line_addresses) const {
    int remaining_cycles = 0;
    for (const uint64_t line_address : line_addresses) {
        if (const MshrEntry* entry = findMshrEntry(line_address)) {
            remaining_cycles = std::max(remaining_cycles, entry->remaining_cycles);
        }
    }
    return remaining_cycles;
}

bool BlockingCache::wouldReadyHit(uint64_t address, uint8_t size) const {
    if (size == 0) {
        return false;
    }

    const uint64_t size_minus_one = static_cast<uint64_t>(size) - 1;
    if (address > std::numeric_limits<uint64_t>::max() - size_minus_one) {
        return false;
    }

    const uint64_t start_line = address / config_.line_size_bytes;
    const uint64_t end_line = (address + size_minus_one) / config_.line_size_bytes;
    for (uint64_t line = start_line; line <= end_line; ++line) {
        if (findLine(line) == nullptr || isLinePendingFill(line)) {
            return false;
        }
    }
    return true;
}

bool BlockingCache::hasAllocatableLine(uint64_t line_address) const {
    const size_t set_index = lineToSetIndex(line_address);
    const auto& set = sets_[set_index];
    return std::any_of(set.begin(), set.end(), [](const CacheLine& line) {
        return !line.valid || !line.fill_pending;
    });
}

bool BlockingCache::hasCleanAllocatableLine(uint64_t line_address) const {
    const size_t set_index = lineToSetIndex(line_address);
    const auto& set = sets_[set_index];
    return std::any_of(set.begin(), set.end(), [](const CacheLine& line) {
        return !line.valid || (!line.fill_pending && !line.dirty);
    });
}

BlockingCache::CacheLine* BlockingCache::allocateLine(uint64_t line_address, bool& dirty_eviction) {
    const size_t set_index = lineToSetIndex(line_address);
    auto& set = sets_[set_index];

    for (auto& line : set) {
        if (!line.valid) {
            return &line;
        }
    }

    auto victim_it = std::min_element(
        set.begin(), set.end(), [](const CacheLine& lhs, const CacheLine& rhs) {
            if (lhs.fill_pending != rhs.fill_pending) {
                return !lhs.fill_pending;
            }
            return lhs.lru_stamp < rhs.lru_stamp;
        });

    if (victim_it == set.end() || victim_it->fill_pending) {
        return nullptr;
    }

    if (victim_it->valid && victim_it->dirty) {
        dirty_eviction = true;
    }
    return &*victim_it;
}

BlockingCache::CacheLine* BlockingCache::allocateCleanLine(uint64_t line_address) {
    const size_t set_index = lineToSetIndex(line_address);
    auto& set = sets_[set_index];

    for (auto& line : set) {
        if (!line.valid) {
            return &line;
        }
    }

    auto victim_it = std::min_element(
        set.begin(), set.end(), [](const CacheLine& lhs, const CacheLine& rhs) {
            const bool lhs_eligible = !lhs.fill_pending && !lhs.dirty;
            const bool rhs_eligible = !rhs.fill_pending && !rhs.dirty;
            if (lhs_eligible != rhs_eligible) {
                return lhs_eligible;
            }
            return lhs.lru_stamp < rhs.lru_stamp;
        });

    if (victim_it == set.end() || victim_it->fill_pending || victim_it->dirty) {
        return nullptr;
    }
    return &*victim_it;
}

BlockingCache::CacheLine* BlockingCache::installLine(const std::shared_ptr<Memory>& memory,
                                                     uint64_t line_address,
                                                     bool& dirty_eviction,
                                                     bool mark_prefetched,
                                                     bool fill_pending,
                                                     DeferredWriteback* deferred_writeback) {
    CacheLine* allocated = allocateLine(line_address, dirty_eviction);
    if (!allocated) {
        return nullptr;
    }

    if (allocated->valid && allocated->prefetched) {
        stats_.prefetch_unused_evictions++;
    }
    if (allocated->valid && allocated->dirty) {
        const uint64_t victim_line_addr = allocated->tag * set_count_ + lineToSetIndex(line_address);
        if (deferred_writeback != nullptr) {
            deferred_writeback->valid = true;
            deferred_writeback->line_address = victim_line_addr;
            deferred_writeback->data = allocated->data;
        } else {
            writebackLineToMemory(memory, victim_line_addr, *allocated);
        }
    }

    allocated->valid = true;
    allocated->dirty = false;
    allocated->prefetched = mark_prefetched;
    allocated->tag = lineToTag(line_address);
    allocated->fill_pending = fill_pending;
    if (fill_pending) {
        std::fill(allocated->data.begin(), allocated->data.end(), 0);
    } else {
        fillLineFromMemory(memory, line_address, *allocated);
    }
    return allocated;
}

bool BlockingCache::startLineFill(const std::shared_ptr<Memory>& memory,
                                  uint64_t line_address,
                                  bool& dirty_eviction) {
    if (findMshrEntry(line_address) != nullptr) {
        return true;
    }

    DeferredWriteback victim_writeback;
    CacheLine* reserved = installLine(
        memory, line_address, dirty_eviction, /*mark_prefetched=*/false, /*fill_pending=*/true, &victim_writeback);
    if (reserved == nullptr) {
        return false;
    }

    touchLine(*reserved);
    MshrEntry entry{};
    entry.line_address = line_address;
    entry.remaining_cycles = config_.hit_latency + config_.miss_penalty;
    entry.has_demand_waiter = true;
    entry.mark_prefetched_on_fill = false;
    entry.memory = memory;
    entry.fill_data = readLineDataFromMemory(memory, line_address);
    entry.victim_writeback = std::move(victim_writeback);
    mshr_entries_.push_back(std::move(entry));
    return true;
}

bool BlockingCache::startPrefetchFill(const std::shared_ptr<Memory>& memory, uint64_t line_address) {
    if (findMshrEntry(line_address) != nullptr || prefetchMshrCount() >= config_.max_outstanding_prefetches) {
        return false;
    }
    if (!hasCleanAllocatableLine(line_address)) {
        return false;
    }

    MshrEntry entry{};
    entry.line_address = line_address;
    entry.remaining_cycles = config_.hit_latency + config_.miss_penalty;
    entry.has_demand_waiter = false;
    entry.mark_prefetched_on_fill = true;
    entry.memory = memory;
    entry.fill_data = readLineDataFromMemory(memory, line_address);
    mshr_entries_.push_back(std::move(entry));
    return true;
}

void BlockingCache::completeMshrFill(const MshrEntry& entry) {
    if (entry.victim_writeback.valid && entry.memory) {
        writebackDataToMemory(entry.memory, entry.victim_writeback.line_address, entry.victim_writeback.data);
    }

    CacheLine* line = findLine(entry.line_address);
    if (line == nullptr) {
        if (entry.has_demand_waiter) {
            bool dirty_eviction = false;
            line = allocateLine(entry.line_address, dirty_eviction);
            if (line == nullptr) {
                return;
            }
            if (line->valid && line->prefetched) {
                stats_.prefetch_unused_evictions++;
            }
            if (line->valid && line->dirty && entry.memory) {
                const uint64_t victim_line_addr = line->tag * set_count_ + lineToSetIndex(entry.line_address);
                writebackLineToMemory(entry.memory, victim_line_addr, *line);
            }
        } else {
            line = allocateCleanLine(entry.line_address);
            if (line == nullptr) {
                return;
            }
            if (line->valid && line->prefetched) {
                stats_.prefetch_unused_evictions++;
            }
        }

        line->valid = true;
        line->tag = lineToTag(entry.line_address);
    }

    line->data = entry.fill_data;
    line->dirty = false;
    line->prefetched = entry.mark_prefetched_on_fill;
    line->fill_pending = false;
    touchLine(*line);
}

void BlockingCache::completePendingFills(const std::vector<uint64_t>& line_addresses) {
    for (const uint64_t line_address : line_addresses) {
        auto it = std::find_if(
            mshr_entries_.begin(),
            mshr_entries_.end(),
            [line_address](const MshrEntry& entry) { return entry.line_address == line_address; });
        if (it == mshr_entries_.end()) {
            continue;
        }

        completeMshrFill(*it);
        mshr_entries_.erase(it);
    }
}

void BlockingCache::cancelPendingFill(uint64_t line_address) {
    auto it = mshr_entries_.begin();
    while (it != mshr_entries_.end()) {
        if (it->line_address != line_address) {
            ++it;
            continue;
        }

        if (it->victim_writeback.valid && it->memory) {
            writebackDataToMemory(it->memory, it->victim_writeback.line_address, it->victim_writeback.data);
        }
        it = mshr_entries_.erase(it);
    }
}

bool BlockingCache::cancelOnePendingPrefetchInSet(size_t set_index) {
    auto it = std::find_if(
        mshr_entries_.begin(),
        mshr_entries_.end(),
        [this, set_index](const MshrEntry& entry) {
            return !entry.has_demand_waiter && lineToSetIndex(entry.line_address) == set_index;
        });
    if (it == mshr_entries_.end()) {
        return false;
    }

    if (CacheLine* line = findLine(it->line_address)) {
        line->valid = false;
        line->dirty = false;
        line->prefetched = false;
        line->fill_pending = false;
        line->tag = 0;
        line->lru_stamp = 0;
        std::fill(line->data.begin(), line->data.end(), 0);
    }
    stats_.prefetch_unused_evictions++;
    mshr_entries_.erase(it);
    return true;
}

void BlockingCache::maybeIssueNextLinePrefetch(const std::shared_ptr<Memory>& memory,
                                               uint64_t demand_line_address) {
    if (!config_.enable_next_line_prefetch || !memory) {
        return;
    }

    stats_.prefetch_requests++;

    const uint64_t next_line_address = demand_line_address + 1;
    const uint64_t next_line_base = lineToBaseAddress(next_line_address);
    if (next_line_base >= memory->getSize() || isBypassAccess(memory, next_line_base, /*size=*/1)) {
        return;
    }

    if (findLine(next_line_address) != nullptr || findMshrEntry(next_line_address) != nullptr) {
        stats_.prefetch_dropped_already_resident++;
        return;
    }

    const size_t target_set_index = lineToSetIndex(next_line_address);
    if (countUnusedPrefetchedLinesInSet(target_set_index) >= 1) {
        stats_.prefetch_dropped_set_throttle++;
        return;
    }

    if (demandMshrCount() >= config_.max_outstanding_misses ||
        !startPrefetchFill(memory, next_line_address)) {
        stats_.prefetch_dropped_set_throttle++;
        return;
    }
    stats_.prefetch_issued++;
}

size_t BlockingCache::countUnusedPrefetchedLinesInSet(size_t set_index) const {
    size_t count = 0;
    const auto& set = sets_.at(set_index);
    for (const auto& line : set) {
        if (line.valid && line.prefetched) {
            ++count;
        }
    }
    return count;
}

void BlockingCache::touchLine(CacheLine& line) {
    line.lru_stamp = ++lru_clock_;
}

uint64_t BlockingCache::readMemoryValue(const std::shared_ptr<Memory>& memory, uint64_t address, uint8_t size) {
    switch (size) {
        case 1:
            return memory->readByte(address);
        case 2:
            return memory->readHalfWord(address);
        case 4:
            return memory->readWord(address);
        case 8:
            return memory->read64(address);
        default:
            throw SimulatorException("unsupported memory read size: " + std::to_string(size));
    }
}

void BlockingCache::writeMemoryValue(const std::shared_ptr<Memory>& memory,
                                     uint64_t address,
                                     uint8_t size,
                                     uint64_t value) {
    switch (size) {
        case 1:
            memory->writeByte(address, static_cast<uint8_t>(value & 0xFFU));
            return;
        case 2:
            memory->writeHalfWord(address, static_cast<uint16_t>(value & 0xFFFFU));
            return;
        case 4:
            memory->writeWord(address, static_cast<uint32_t>(value & 0xFFFFFFFFULL));
            return;
        case 8:
            memory->write64(address, value);
            return;
        default:
            throw SimulatorException("unsupported memory write size: " + std::to_string(size));
    }
}

std::vector<uint8_t> BlockingCache::readLineDataFromMemory(const std::shared_ptr<Memory>& memory,
                                                           uint64_t line_address) const {
    std::vector<uint8_t> data(config_.line_size_bytes, 0);
    const uint64_t line_base = lineToBaseAddress(line_address);
    const uint64_t memory_size = memory->getSize();
    for (size_t i = 0; i < config_.line_size_bytes; ++i) {
        const uint64_t addr = line_base + i;
        data[i] = (addr < memory_size) ? memory->readByte(addr) : 0;
    }
    return data;
}

void BlockingCache::fillLineFromMemory(const std::shared_ptr<Memory>& memory, uint64_t line_address, CacheLine& line) {
    line.data = readLineDataFromMemory(memory, line_address);
}

void BlockingCache::writebackDataToMemory(const std::shared_ptr<Memory>& memory,
                                          uint64_t line_address,
                                          const std::vector<uint8_t>& data) {
    const uint64_t line_base = lineToBaseAddress(line_address);
    const uint64_t memory_size = memory->getSize();
    const size_t write_size = std::min(config_.line_size_bytes, data.size());
    for (size_t i = 0; i < write_size; ++i) {
        const uint64_t addr = line_base + i;
        if (addr < memory_size) {
            memory->writeByte(addr, data[i]);
        }
    }
}

void BlockingCache::writebackLineToMemory(const std::shared_ptr<Memory>& memory,
                                          uint64_t line_address,
                                          const CacheLine& line) {
    writebackDataToMemory(memory, line_address, line.data);
}

uint8_t BlockingCache::readCacheOrPendingByte(uint64_t address) const {
    const uint64_t line_address = address / config_.line_size_bytes;
    const size_t offset = static_cast<size_t>(address % config_.line_size_bytes);

    const CacheLine* line = findLine(line_address);
    if (line && !line->fill_pending) {
        return line->data[offset];
    }

    if (const MshrEntry* entry = findMshrEntry(line_address)) {
        if (offset >= entry->fill_data.size()) {
            throw SimulatorException("pending fill data offset out of range");
        }
        return entry->fill_data[offset];
    }

    throw SimulatorException("cache read byte miss after ensureResident");
}

uint8_t BlockingCache::readCachedByte(uint64_t address) const {
    const uint64_t line_address = address / config_.line_size_bytes;
    const size_t set_index = lineToSetIndex(line_address);
    const uint64_t tag = lineToTag(line_address);
    const size_t offset = static_cast<size_t>(address % config_.line_size_bytes);

    const auto& set = sets_[set_index];
    for (const auto& line : set) {
        if (line.valid && line.tag == tag) {
            return line.data[offset];
        }
    }
    throw SimulatorException("cache read byte miss after ensureResident");
}

void BlockingCache::writeCachedByte(uint64_t address, uint8_t value, bool mark_dirty) {
    const uint64_t line_address = address / config_.line_size_bytes;
    const size_t set_index = lineToSetIndex(line_address);
    const uint64_t tag = lineToTag(line_address);
    const size_t offset = static_cast<size_t>(address % config_.line_size_bytes);

    auto& set = sets_[set_index];
    for (auto& line : set) {
        if (line.valid && line.tag == tag) {
            line.data[offset] = value;
            if (mark_dirty) {
                line.dirty = true;
            }
            touchLine(line);
            return;
        }
    }
    throw SimulatorException("cache write byte miss after ensureResident");
}

} // namespace riscv
