## 生成汇编文件
riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -S test_simple.c -o test_simple.s

## 编译
riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -nostdlib -nostartfiles -Wl,-Ttext=0x1000 test_simple.c -o test_simple.elf

## 反汇编
riscv64-unknown-elf-objdump -d test_simple.elf

## 转换为二进制文件
riscv64-unknown-elf-objcopy -O binary test_simple.elf test_simple.bin

## 查看二进制文件
hexdump -C test_simple.bin | head -10