#include <stdint.h>

#include "bench_common.h"

#define K_NODES 8192
#define K_MASK (K_NODES - 1)
#define K_STRIDE 257
#define K_ROUNDS 64

static uint32_t next_idx[K_NODES] __attribute__((aligned(64)));

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    for (int i = 0; i < K_NODES; ++i) {
        next_idx[i] = (uint32_t)((i + K_STRIDE) & K_MASK);
    }

    uint32_t idx = 0;
    uint64_t checksum = 0;

    setStats(1);
    for (int round = 0; round < K_ROUNDS; ++round) {
        for (int i = 0; i < K_NODES; ++i) {
            idx = next_idx[idx];
            checksum += idx;
        }
    }
    setStats(0);

    if (idx != 0) {
        return bench_report_fail("lsu_pointer_chase", 0, idx);
    }
    return bench_report_pass("lsu_pointer_chase", checksum);
}
