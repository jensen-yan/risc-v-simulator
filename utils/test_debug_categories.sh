#!/bin/bash

# 测试各个调试分类的脚本
# 用于定位哪个分类导致了二进制文件输出

echo "开始测试各个调试分类..."

# 定义要测试的分类
categories=("FETCH" "DECODE" "COMMIT" "ISSUE" "EXECUTE" "WRITEBACK" "ROB" "RENAME" "RS" "BRANCH" "STALL" "MEMORY" "SYSCALL" "DIFFTEST")

# 测试程序
program="../riscv-tests/isa/rv32ui-p-add"

for category in "${categories[@]}"; do
    echo "测试分类: $category"
    
    # 输出文件名
    output_file="test_${category}.out"
    
    # 运行测试
    ./risc-v-sim -e -m 2164260864 "$program" --debug-file="$output_file" --debug-categories="$category" 2>/dev/null
    
    # 检查文件类型
    file_type=$(file "$output_file" | cut -d: -f2)
    echo "  文件类型: $file_type"
    
    # 检查文件大小
    if [ -f "$output_file" ]; then
        size=$(wc -c < "$output_file")
        echo "  文件大小: $size 字节"
        
        # 如果是二进制文件，显示前几个字节
        if [[ "$file_type" == *"data"* ]]; then
            echo "  ⚠️  发现二进制输出！前16字节："
            hexdump -C "$output_file" | head -n 2
        fi
    else
        echo "  ❌ 文件未生成"
    fi
    
    echo ""
done

echo "测试完成！"
echo ""
echo "总结："
for category in "${categories[@]}"; do
    output_file="test_${category}.out"
    if [ -f "$output_file" ]; then
        file_type=$(file "$output_file" | cut -d: -f2)
        if [[ "$file_type" == *"data"* ]]; then
            echo "❌ $category: 二进制文件"
        else
            echo "✅ $category: 文本文件"
        fi
    fi
done 