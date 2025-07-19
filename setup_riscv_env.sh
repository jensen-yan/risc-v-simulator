#!/bin/bash
# RISC-V 编译环境配置脚本
# 对于ubuntu 24.04 需要安装 交叉编译器 和 picolibc 库（替换newlibc库or glibc库）
# sudo apt install gcc-riscv64-unknown-elf    
# sudo apt install picolibc-riscv64-unknown-elf

# 使用方法: source setup_riscv_env.sh

# 设置 RISC-V 工具链路径
export RISCV=/usr

# 设置编译器路径
export RISCV_PREFIX=riscv64-unknown-elf-

# 设置头文件搜索路径
export CPATH="/usr/lib/picolibc/riscv64-unknown-elf/include:$CPATH"
export C_INCLUDE_PATH="/usr/lib/picolibc/riscv64-unknown-elf/include:$C_INCLUDE_PATH"

# 设置库文件搜索路径
export LIBRARY_PATH="/usr/lib/picolibc/riscv64-unknown-elf/lib/rv64imafdc/lp64d:$LIBRARY_PATH"

# 显示配置信息
echo "✅ RISC-V 环境配置完成"
echo "   RISCV = $RISCV"
echo "   头文件路径 = /usr/lib/picolibc/riscv64-unknown-elf/include"
echo "   库文件路径 = /usr/lib/picolibc/riscv64-unknown-elf/lib/rv64imafdc/lp64d"
echo ""
echo "现在可以编译 riscv-tests："
echo "   cd riscv-tests && make" 