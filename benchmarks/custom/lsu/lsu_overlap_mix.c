#include <stdint.h>

#include "bench_common.h"

#define K_SLOTS 256
#define K_ITERS 4096

static volatile uint64_t wide_slots[K_SLOTS] __attribute__((aligned(64)));
static volatile uint64_t overlap_slots[K_SLOTS] __attribute__((aligned(64)));

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    volatile uint32_t* overlap_words = (volatile uint32_t*)overlap_slots;

    for (int i = 0; i < K_SLOTS; ++i) {
        wide_slots[i] = 0;
        overlap_slots[i] = 0xdeadbeef00000000ULL | (uint64_t)i;
    }

    uint64_t expected_partial = 0;
    uint64_t expected_overlap = 0;

    for (int i = 0; i < K_ITERS; ++i) {
        const int idx = i & (K_SLOTS - 1);
        const uint64_t wide_value = 0x1122334455667788ULL ^ ((uint64_t)i * 0x01010101ULL);
        const uint64_t old_overlap_value = overlap_slots[idx];
        const uint32_t new_low = (uint32_t)(i * 17U + 3U);

        wide_slots[idx] = wide_value;
        expected_partial += (uint32_t)wide_value;

        overlap_words[idx * 2] = new_low;
        expected_overlap += (old_overlap_value & 0xffffffff00000000ULL) | (uint64_t)new_low;
    }

    uint64_t checksum_partial = 0;
    uint64_t checksum_overlap = 0;

    setStats(1);
    for (int i = 0; i < K_ITERS; ++i) {
        const int idx = i & (K_SLOTS - 1);
        const uint64_t wide_value = 0x1122334455667788ULL ^ ((uint64_t)i * 0x01010101ULL);
        const uint64_t old_overlap_value = overlap_slots[idx];
        const uint32_t new_low = (uint32_t)(i * 17U + 3U);

        wide_slots[idx] = wide_value;
        checksum_partial += (uint32_t)wide_slots[idx];

        overlap_words[idx * 2] = new_low;
        checksum_overlap += overlap_slots[idx];

        const uint64_t expected_value =
            (old_overlap_value & 0xffffffff00000000ULL) | (uint64_t)new_low;
        if (overlap_slots[idx] != expected_value) {
            return bench_report_fail("lsu_overlap_mix", expected_value, overlap_slots[idx]);
        }
    }
    setStats(0);

    if (checksum_partial != expected_partial) {
        return bench_report_fail("lsu_overlap_mix.partial", expected_partial, checksum_partial);
    }

    if (checksum_overlap != expected_overlap) {
        return bench_report_fail("lsu_overlap_mix.overlap", expected_overlap, checksum_overlap);
    }

    return bench_report_pass("lsu_overlap_mix", checksum_partial ^ checksum_overlap);
}
