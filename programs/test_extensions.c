// 测试RISC-V扩展指令的程序
// 测试M扩展（乘除法）、F扩展（浮点运算）

void test_m_extension() {
    // 测试M扩展指令
    int a = 15;
    int b = 7;
    
    // 乘法测试
    int mul_result = a * b;  // 应该生成MUL指令
    
    // 除法测试  
    int div_result = a / b;  // 应该生成DIV指令
    int rem_result = a % b;  // 应该生成REM指令
    
    // 输出结果（使用系统调用）
    // 这里使用简单的内存写入来"输出"结果
    volatile int* output = (volatile int*)0x2000;
    output[0] = mul_result;  // 15 * 7 = 105
    output[1] = div_result;  // 15 / 7 = 2  
    output[2] = rem_result;  // 15 % 7 = 1
}

void test_f_extension() {
    // 测试F扩展指令（需要汇编或编译器支持）
    // 这里用整数模拟浮点操作
    float a = 3.14f;
    float b = 2.0f;
    
    // 基本浮点运算
    float add_result = a + b;  // FADD.S
    float sub_result = a - b;  // FSUB.S  
    float mul_result = a * b;  // FMUL.S
    float div_result = a / b;  // FDIV.S
    
    // 将结果存储到内存
    volatile float* fp_output = (volatile float*)0x2100;
    fp_output[0] = add_result;  // 3.14 + 2.0 = 5.14
    fp_output[1] = sub_result;  // 3.14 - 2.0 = 1.14
    fp_output[2] = mul_result;  // 3.14 * 2.0 = 6.28
    fp_output[3] = div_result;  // 3.14 / 2.0 = 1.57
}

int main() {
    // 初始化标记
    volatile int* status = (volatile int*)0x1F00;
    status[0] = 0xDEADBEEF;  // 开始标记
    
    // 运行M扩展测试
    test_m_extension();
    status[1] = 0x12345678;  // M扩展完成标记
    
    // 运行F扩展测试
    test_f_extension();
    status[2] = 0x87654321;  // F扩展完成标记
    
    // 程序结束标记
    status[3] = 0x00000000;
    
    return 0;
}