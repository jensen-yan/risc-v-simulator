#include "coremark.h"
#include <stdbool.h>
#include <stdarg.h>

static CORE_TICKS start_val;
static CORE_TICKS stop_val;

ee_u32 default_num_contexts = 1;

static inline CORE_TICKS read_mcycle(void) {
    CORE_TICKS value;
    __asm__ volatile("csrr %0, mcycle" : "=r"(value));
    return value;
}

void start_time(void) {
    start_val = read_mcycle();
}

void stop_time(void) {
    stop_val = read_mcycle();
}

CORE_TICKS get_time(void) {
    return stop_val - start_val;
}

secs_ret time_in_secs(CORE_TICKS ticks) {
    return ((secs_ret)ticks) / ((secs_ret)EE_TICKS_PER_SEC);
}

void portable_init(core_portable *p, int *argc, char *argv[]) {
    (void)argc;
    (void)argv;
    p->portable_id = 1;
}

void portable_fini(core_portable *p) {
    p->portable_id = 0;
}

static int ee_puts(const char *s) {
    int count = 0;
    if (!s) {
        s = "(null)";
    }
    while (*s) {
        putchar(*s++);
        ++count;
    }
    return count;
}

static int ee_put_uint(
    unsigned long long value,
    unsigned base,
    bool upper,
    int min_width,
    char pad_char
) {
    static const char *kDigitsLower = "0123456789abcdef";
    static const char *kDigitsUpper = "0123456789ABCDEF";
    const char *digits = upper ? kDigitsUpper : kDigitsLower;
    char buf[32];
    int idx = 0;
    int count = 0;

    if (base < 2 || base > 16) {
        return 0;
    }

    if (value == 0) {
        buf[idx++] = '0';
    } else {
        while (value > 0 && idx < (int)sizeof(buf)) {
            buf[idx++] = digits[value % base];
            value /= base;
        }
    }

    while (idx < min_width) {
        putchar(pad_char);
        ++count;
        --min_width;
    }

    while (idx > 0) {
        putchar(buf[--idx]);
        ++count;
    }
    return count;
}

static int ee_put_int(long long value, int min_width, char pad_char) {
    int count = 0;
    unsigned long long abs_val;

    if (value < 0) {
        putchar('-');
        ++count;
        if (min_width > 0) {
            --min_width;
        }
        abs_val = (unsigned long long)(-(value + 1)) + 1ULL;
    } else {
        abs_val = (unsigned long long)value;
    }
    count += ee_put_uint(abs_val, 10, false, min_width, pad_char);
    return count;
}

static int ee_put_float(double value) {
    int count = 0;
    if (value < 0) {
        putchar('-');
        ++count;
        value = -value;
    }

    const unsigned long long integer = (unsigned long long)value;
    double fractional = value - (double)integer;
    count += ee_put_uint(integer, 10, false, 0, ' ');

    putchar('.');
    ++count;

    /* 输出 6 位小数，满足 CoreMark 输出需求 */
    for (int i = 0; i < 6; ++i) {
        fractional *= 10.0;
        int digit = (int)fractional;
        if (digit < 0) {
            digit = 0;
        } else if (digit > 9) {
            digit = 9;
        }
        putchar((char)('0' + digit));
        ++count;
        fractional -= (double)digit;
    }
    return count;
}

int ee_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int written = 0;

    while (*fmt) {
        if (*fmt != '%') {
            putchar(*fmt++);
            ++written;
            continue;
        }

        ++fmt; /* skip '%' */
        if (*fmt == '%') {
            putchar('%');
            ++fmt;
            ++written;
            continue;
        }

        int long_count = 0;
        char pad_char = ' ';
        int min_width = 0;

        if (*fmt == '0') {
            pad_char = '0';
            ++fmt;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            min_width = min_width * 10 + (*fmt - '0');
            ++fmt;
        }

        while (*fmt == 'l') {
            ++long_count;
            ++fmt;
        }

        switch (*fmt) {
            case 'd':
            case 'i':
                if (long_count >= 2) {
                    written += ee_put_int(va_arg(ap, long long), min_width, pad_char);
                } else if (long_count == 1) {
                    written += ee_put_int(va_arg(ap, long), min_width, pad_char);
                } else {
                    written += ee_put_int(va_arg(ap, int), min_width, pad_char);
                }
                break;
            case 'u':
                if (long_count >= 2) {
                    written += ee_put_uint(va_arg(ap, unsigned long long), 10, false, min_width, pad_char);
                } else if (long_count == 1) {
                    written += ee_put_uint(va_arg(ap, unsigned long), 10, false, min_width, pad_char);
                } else {
                    written += ee_put_uint(va_arg(ap, unsigned int), 10, false, min_width, pad_char);
                }
                break;
            case 'x':
                if (long_count >= 2) {
                    written += ee_put_uint(va_arg(ap, unsigned long long), 16, false, min_width, pad_char);
                } else if (long_count == 1) {
                    written += ee_put_uint(va_arg(ap, unsigned long), 16, false, min_width, pad_char);
                } else {
                    written += ee_put_uint(va_arg(ap, unsigned int), 16, false, min_width, pad_char);
                }
                break;
            case 'X':
                if (long_count >= 2) {
                    written += ee_put_uint(va_arg(ap, unsigned long long), 16, true, min_width, pad_char);
                } else if (long_count == 1) {
                    written += ee_put_uint(va_arg(ap, unsigned long), 16, true, min_width, pad_char);
                } else {
                    written += ee_put_uint(va_arg(ap, unsigned int), 16, true, min_width, pad_char);
                }
                break;
            case 'p':
                written += ee_puts("0x");
                written += ee_put_uint((unsigned long long)(uintptr_t)va_arg(ap, void *), 16, false, 0, ' ');
                break;
            case 's':
                written += ee_puts(va_arg(ap, const char *));
                break;
            case 'c':
                putchar((char)va_arg(ap, int));
                ++written;
                break;
            case 'f':
                written += ee_put_float(va_arg(ap, double));
                break;
            default:
                putchar('%');
                putchar(*fmt);
                written += 2;
                break;
        }

        if (*fmt) {
            ++fmt;
        }
    }

    va_end(ap);
    return written;
}
