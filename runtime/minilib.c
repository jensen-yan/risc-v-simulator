#include "minilib.h"

// 系统调用号定义
#define SYS_EXIT 93
#define SYS_WRITE 64

// 文件描述符
#define STDOUT 1
#define STDERR 2

// 内联汇编系统调用包装
static inline long syscall1(long n, long a1) {
    register long a0 asm("a0") = a1;
    register long a7 asm("a7") = n;
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return a0;
}

static inline long syscall3(long n, long a1, long a2, long a3) {
    register long a0 asm("a0") = a1;
    register long a1_reg asm("a1") = a2;
    register long a2_reg asm("a2") = a3;
    register long a7 asm("a7") = n;
    asm volatile("ecall" : "+r"(a0) : "r"(a1_reg), "r"(a2_reg), "r"(a7) : "memory");
    return a0;
}

// 基本I/O实现
int putchar(int c) {
    char ch = (char)c;
    return (int)syscall3(SYS_WRITE, STDOUT, (long)&ch, 1);
}

int puts(const char* str) {
    if (!str) return -1;
    
    int len = strlen(str);
    long result = syscall3(SYS_WRITE, STDOUT, (long)str, len);
    putchar('\n');  // puts添加换行符
    return (int)result;
}

void exit(int status) {
    syscall1(SYS_EXIT, status);
    // 不应该执行到这里
    while(1);
}

// 字符串函数实现
int strlen(const char* str) {
    if (!str) return 0;
    int len = 0;
    while (str[len]) len++;
    return len;
}

char* strcpy(char* dest, const char* src) {
    if (!dest || !src) return dest;
    char* d = dest;
    while ((*d++ = *src++) != '\0');
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    if (!s1 || !s2) return 0;
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// 数值转换
int atoi(const char* str) {
    if (!str) return 0;
    int result = 0;
    int sign = 1;
    
    // 跳过空格
    while (*str == ' ' || *str == '\t') str++;
    
    // 处理符号
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    // 转换数字
    while (*str >= '0' && *str <= '9') {
        // 用移位和加法实现乘法: result * 10 = result * 8 + result * 2
        result = (result << 3) + (result << 1) + (*str - '0');
        str++;
    }
    
    return sign < 0 ? -result : result;
}

void itoa(int value, char* str, int base) {
    if (!str || base < 2 || base > 36) return;
    
    char* ptr = str;
    char* ptr1 = str;
    char tmp_char;
    
    // 处理负数
    if (value < 0 && base == 10) {
        *ptr++ = '-';
        value = -value;
        ptr1++;
    }
    
    // 特殊情况：值为0
    if (value == 0) {
        *ptr++ = '0';
    } else {
        // 转换数字 - 使用简单的减法避免除法
        while (value > 0) {
            int remainder = value;
            int quotient = 0;
            
            // 计算 value % base 和 value / base
            while (remainder >= base) {
                remainder -= base;
                quotient++;
            }
            
            *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[remainder];
            value = quotient;
        }
    }
    
    *ptr-- = '\0';
    
    // 反转字符串
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
}

// 简化的printf实现，支持可变参数
int printf(const char* format, ...) {
    if (!format) return 0;
    
    // 直接使用寄存器传递的参数，简化实现
    // 在RISC-V ABI中，format在a0，第一个参数在a1
    register long arg1 asm("a1");
    register long arg2 asm("a2");
    register long arg3 asm("a3");
    register long arg4 asm("a4");
    register long arg5 asm("a5");
    register long arg6 asm("a6");
    register long arg7 asm("a7");
    
    // 将寄存器值保存到数组中
    long regs[7] = {arg1, arg2, arg3, arg4, arg5, arg6, arg7};
    int arg_index = 0;
    int written = 0;
    
    while (*format) {
        if (*format == '%' && *(format + 1)) {
            format++; // 跳过%
            
            switch (*format) {
                case 'd': {
                    // 输出整数
                    int value = (int)regs[arg_index++];
                    char buffer[32];
                    itoa(value, buffer, 10);
                    int len = strlen(buffer);
                    syscall3(SYS_WRITE, STDOUT, (long)buffer, len);
                    written += len;
                    break;
                }
                case 'x': {
                    // 输出十六进制
                    unsigned int value = (unsigned int)regs[arg_index++];
                    char buffer[32];
                    itoa((int)value, buffer, 16);
                    int len = strlen(buffer);
                    syscall3(SYS_WRITE, STDOUT, (long)buffer, len);
                    written += len;
                    break;
                }
                case 'c': {
                    // 输出字符
                    char c = (char)regs[arg_index++];
                    syscall3(SYS_WRITE, STDOUT, (long)&c, 1);
                    written++;
                    break;
                }
                case 's': {
                    // 输出字符串
                    char* str = (char*)regs[arg_index++];
                    if (str) {
                        int len = strlen(str);
                        syscall3(SYS_WRITE, STDOUT, (long)str, len);
                        written += len;
                    }
                    break;
                }
                case '%': {
                    // 输出%字符
                    syscall3(SYS_WRITE, STDOUT, (long)"%", 1);
                    written++;
                    break;
                }
                default:
                    // 不支持的格式，输出原样
                    char temp[2] = {'%', *format};
                    syscall3(SYS_WRITE, STDOUT, (long)temp, 2);
                    written += 2;
                    break;
            }
        } else {
            // 普通字符直接输出
            syscall3(SYS_WRITE, STDOUT, (long)format, 1);
            written++;
        }
        format++;
    }
    
    return written;
}