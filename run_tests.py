#!/usr/bin/env python3
"""
RISC-V 模拟器批量测试脚本
测试模拟器对 riscv-tests 的兼容性
"""

import os
import subprocess
import sys
import time
import glob
from pathlib import Path
from typing import List, Tuple, Dict
import argparse

class TestRunner:
    def __init__(self, simulator_path: str, riscv_tests_path: str):
        self.simulator_path = simulator_path
        self.riscv_tests_path = riscv_tests_path
        self.results = {
            'passed': [],
            'failed': [],
            'timeout': [],
            'error': []
        }
        
    def find_test_files(self, test_pattern: str = "rv32ui-p-*") -> List[str]:
        """查找符合模式的测试文件"""
        isa_dir = os.path.join(self.riscv_tests_path, "isa")
        pattern = os.path.join(isa_dir, test_pattern)
        
        # 排除 .dump 文件，只要可执行文件
        files = []
        for file_path in glob.glob(pattern):
            if not file_path.endswith('.dump') and os.access(file_path, os.X_OK):
                files.append(file_path)
        
        return sorted(files)
    
    def run_single_test(self, test_file: str, timeout: int = 10) -> Tuple[str, str, int]:
        """运行单个测试文件"""
        test_name = os.path.basename(test_file)
        print(f"运行测试: {test_name}...", end=' ', flush=True)
        
        try:
            # 构建模拟器命令
            cmd = [
                self.simulator_path,
                "-e",  # ELF 模式
                "-m", "2164260864",  # 2GB 内存
                test_file
            ]
            
            # 运行测试
            start_time = time.time()
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=timeout
            )
            end_time = time.time()
            
            # 解析结果
            stdout = result.stdout
            stderr = result.stderr
            
            # 检查是否通过
            if "=== 测试结果: PASS ===" in stdout:
                print(f"✅ PASS ({end_time - start_time:.2f}s)")
                return "passed", "", result.returncode
            elif "=== 测试结果: FAIL ===" in stdout:
                print(f"❌ FAIL ({end_time - start_time:.2f}s)")
                return "failed", stderr, result.returncode
            else:
                print(f"⚠️  UNKNOWN ({end_time - start_time:.2f}s)")
                return "error", f"未知结果:\nSTDOUT:\n{stdout}\nSTDERR:\n{stderr}", result.returncode
                
        except subprocess.TimeoutExpired:
            print(f"⏰ TIMEOUT ({timeout}s)")
            return "timeout", f"测试超时 ({timeout}s)", -1
        except Exception as e:
            print(f"💥 ERROR")
            return "error", f"执行错误: {str(e)}", -1
    
    def run_test_suite(self, test_pattern: str = "rv32ui-p-*", timeout: int = 10) -> Dict:
        """运行测试套件"""
        print(f"查找测试文件: {test_pattern}")
        test_files = self.find_test_files(test_pattern)
        
        if not test_files:
            print(f"❌ 未找到匹配的测试文件: {test_pattern}")
            return self.results
        
        print(f"找到 {len(test_files)} 个测试文件\n")
        
        # 运行每个测试
        for test_file in test_files:
            test_name = os.path.basename(test_file)
            status, output, returncode = self.run_single_test(test_file, timeout)
            
            self.results[status].append({
                'name': test_name,
                'file': test_file,
                'output': output,
                'returncode': returncode
            })
        
        return self.results
    
    def print_summary(self):
        """打印测试结果摘要"""
        total = sum(len(tests) for tests in self.results.values())
        passed = len(self.results['passed'])
        failed = len(self.results['failed'])
        timeout = len(self.results['timeout'])
        error = len(self.results['error'])
        
        print("\n" + "="*60)
        print("测试结果摘要")
        print("="*60)
        print(f"总测试数: {total}")
        print(f"✅ 通过:   {passed} ({passed/total*100:.1f}%)")
        print(f"❌ 失败:   {failed} ({failed/total*100:.1f}%)")
        print(f"⏰ 超时:   {timeout} ({timeout/total*100:.1f}%)")
        print(f"💥 错误:   {error} ({error/total*100:.1f}%)")
        
        # 打印失败的测试详情
        if failed > 0:
            print(f"\n失败的测试:")
            for test in self.results['failed']:
                print(f"  - {test['name']}")
        
        if timeout > 0:
            print(f"\n超时的测试:")
            for test in self.results['timeout']:
                print(f"  - {test['name']}")
        
        if error > 0:
            print(f"\n出错的测试:")
            for test in self.results['error']:
                print(f"  - {test['name']}: {test['output'][:100]}...")
    
    def save_results(self, output_file: str):
        """保存详细结果到文件"""
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write("RISC-V 模拟器测试结果\n")
            f.write("="*50 + "\n\n")
            
            for status, tests in self.results.items():
                if tests:
                    f.write(f"{status.upper()} ({len(tests)} 测试):\n")
                    f.write("-" * 30 + "\n")
                    for test in tests:
                        f.write(f"测试: {test['name']}\n")
                        f.write(f"文件: {test['file']}\n")
                        f.write(f"返回码: {test['returncode']}\n")
                        if test['output']:
                            f.write(f"输出:\n{test['output']}\n")
                        f.write("\n")
                    f.write("\n")

def main():
    parser = argparse.ArgumentParser(description='RISC-V 模拟器批量测试工具')
    parser.add_argument('--simulator', '-s', 
                       default='./build/risc-v-sim',
                       help='模拟器可执行文件路径')
    parser.add_argument('--tests-dir', '-t',
                       default='./riscv-tests',
                       help='riscv-tests 目录路径')
    parser.add_argument('--pattern', '-p',
                       default='rv32ui-p-*',
                       help='测试文件模式 (默认: rv32ui-p-*)')
    parser.add_argument('--timeout',
                       type=int, default=10,
                       help='单个测试超时时间(秒)')
    parser.add_argument('--output', '-o',
                       help='结果输出文件')
    parser.add_argument('--verbose', '-v',
                       action='store_true',
                       help='详细输出')
    
    args = parser.parse_args()
    
    # 检查模拟器是否存在
    if not os.path.exists(args.simulator):
        print(f"❌ 模拟器文件不存在: {args.simulator}")
        print("请先编译模拟器: make -C build")
        sys.exit(1)
    
    # 检查测试目录是否存在
    if not os.path.exists(args.tests_dir):
        print(f"❌ 测试目录不存在: {args.tests_dir}")
        print("请确保 riscv-tests 子模块已初始化并编译")
        sys.exit(1)
    
    print("RISC-V 模拟器批量测试工具")
    print(f"模拟器: {args.simulator}")
    print(f"测试目录: {args.tests_dir}")
    print(f"测试模式: {args.pattern}")
    print(f"超时时间: {args.timeout}s")
    print("-" * 50)
    
    # 运行测试
    runner = TestRunner(args.simulator, args.tests_dir)
    results = runner.run_test_suite(args.pattern, args.timeout)
    
    # 打印摘要
    runner.print_summary()
    
    # 保存结果
    if args.output:
        runner.save_results(args.output)
        print(f"\n详细结果已保存到: {args.output}")
    
    # 返回适当的退出码
    if len(results['failed']) + len(results['timeout']) + len(results['error']) > 0:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == '__main__':
    main()