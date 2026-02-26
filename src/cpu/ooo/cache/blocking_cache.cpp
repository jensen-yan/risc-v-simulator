#include "cpu/ooo/cache/blocking_cache.h"

#include "core/memory.h"

#include <algorithm>
#include <stdexcept>

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
    if (config_.hit_latency <= 0 || config_.miss_penalty < 0) {
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
        value |= static_cast<uint64_t>(readCachedByte(address + i)) << (8U * i);
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
                static_cast<uint16_t>(readCachedByte(address + 2)) |
                (static_cast<uint16_t>(readCachedByte(address + 3)) << 8U));
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
        CacheLine* line = findLine(line_address);
        if (line) {
            line->valid = false;
            line->dirty = false;
            line->tag = 0;
            line->lru_stamp = 0;
            std::fill(line->data.begin(), line->data.end(), 0);
        }
    }
}

void BlockingCache::tick() {
    if (!miss_in_flight_) {
        return;
    }

    if (miss_service_remaining_cycles_ > 0) {
        --miss_service_remaining_cycles_;
    }
    if (miss_service_remaining_cycles_ == 0) {
        miss_in_flight_ = false;
    }
}

void BlockingCache::flushInFlight() {
    miss_in_flight_ = false;
    miss_service_remaining_cycles_ = 0;
}

void BlockingCache::reset() {
    flushInFlight();
    lru_clock_ = 0;
    for (auto& set : sets_) {
        for (auto& line : set) {
            line.valid = false;
            line.dirty = false;
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

    if (model_timing && miss_in_flight_) {
        result.blocked = true;
        return result;
    }

    const auto line_addresses = enumerateLineAddresses(address, size);
    bool overall_hit = true;
    bool dirty_eviction = false;

    for (const auto line_address : line_addresses) {
        CacheLine* line = findLine(line_address);
        if (line) {
            touchLine(*line);
            continue;
        }

        overall_hit = false;
        CacheLine& allocated = allocateLine(line_address, dirty_eviction);
        if (allocated.valid && allocated.dirty) {
            const uint64_t victim_line_addr = allocated.tag * set_count_ + lineToSetIndex(line_address);
            writebackLineToMemory(memory, victim_line_addr, allocated);
        }

        allocated.valid = true;
        allocated.dirty = false;
        allocated.tag = lineToTag(line_address);
        fillLineFromMemory(memory, line_address, allocated);
        touchLine(allocated);
    }

    result.hit = overall_hit;
    result.dirty_eviction = dirty_eviction;
    result.latency_cycles = config_.hit_latency + (overall_hit ? 0 : config_.miss_penalty);

    if (model_timing && !overall_hit) {
        miss_in_flight_ = true;
        miss_service_remaining_cycles_ = result.latency_cycles;
    }
    return result;
}

std::vector<uint64_t> BlockingCache::enumerateLineAddresses(uint64_t address, uint8_t size) const {
    const uint64_t start_line = address / config_.line_size_bytes;
    const uint64_t end_line = (address + static_cast<uint64_t>(size) - 1) / config_.line_size_bytes;

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

BlockingCache::CacheLine& BlockingCache::allocateLine(uint64_t line_address, bool& dirty_eviction) {
    const size_t set_index = lineToSetIndex(line_address);
    auto& set = sets_[set_index];

    for (auto& line : set) {
        if (!line.valid) {
            return line;
        }
    }

    auto victim_it = std::min_element(
        set.begin(), set.end(), [](const CacheLine& lhs, const CacheLine& rhs) {
            return lhs.lru_stamp < rhs.lru_stamp;
        });

    if (victim_it->valid && victim_it->dirty) {
        dirty_eviction = true;
    }
    return *victim_it;
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

void BlockingCache::fillLineFromMemory(const std::shared_ptr<Memory>& memory, uint64_t line_address, CacheLine& line) {
    const uint64_t line_base = lineToBaseAddress(line_address);
    const uint64_t memory_size = memory->getSize();
    for (size_t i = 0; i < config_.line_size_bytes; ++i) {
        const uint64_t addr = line_base + i;
        line.data[i] = (addr < memory_size) ? memory->readByte(addr) : 0;
    }
}

void BlockingCache::writebackLineToMemory(const std::shared_ptr<Memory>& memory,
                                          uint64_t line_address,
                                          const CacheLine& line) {
    const uint64_t line_base = lineToBaseAddress(line_address);
    const uint64_t memory_size = memory->getSize();
    for (size_t i = 0; i < config_.line_size_bytes; ++i) {
        const uint64_t addr = line_base + i;
        if (addr < memory_size) {
            memory->writeByte(addr, line.data[i]);
        }
    }
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
