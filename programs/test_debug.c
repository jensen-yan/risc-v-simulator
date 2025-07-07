#include "../runtime/minilib.h"

int main() {
    puts("=== 调试测试程序 ===");
    
    // 测试简单字符串
    puts("测试1: 纯字符串");
    printf("Hello World\n");
    
    // 测试单个整数
    puts("测试2: 单个整数 42");
    printf("数字: %d\n", 42);
    
    // 测试单个字符
    puts("测试3: 单个字符 A");
    printf("字符: %c\n", 'A');
    
    // 测试十六进制
    puts("测试4: 十六进制 255");
    printf("十六进制: %x\n", 255);
    
    puts("=== 测试完成 ===");
    return 0;
}