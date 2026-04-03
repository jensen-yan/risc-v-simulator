#include <stdint.h>

#include "bench_common.h"

#define K_SLOTS 128
#define K_ITERS 4096

static volatile uint64_t target_slots[K_SLOTS] __attribute__((aligned(64)));
static volatile uint64_t safe_slots[K_SLOTS] __attribute__((aligned(64)));
static volatile uint64_t delay_sink;

__attribute__((noinline)) static volatile uint64_t* delayed_slot_ptr(volatile uint64_t* base,
                                                                     int idx,
                                                                     uint64_t token) {
    uint64_t x = token | 1ULL;
    x = x * 6364136223846793005ULL + 1442695040888963407ULL;
    x ^= x >> 17;
    x = x * 0xbf58476d1ce4e5b9ULL + 0x94d049bb133111ebULL;
    delay_sink ^= x;
    return base + idx;
}

__attribute__((optimize("O2,no-tree-tail-merge,no-crossjumping,no-code-hoisting"))) int main(
    int argc,
    char** argv) {
    (void)argc;
    (void)argv;

    for (int i = 0; i < K_SLOTS; ++i) {
        target_slots[i] = 0x1000000000000000ULL | (uint64_t)i;
        safe_slots[i] = 0x2000000000000000ULL | (uint64_t)i;
    }

    uint64_t expected_checksum = 0;
    uint64_t checksum = 0;
    volatile uint64_t expected_value = 0;

    setStats(1);
    for (int i = 0; i < K_ITERS; ++i) {
        const int idx = (i * 7) & (K_SLOTS - 1);
        const int bad_phase = ((i >> 2) & 1);
        const uint64_t old_target = target_slots[idx];
        const uint64_t token = ((uint64_t)i << 32) ^ old_target ^ delay_sink;
        const uint64_t safe_value = 0xabc0000000000000ULL ^ ((uint64_t)i * 0x10101ULL);
        const uint64_t bad_value = 0x1350000000000000ULL ^ ((uint64_t)i * 0x1001001ULL);

        if (bad_phase) {
            *delayed_slot_ptr(target_slots, idx, token) = bad_value;
            expected_value = bad_value;
            expected_checksum += bad_value;
            goto observe_target;
        } else {
            *delayed_slot_ptr(safe_slots, idx, token) = safe_value;
            expected_value = old_target;
            expected_checksum += old_target;
        }

observe_target:
        asm volatile("" ::: "memory");
        const uint64_t observed = target_slots[idx];
        const uint64_t expected_now = expected_value;
        checksum += observed;
        if (observed != expected_now) {
            return bench_report_fail("lsu_pair_alias", expected_now, observed);
        }
    }
    setStats(0);

    if (checksum != expected_checksum) {
        return bench_report_fail("lsu_pair_alias.checksum", expected_checksum, checksum);
    }

    return bench_report_pass("lsu_pair_alias", checksum ^ delay_sink);
}
