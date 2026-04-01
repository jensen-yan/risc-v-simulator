#include <stdint.h>

#include "bench_common.h"

#define K_ARRAY_SIZE 4096
#define K_ROUNDS 256

static uint32_t stream0[K_ARRAY_SIZE] __attribute__((aligned(64)));
static uint32_t stream1[K_ARRAY_SIZE] __attribute__((aligned(64)));
static uint32_t stream2[K_ARRAY_SIZE] __attribute__((aligned(64)));
static uint32_t stream3[K_ARRAY_SIZE] __attribute__((aligned(64)));
static uint32_t stream4[K_ARRAY_SIZE] __attribute__((aligned(64)));
static uint32_t stream5[K_ARRAY_SIZE] __attribute__((aligned(64)));
static uint32_t stream6[K_ARRAY_SIZE] __attribute__((aligned(64)));
static uint32_t stream7[K_ARRAY_SIZE] __attribute__((aligned(64)));

static void init_stream(uint32_t* data, uint32_t seed) {
    for (int i = 0; i < K_ARRAY_SIZE; ++i) {
        data[i] = (uint32_t)(i * 17U + seed);
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    init_stream(stream0, 1);
    init_stream(stream1, 3);
    init_stream(stream2, 5);
    init_stream(stream3, 7);
    init_stream(stream4, 11);
    init_stream(stream5, 13);
    init_stream(stream6, 17);
    init_stream(stream7, 19);

    uint64_t checksum = 0;
    uint64_t expected = 0;

    for (int round = 0; round < K_ROUNDS; ++round) {
        for (int i = 0; i < K_ARRAY_SIZE; ++i) {
            expected += stream0[i] + stream1[i] + stream2[i] + stream3[i];
            expected += stream4[i] + stream5[i] + stream6[i] + stream7[i];
        }
    }

    setStats(1);
    for (int round = 0; round < K_ROUNDS; ++round) {
        for (int i = 0; i < K_ARRAY_SIZE; ++i) {
            checksum += stream0[i] + stream1[i] + stream2[i] + stream3[i];
            checksum += stream4[i] + stream5[i] + stream6[i] + stream7[i];
        }
    }
    setStats(0);

    if (checksum != expected) {
        return bench_report_fail("lsu_mlp", expected, checksum);
    }
    return bench_report_pass("lsu_mlp", checksum);
}
