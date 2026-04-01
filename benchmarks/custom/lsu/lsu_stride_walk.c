#include <stdint.h>

#include "bench_common.h"

#define K_ARRAY_SIZE (1 << 15)
#define K_MASK (K_ARRAY_SIZE - 1)
#define K_STRIDE 16
#define K_ROUNDS 64

static uint32_t data[K_ARRAY_SIZE] __attribute__((aligned(64)));

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    for (int i = 0; i < K_ARRAY_SIZE; ++i) {
        data[i] = (uint32_t)(i ^ 0x13579bdfU);
    }

    uint64_t checksum = 0;
    uint64_t expected = 0;
    int idx = 0;

    for (int round = 0; round < K_ROUNDS; ++round) {
        for (int i = 0; i < K_ARRAY_SIZE; ++i) {
            idx = (idx + K_STRIDE) & K_MASK;
            expected += data[idx];
        }
    }

    idx = 0;
    setStats(1);
    for (int round = 0; round < K_ROUNDS; ++round) {
        for (int i = 0; i < K_ARRAY_SIZE; ++i) {
            idx = (idx + K_STRIDE) & K_MASK;
            checksum += data[idx];
        }
    }
    setStats(0);

    if (checksum != expected) {
        return bench_report_fail("lsu_stride_walk", expected, checksum);
    }
    return bench_report_pass("lsu_stride_walk", checksum);
}
