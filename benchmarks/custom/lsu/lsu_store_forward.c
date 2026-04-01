#include <stdint.h>

#include "bench_common.h"

#define K_SLOTS 256
#define K_ITERS 200000

static volatile uint64_t slots[K_SLOTS] __attribute__((aligned(64)));

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    for (int i = 0; i < K_SLOTS; ++i) {
        slots[i] = 0;
    }

    uint64_t checksum = 0;
    uint64_t expected = 0;

    setStats(1);
    for (int i = 0; i < K_ITERS; ++i) {
        const int idx = i & (K_SLOTS - 1);
        const uint64_t value = (uint64_t)i ^ 0x5a5a5a5aULL;
        slots[idx] = value;
        checksum += slots[idx];
        expected += value;
    }
    setStats(0);

    if (checksum != expected) {
        return bench_report_fail("lsu_store_forward", expected, checksum);
    }
    return bench_report_pass("lsu_store_forward", checksum);
}
