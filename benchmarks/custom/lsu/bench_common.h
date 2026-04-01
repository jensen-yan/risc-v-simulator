#pragma once

#include <stdint.h>
#include <stdio.h>

#include "util.h"

static inline int bench_report_pass(const char* name, uint64_t checksum) {
    printf("[%s] checksum=%llu\n", name, (unsigned long long)checksum);
    printf("=== TEST RESULT: PASS ===\n");
    return 0;
}

static inline int bench_report_fail(const char* name, uint64_t expected, uint64_t actual) {
    printf("[%s] expected=%llu actual=%llu\n",
           name,
           (unsigned long long)expected,
           (unsigned long long)actual);
    printf("=== TEST RESULT: FAIL ===\n");
    return 1;
}
