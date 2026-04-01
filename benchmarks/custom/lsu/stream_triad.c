#include <stdint.h>

#include "bench_common.h"

#define K_ARRAY_SIZE 512
#define K_ROUNDS 8
#define K_SCALAR 3ULL

static uint64_t a[K_ARRAY_SIZE] __attribute__((aligned(64)));
static uint64_t b[K_ARRAY_SIZE] __attribute__((aligned(64)));
static uint64_t c[K_ARRAY_SIZE] __attribute__((aligned(64)));

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    for (int i = 0; i < K_ARRAY_SIZE; ++i) {
        a[i] = 0;
        b[i] = (uint64_t)(i + 1);
        c[i] = (uint64_t)(2 * i + 3);
    }

    uint64_t expected = 0;
    for (int round = 0; round < K_ROUNDS; ++round) {
        for (int i = 0; i < K_ARRAY_SIZE; ++i) {
            expected += b[i] + K_SCALAR * c[i];
        }
    }

    uint64_t checksum = 0;
    setStats(1);
    for (int round = 0; round < K_ROUNDS; ++round) {
        for (int i = 0; i < K_ARRAY_SIZE; ++i) {
            a[i] = b[i] + K_SCALAR * c[i];
            checksum += a[i];
        }
    }
    setStats(0);

    if (checksum != expected) {
        return bench_report_fail("stream_triad", expected, checksum);
    }
    return bench_report_pass("stream_triad", checksum);
}
