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
from concurrent.futures import ThreadPoolExecutor, as_completed
import threading


LOG_CATEGORY_RUNNER = "RUNNER"
LOG_CATEGORY_TEST = "TEST"
LOG_CATEGORY_RESULT = "RESULT"
LOG_CATEGORY_SUMMARY = "SUMMARY"
LOG_CATEGORY_SUGGEST = "SUGGEST"


def log_message(level: str, category: str, message: str) -> None:
    """统一日志输出格式"""
    print(f"[{level.upper()}][{category}] {message}")


class TestRunner:
    def __init__(self, simulator_path: str, riscv_tests_path: str, verbose: bool = False):
        self.simulator_path = simulator_path
        self.riscv_tests_path = riscv_tests_path
        self.results = {
            'passed': [],
            'failed': [],
            'timeout': [],
            'error': []
        }
        self.lock = threading.Lock()  # 线程锁保护共享数据
        self.verbose = verbose
        
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
    
    def run_single_test(self, test_file: str, timeout: int = 10, ooo: bool = False) -> Tuple[str, str, int, float, str]:
        """运行单个测试文件，返回状态、输出、返回码、执行时间和测试名"""
        test_name = os.path.basename(test_file)
        
        try:
            # 构建模拟器命令
            cmd = [
                self.simulator_path,
                "-e",  # ELF 模式
                "-m", "2164260864",  # 2GB 内存
                test_file
            ]
            if ooo:
                cmd.append("--ooo")
            else:
                cmd.append("--in-order")

            if self.verbose:
                log_message("debug", LOG_CATEGORY_RUNNER, f"cmd: {' '.join(cmd)}")
            
            # 运行测试
            start_time = time.time()
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=timeout
            )
            end_time = time.time()
            elapsed = end_time - start_time
            
            # 解析结果
            stdout = result.stdout
            stderr = result.stderr
            
            # 检查是否通过
            if "=== 测试结果: PASS ===" in stdout:
                return "passed", "", result.returncode, elapsed, test_name
            elif "=== 测试结果: FAIL ===" in stdout:
                return "failed", stderr, result.returncode, elapsed, test_name
            else:
                return "error", f"未知结果:\nSTDOUT:\n{stdout}\nSTDERR:\n{stderr}", result.returncode, elapsed, test_name
                
        except subprocess.TimeoutExpired:
            return "timeout", f"测试超时 ({timeout}s)", -1, timeout, test_name
        except Exception as e:
            return "error", f"执行错误: {str(e)}", -1, 0.0, test_name
    
    def run_test_suite(self, test_pattern: str = "rv32ui-p-*", timeout: int = 10, ooo: bool = False, max_workers: int = 0) -> Dict:
        """运行测试套件"""
        log_message("info", LOG_CATEGORY_RUNNER, f"查找测试文件: {test_pattern}")
        test_files = self.find_test_files(test_pattern)
        
        if not test_files:
            log_message("error", LOG_CATEGORY_RUNNER, f"未找到匹配的测试文件: {test_pattern}")
            return self.results
        
        log_message("info", LOG_CATEGORY_RUNNER, f"找到 {len(test_files)} 个测试文件")
        
        # 设置默认线程数
        if max_workers <= 0:
            max_workers = min(len(test_files), os.cpu_count() or 4)
        
        log_message("info", LOG_CATEGORY_RUNNER, f"使用 {max_workers} 个线程并行运行测试")
        
        if ooo:
            log_message("info", LOG_CATEGORY_RUNNER, "测试模式: OOO CPU")
        log_message("info", LOG_CATEGORY_RUNNER, "-" * 50)
        
        completed_count = 0
        total_tests = len(test_files)
        
        # 使用线程池并行运行测试
        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            # 提交所有任务
            future_to_test = {executor.submit(self.run_single_test, test_file, timeout, ooo): test_file 
                             for test_file in test_files}
            
            # 处理完成的任务
            for future in as_completed(future_to_test):
                test_file = future_to_test[future]
                try:
                    status, output, returncode, elapsed, test_name = future.result()
                    
                    # 线程安全地更新结果
                    with self.lock:
                        self.results[status].append({
                            'name': test_name,
                            'file': test_file,
                            'output': output,
                            'returncode': returncode
                        })
                        completed_count += 1
                    
                    # 打印结果（线程安全）
                    status_emoji = {
                        'passed': '✅ PASS',
                        'failed': '❌ FAIL',
                        'timeout': '⏰ TIMEOUT',
                        'error': '💥 ERROR'
                    }
                    status_label = status_emoji.get(status, '⚠️ UNKNOWN')
                    log_message("info", LOG_CATEGORY_RESULT,
                                f"{completed_count:02d}/{total_tests} {test_name}: {status_label} ({elapsed:.2f}s)")
                    
                except Exception as exc:
                    test_name = os.path.basename(test_file)
                    with self.lock:
                        self.results['error'].append({
                            'name': test_name,
                            'file': test_file,
                            'output': f'线程执行异常: {exc}',
                            'returncode': -1
                        })
                        completed_count += 1
                    log_message("error", LOG_CATEGORY_RESULT,
                                f"{completed_count:02d}/{total_tests} {test_name}: 💥 ERROR (线程异常)")
        
        return self.results
    
    def print_summary(self):
        """打印测试结果摘要"""
        total = sum(len(tests) for tests in self.results.values())
        passed = len(self.results['passed'])
        failed = len(self.results['failed'])
        timeout = len(self.results['timeout'])
        error = len(self.results['error'])
        
        log_message("info", LOG_CATEGORY_SUMMARY, "=" * 60)
        log_message("info", LOG_CATEGORY_SUMMARY, "测试结果摘要")
        log_message("info", LOG_CATEGORY_SUMMARY, "=" * 60)
        log_message("info", LOG_CATEGORY_SUMMARY, f"总测试数: {total}")
        if total > 0:
            log_message("info", LOG_CATEGORY_SUMMARY,
                        f"✅ 通过:   {passed} ({passed/total*100:.1f}%)")
            log_message("info", LOG_CATEGORY_SUMMARY,
                        f"❌ 失败:   {failed} ({failed/total*100:.1f}%)")
            log_message("info", LOG_CATEGORY_SUMMARY,
                        f"⏰ 超时:   {timeout} ({timeout/total*100:.1f}%)")
            log_message("info", LOG_CATEGORY_SUMMARY,
                        f"💥 错误:   {error} ({error/total*100:.1f}%)")
        
        # 打印通过的测试（如果全部通过）
        if passed == total and total > 0:
            log_message("info", LOG_CATEGORY_SUMMARY, "🎉 所有测试通过！")
        
        # 打印失败的测试详情
        if failed > 0:
            log_message("warn", LOG_CATEGORY_SUMMARY, "失败的测试:")
            for test in self.results['failed']:
                log_message("warn", LOG_CATEGORY_SUMMARY, f"  - {test['name']}")
        
        if timeout > 0:
            log_message("warn", LOG_CATEGORY_SUMMARY, "超时的测试:")
            for test in self.results['timeout']:
                log_message("warn", LOG_CATEGORY_SUMMARY, f"  - {test['name']}")
        
        if error > 0:
            log_message("error", LOG_CATEGORY_SUMMARY, "出错的测试:")
            for test in self.results['error']:
                log_message("error", LOG_CATEGORY_SUMMARY, f"  - {test['name']}")
                # 只显示前100个字符
                if test['output'] and len(test['output']) > 100:
                    log_message("error", LOG_CATEGORY_SUMMARY,
                                f"    错误: {test['output'][:100]}...")
                elif test['output']:
                    log_message("error", LOG_CATEGORY_SUMMARY,
                                f"    错误: {test['output']}")
        
        # 提供改进建议
        if error > 0 or failed > 0 or timeout > 0:
            log_message("info", LOG_CATEGORY_SUGGEST, "💡 可能的改进方向:")
            if any("内存错误" in test.get('output', '') for test in self.results['error']):
                log_message("info", LOG_CATEGORY_SUGGEST,
                            "  - 内存管理：检查程序加载地址和内存大小配置")
            if any("压缩指令" in test.get('name', '') or "rvc" in test.get('name', '')
                   for test in self.results['error'] + self.results['failed']):
                log_message("info", LOG_CATEGORY_SUGGEST,
                            "  - C扩展支持：实现16位压缩指令解码")
            if any("浮点" in test.get('name', '') or "uf-" in test.get('name', '')
                   for test in self.results['error'] + self.results['failed']):
                log_message("info", LOG_CATEGORY_SUGGEST,
                            "  - F扩展支持：完善浮点指令实现")
            if any("原子" in test.get('name', '') or "ua-" in test.get('name', '')
                   for test in self.results['error'] + self.results['failed']):
                log_message("info", LOG_CATEGORY_SUGGEST,
                            "  - A扩展支持：实现原子操作指令")
    
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
    # 可用的测试模式说明
    pattern_help = """
可用的测试模式 (pattern):
  rv32ui-p-*     - 用户级整数指令 (基础)
  rv32um-p-*     - 用户级乘除法指令 (M扩展)
  rv32ua-p-*     - 用户级原子指令 (A扩展)  
  rv32uf-p-*     - 用户级单精度浮点 (F扩展)
  rv32ud-p-*     - 用户级双精度浮点 (D扩展)
  rv32uc-p-*     - 用户级压缩指令 (C扩展)
  rv32uzfh-p-*   - 用户级半精度浮点 (Zfh扩展)
  rv32uzba-p-*   - 位操作地址生成 (Zba扩展)
  rv32uzbb-p-*   - 位操作基础 (Zbb扩展)
  rv32uzbc-p-*   - 位操作进位 (Zbc扩展)
  rv32uzbs-p-*   - 位操作单一 (Zbs扩展)
  rv32mi-p-*     - 机器级整数指令
  rv32si-p-*     - 监督级指令
  
常用示例:
  --pattern "rv32ui-p-*"      # 所有基础整数测试
  --pattern "rv32ui-p-add*"   # 加法相关测试
  --pattern "rv32u*-p-*"      # 所有用户级测试
    """
    
    parser = argparse.ArgumentParser(
        description='RISC-V 模拟器批量测试工具',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=pattern_help
    )
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
    parser.add_argument('--ooo',
                       action='store_true',
                       help='测试OOO CPU')
    parser.add_argument('--workers', '-w',
                       type=int, default=4,
                       help='并行测试的线程数 (默认: 4核心，0表示自动检测)')
    
    args = parser.parse_args()
    
    # 检查模拟器是否存在
    if not os.path.exists(args.simulator):
        log_message("error", LOG_CATEGORY_RUNNER, f"模拟器文件不存在: {args.simulator}")
        log_message("info", LOG_CATEGORY_RUNNER, "请先编译模拟器: make -C build")
        sys.exit(1)
    
    # 检查测试目录是否存在
    if not os.path.exists(args.tests_dir):
        log_message("error", LOG_CATEGORY_RUNNER, f"测试目录不存在: {args.tests_dir}")
        log_message("info", LOG_CATEGORY_RUNNER, "请确保 riscv-tests 子模块已初始化并编译")
        sys.exit(1)
    
    log_message("info", LOG_CATEGORY_RUNNER, "RISC-V 模拟器批量测试工具")
    log_message("info", LOG_CATEGORY_RUNNER, f"模拟器: {args.simulator}")
    log_message("info", LOG_CATEGORY_RUNNER, f"测试目录: {args.tests_dir}")
    log_message("info", LOG_CATEGORY_RUNNER, f"测试模式: {args.pattern}")
    log_message("info", LOG_CATEGORY_RUNNER, f"超时时间: {args.timeout}s")
    log_message("info", LOG_CATEGORY_RUNNER, "-" * 50)
    
    # 运行测试
    runner = TestRunner(args.simulator, args.tests_dir, verbose=args.verbose)
    results = runner.run_test_suite(args.pattern, args.timeout, args.ooo, args.workers)
    
    # 打印摘要
    runner.print_summary()
    
    # 保存结果
    if args.output:
        runner.save_results(args.output)
        log_message("info", LOG_CATEGORY_RUNNER, f"详细结果已保存到: {args.output}")
    
    # 返回适当的退出码
    if len(results['failed']) + len(results['timeout']) + len(results['error']) > 0:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == '__main__':
    main()
