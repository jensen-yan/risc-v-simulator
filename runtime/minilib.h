#pragma once

// 最小运行时库，提供基本的printf支持
// 针对RISC-V模拟器优化

// printf函数现在使用直接寄存器访问，无需va_list

#ifdef __cplusplus
extern "C" {
#endif

// 基本I/O函数
int printf(const char* format, ...);
int puts(const char* str);
int putchar(int c);

// 程序退出
void exit(int status);

// 字符串函数
int strlen(const char* str);
char* strcpy(char* dest, const char* src);
int strcmp(const char* s1, const char* s2);

// 数值转换
int atoi(const char* str);
void itoa(int value, char* str, int base);

#ifdef __cplusplus
}
#endif