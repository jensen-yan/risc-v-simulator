// 简单的计算程序 - 不需要系统调用
// 计算1到10的和

int main() {
    int sum = 0;
    for (int i = 1; i <= 10; i++) {
        sum += i;
    }
    return sum;  // 应该返回55
}