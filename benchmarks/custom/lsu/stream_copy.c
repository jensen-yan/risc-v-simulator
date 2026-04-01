#include <stdint.h>

#include "bench_common.h"

#define K_ARRAY_SIZE 512
#define K_ROUNDS 8

static uint64_t src[K_ARRAY_SIZE] __attribute__((aligned(64)));
static uint64_t dst[K_ARRAY_SIZE] __attribute__((aligned(64)));

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    for (int i = 0; i < K_ARRAY_SIZE; ++i) {
        src[i] = (uint64_t)i * 13ULL + 7ULL;
        dst[i] = 0;
    }

    uint64_t expected = 0;
    for (int round = 0; round < K_ROUNDS; ++round) {
        for (int i = 0; i < K_ARRAY_SIZE; ++i) {
            expected += src[i];
        }
    }

    uint64_t checksum = 0;
    setStats(1);
    for (int round = 0; round < K_ROUNDS; ++round) {
        for (int i = 0; i < K_ARRAY_SIZE; ++i) {
            dst[i] = src[i];
            checksum += dst[i];
        }
    }
    setStats(0);

    if (checksum != expected) {
        return bench_report_fail("stream_copy", expected, checksum);
    }
    return bench_report_pass("stream_copy", checksum);
}
