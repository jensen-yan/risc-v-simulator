#ifndef CORE_PORTME_H
#define CORE_PORTME_H

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

/* CoreMark 类型定义 */
typedef signed char ee_s8;
typedef unsigned char ee_u8;
typedef signed short ee_s16;
typedef unsigned short ee_u16;
typedef signed int ee_s32;
typedef unsigned int ee_u32;
typedef unsigned long long ee_u64;
typedef double ee_f32;
typedef uintptr_t ee_ptr_int;
typedef size_t ee_size_t;

/* 按 CoreMark 约定声明秒类型 */
typedef double secs_ret;

#ifndef ITERATIONS
#define ITERATIONS 2000
#endif

#ifndef MULTITHREAD
#define MULTITHREAD 1
#endif

#ifndef PERFORMANCE_RUN
#define PERFORMANCE_RUN 1
#endif

#ifndef VALIDATION_RUN
#define VALIDATION_RUN 0
#endif

#ifndef PROFILE_RUN
#define PROFILE_RUN 0
#endif

#define HAS_FLOAT 1
#define HAS_TIME_H 0
#define USE_CLOCK 0
#define HAS_STDIO 1
#define HAS_PRINTF 0

/*
 * 这里使用 mcycle 作为 tick。
 * 模拟器未定义真实频率，因此默认按 1MHz 折算，
 * 方便输出稳定的 CoreMark/MHz 相对趋势。
 */
#define EE_TICKS_PER_SEC 1000000ULL

typedef ee_u64 CORE_TICKS;

#define COMPILER_VERSION "riscv64-unknown-elf-gcc"
#define COMPILER_FLAGS "-O2 -DPERFORMANCE_RUN=1 -DMEM_METHOD=MEM_STATIC"
#define MEM_LOCATION "STATIC"

#define MAIN_HAS_NOARGC 1

#define ee_sprintf sprintf
int ee_printf(const char *fmt, ...);

/* 计时相关宏 */
#define CORETIMETYPE CORE_TICKS
#define GETMYTIME(_t) (*(_t) = get_time())
#define MYTIMEDIFF(fin, ini) ((fin) - (ini))
#define TIMER_RES_DIVIDER 1
#define SAMPLE_TIME_IMPLEMENTATION 1

/* 对齐辅助，沿用 CoreMark 常见实现 */
#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x)-1) & ~3))

typedef struct core_portable_s {
    ee_u8 portable_id;
} core_portable;

extern ee_u32 default_num_contexts;

void portable_init(core_portable *p, int *argc, char *argv[]);
void portable_fini(core_portable *p);

void start_time(void);
void stop_time(void);
CORE_TICKS get_time(void);
secs_ret time_in_secs(CORE_TICKS ticks);

#endif
