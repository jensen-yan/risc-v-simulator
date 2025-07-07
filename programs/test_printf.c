#include "../runtime/minilib.h"

int main() {
    printf("Hello, RISC-V World!\n");
    printf("测试数字: %d\n", 42);
    printf("测试十六进制: 0x%x\n", 255);
    printf("测试字符: %c\n", 'A');
    printf("测试字符串: %s\n", "这是一个测试");
    
    int sum = 0;
    for (int i = 1; i <= 5; i++) {
        sum += i;
        printf("i=%d, sum=%d\n", i, sum);
    }
    
    printf("计算完成，总和为: %d\n", sum);
    return 0;
}