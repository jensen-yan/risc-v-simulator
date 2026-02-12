#include "cpu/ooo/cache/blocking_cache.h"

#include <algorithm>
#include <stdexcept>

namespace riscv {

namespace {

bool isPowerOfTwo(size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
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
    }
}

CacheAccessResult BlockingCache::access(uint64_t address, uint8_t size, CacheAccessType access_type) {
    CacheAccessResult result{};
    if (miss_in_flight_) {
        result.blocked = true;
        return result;
    }

    const auto line_addresses = enumerateLineAddresses(address, size);
    bool overall_hit = true;
    bool dirty_eviction = false;

    for (const auto line_address : line_addresses) {
        CacheLine* line = findLine(line_address);
        if (line) {
            if (access_type == CacheAccessType::Write &&
                config_.write_policy == CacheWritePolicy::WriteBackWriteAllocate) {
                line->dirty = true;
            }
            touchLine(*line);
            continue;
        }

        overall_hit = false;
        CacheLine& allocated = allocateLine(line_address, dirty_eviction);
        allocated.valid = true;
        allocated.tag = lineToTag(line_address);
        allocated.dirty = (access_type == CacheAccessType::Write &&
                           config_.write_policy == CacheWritePolicy::WriteBackWriteAllocate);
        touchLine(allocated);
    }

    result.hit = overall_hit;
    result.dirty_eviction = dirty_eviction;
    result.latency_cycles = config_.hit_latency + (overall_hit ? 0 : config_.miss_penalty);

    if (!overall_hit) {
        miss_in_flight_ = true;
        miss_service_remaining_cycles_ = result.latency_cycles;
    }

    return result;
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
            line = CacheLine{};
        }
    }
}

std::vector<uint64_t> BlockingCache::enumerateLineAddresses(uint64_t address, uint8_t size) const {
    const uint8_t access_size = size == 0 ? 1 : size;
    const uint64_t start_line = address / config_.line_size_bytes;
    const uint64_t end_line = (address + access_size - 1) / config_.line_size_bytes;

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

} // namespace riscv

