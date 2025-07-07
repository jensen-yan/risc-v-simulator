// 算术运算测试程序
int main() {
    int a = 15;
    int b = 7;
    
    int sum = a + b;      // 22
    int diff = a - b;     // 8
    int prod = a * b;     // 105 (需要乘法指令，先用加法实现)
    
    // 用加法实现乘法 a * b
    int mult_result = 0;
    for (int i = 0; i < b; i++) {
        mult_result += a;
    }
    
    return sum + diff + mult_result;  // 22 + 8 + 105 = 135
}