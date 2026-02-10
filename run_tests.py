#!/usr/bin/env python3
"""
Batch test runner for the RISC-V simulator.
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
    """Unified log output format."""
    print(f"[{level.upper()}][{category}] {message}")


PASS_MARKER = "=== TEST RESULT: PASS ==="
FAIL_MARKER = "=== TEST RESULT: FAIL ==="
PROGRAM_FINISHED_MARKER = "Program finished"
PROGRAM_HALTED_MARKER = "Program State: halted"


class TestRunner:
    def __init__(self, simulator_path: str, riscv_tests_path: str,
                 tests_subdir: str = "isa", allow_implicit_pass: bool = False,
                 verbose: bool = False):
        self.simulator_path = simulator_path
        self.riscv_tests_path = riscv_tests_path
        self.tests_subdir = tests_subdir
        self.allow_implicit_pass = allow_implicit_pass
        self.results = {
            'passed': [],
            'failed': [],
            'timeout': [],
            'error': []
        }
        self.lock = threading.Lock()  # 线程锁保护共享数据
        self.verbose = verbose
        
    def find_test_files(self, test_pattern: str = "rv32ui-p-*") -> List[str]:
        """Find test files matching the given pattern."""
        tests_dir = os.path.join(self.riscv_tests_path, self.tests_subdir)
        pattern = os.path.join(tests_dir, test_pattern)
        
        # Exclude .dump files and keep executable binaries only.
        files = []
        for file_path in glob.glob(pattern):
            if not file_path.endswith('.dump') and os.access(file_path, os.X_OK):
                files.append(file_path)
        
        return sorted(files)
    
    def run_single_test(self, test_file: str, timeout: int = 10, ooo: bool = False) -> Tuple[str, str, int, float, str]:
        """Run one test and return status, output, return code, elapsed time and test name."""
        test_name = os.path.basename(test_file)
        
        try:
            # Build simulator command.
            cmd = [
                self.simulator_path,
                "-e",  # ELF mode
                "-m", "2164260864",  # 2GB memory
                test_file
            ]
            if ooo:
                cmd.append("--ooo")
            else:
                cmd.append("--in-order")

            if self.verbose:
                log_message("debug", LOG_CATEGORY_RUNNER, f"cmd: {' '.join(cmd)}")
            
            # Run test.
            start_time = time.time()
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=timeout
            )
            end_time = time.time()
            elapsed = end_time - start_time
            
            # Parse results.
            stdout = result.stdout
            stderr = result.stderr
            
            # Primary status markers.
            if PASS_MARKER in stdout:
                return "passed", "", result.returncode, elapsed, test_name
            elif FAIL_MARKER in stdout:
                return "failed", f"STDOUT:\n{stdout}\nSTDERR:\n{stderr}", result.returncode, elapsed, test_name
            elif result.returncode != 0:
                return "error", f"Non-zero return code: {result.returncode}\nSTDOUT:\n{stdout}\nSTDERR:\n{stderr}", result.returncode, elapsed, test_name
            elif (
                self.allow_implicit_pass and
                result.returncode == 0 and
                PROGRAM_FINISHED_MARKER in stdout and
                PROGRAM_HALTED_MARKER in stdout
            ):
                # Fallback for tests that terminate normally without PASS/FAIL marker.
                return "passed", "", result.returncode, elapsed, test_name
            else:
                return "error", f"Unknown result:\nSTDOUT:\n{stdout}\nSTDERR:\n{stderr}", result.returncode, elapsed, test_name
                
        except subprocess.TimeoutExpired:
            return "timeout", f"Test timeout ({timeout}s)", -1, timeout, test_name
        except Exception as e:
            return "error", f"Execution error: {str(e)}", -1, 0.0, test_name
    
    def run_test_suite(self, test_pattern: str = "rv32ui-p-*", timeout: int = 10, ooo: bool = False, max_workers: int = 0) -> Dict:
        """Run a test suite."""
        log_message("info", LOG_CATEGORY_RUNNER, f"Finding test files: {test_pattern}")
        test_files = self.find_test_files(test_pattern)
        
        if not test_files:
            log_message("error", LOG_CATEGORY_RUNNER, f"No test files matched pattern: {test_pattern}")
            return self.results
        
        log_message("info", LOG_CATEGORY_RUNNER, f"Found {len(test_files)} test files")
        
        # Default worker count.
        if max_workers <= 0:
            max_workers = min(len(test_files), os.cpu_count() or 4)
        
        log_message("info", LOG_CATEGORY_RUNNER, f"Running tests with {max_workers} workers")
        
        if ooo:
            log_message("info", LOG_CATEGORY_RUNNER, "CPU mode: OOO")
        else:
            log_message("info", LOG_CATEGORY_RUNNER, "CPU mode: In-Order")
        log_message("info", LOG_CATEGORY_RUNNER, "-" * 50)
        
        completed_count = 0
        total_tests = len(test_files)
        
        # Run tests in parallel via thread pool.
        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            # Submit tasks.
            future_to_test = {executor.submit(self.run_single_test, test_file, timeout, ooo): test_file 
                             for test_file in test_files}
            
            # Collect completed tasks.
            for future in as_completed(future_to_test):
                test_file = future_to_test[future]
                try:
                    status, output, returncode, elapsed, test_name = future.result()
                    
                    # Thread-safe result update.
                    with self.lock:
                        self.results[status].append({
                            'name': test_name,
                            'file': test_file,
                            'output': output,
                            'returncode': returncode
                        })
                        completed_count += 1
                    
                    # Print status.
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
                            'output': f'Thread execution exception: {exc}',
                            'returncode': -1
                        })
                        completed_count += 1
                    log_message("error", LOG_CATEGORY_RESULT,
                                f"{completed_count:02d}/{total_tests} {test_name}: 💥 ERROR (thread exception)")
        
        return self.results
    
    def print_summary(self):
        """Print test summary."""
        total = sum(len(tests) for tests in self.results.values())
        passed = len(self.results['passed'])
        failed = len(self.results['failed'])
        timeout = len(self.results['timeout'])
        error = len(self.results['error'])
        
        log_message("info", LOG_CATEGORY_SUMMARY, "=" * 60)
        log_message("info", LOG_CATEGORY_SUMMARY, "Test Summary")
        log_message("info", LOG_CATEGORY_SUMMARY, "=" * 60)
        log_message("info", LOG_CATEGORY_SUMMARY, f"Total tests: {total}")
        if total > 0:
            log_message("info", LOG_CATEGORY_SUMMARY,
                        f"✅ Passed:  {passed} ({passed/total*100:.1f}%)")
            log_message("info", LOG_CATEGORY_SUMMARY,
                        f"❌ Failed:  {failed} ({failed/total*100:.1f}%)")
            log_message("info", LOG_CATEGORY_SUMMARY,
                        f"⏰ Timeout: {timeout} ({timeout/total*100:.1f}%)")
            log_message("info", LOG_CATEGORY_SUMMARY,
                        f"💥 Error:   {error} ({error/total*100:.1f}%)")
        
        # Print all-pass message.
        if passed == total and total > 0:
            log_message("info", LOG_CATEGORY_SUMMARY, "🎉 All tests passed")
        
        # Print failure details.
        if failed > 0:
            log_message("warn", LOG_CATEGORY_SUMMARY, "Failed tests:")
            for test in self.results['failed']:
                log_message("warn", LOG_CATEGORY_SUMMARY, f"  - {test['name']}")
        
        if timeout > 0:
            log_message("warn", LOG_CATEGORY_SUMMARY, "Timeout tests:")
            for test in self.results['timeout']:
                log_message("warn", LOG_CATEGORY_SUMMARY, f"  - {test['name']}")
        
        if error > 0:
            log_message("error", LOG_CATEGORY_SUMMARY, "Error tests:")
            for test in self.results['error']:
                log_message("error", LOG_CATEGORY_SUMMARY, f"  - {test['name']}")
                # Only show first 100 chars.
                if test['output'] and len(test['output']) > 100:
                    log_message("error", LOG_CATEGORY_SUMMARY,
                                f"    Error: {test['output'][:100]}...")
                elif test['output']:
                    log_message("error", LOG_CATEGORY_SUMMARY,
                                f"    Error: {test['output']}")
        
        # Suggest possible directions.
        if error > 0 or failed > 0 or timeout > 0:
            log_message("info", LOG_CATEGORY_SUGGEST, "💡 Potential improvements:")
            if any("memory" in test.get('output', '').lower() for test in self.results['error']):
                log_message("info", LOG_CATEGORY_SUGGEST,
                            "  - Memory handling: validate load address and memory size")
            if any("rvc" in test.get('name', '')
                   for test in self.results['error'] + self.results['failed']):
                log_message("info", LOG_CATEGORY_SUGGEST,
                            "  - C extension support: improve 16-bit compressed decoding")
            if any("uf-" in test.get('name', '') or "ud-" in test.get('name', '') or "uzfh-" in test.get('name', '')
                   for test in self.results['error'] + self.results['failed']):
                log_message("info", LOG_CATEGORY_SUGGEST,
                            "  - F extension support: improve floating-point implementation")
            if any("ua-" in test.get('name', '')
                   for test in self.results['error'] + self.results['failed']):
                log_message("info", LOG_CATEGORY_SUGGEST,
                            "  - A extension support: implement atomic instructions")
    
    def save_results(self, output_file: str):
        """Save detailed results to a file."""
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write("RISC-V Simulator Test Results\n")
            f.write("="*50 + "\n\n")
            
            for status, tests in self.results.items():
                if tests:
                    f.write(f"{status.upper()} ({len(tests)} tests):\n")
                    f.write("-" * 30 + "\n")
                    for test in tests:
                        f.write(f"Test: {test['name']}\n")
                        f.write(f"File: {test['file']}\n")
                        f.write(f"Return code: {test['returncode']}\n")
                        if test['output']:
                            f.write(f"Output:\n{test['output']}\n")
                        f.write("\n")
                    f.write("\n")

def main():
    # Available test patterns.
    pattern_help = """
Available test patterns:
  (for --tests-subdir isa)
  rv32ui-p-*     - User integer instructions (base)
  rv32um-p-*     - User multiply/divide instructions (M extension)
  rv32ua-p-*     - User atomic instructions (A extension)
  rv32uf-p-*     - User single-precision floating-point (F extension)
  rv32ud-p-*     - User double-precision floating-point (D extension)
  rv32uc-p-*     - User compressed instructions (C extension)
  rv32uzfh-p-*   - User half-precision floating-point (Zfh extension)
  rv32uzba-p-*   - Bit-manip address generation (Zba extension)
  rv32uzbb-p-*   - Bit-manip base (Zbb extension)
  rv32uzbc-p-*   - Bit-manip carry-less multiply (Zbc extension)
  rv32uzbs-p-*   - Bit-manip single-bit ops (Zbs extension)
  rv32mi-p-*     - Machine-level integer instructions
  rv32si-p-*     - Supervisor-level instructions

  (for --tests-subdir benchmarks)
  *.riscv        - All riscv-tests benchmarks
  dhrystone.riscv - Dhrystone benchmark
  mm.riscv        - Matrix multiply benchmark
  
Examples:
  --pattern "rv32ui-p-*"      # all base integer tests
  --pattern "rv32ui-p-add*"   # add-related tests
  --pattern "rv32u*-p-*"      # all user-level tests
    """
    
    parser = argparse.ArgumentParser(
        description='RISC-V simulator batch test tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=pattern_help
    )
    parser.add_argument('--simulator', '-s', 
                       default='./build/risc-v-sim',
                       help='Path to simulator executable')
    parser.add_argument('--tests-dir', '-t',
                       default='./riscv-tests',
                       help='Path to riscv-tests directory')
    parser.add_argument('--tests-subdir',
                       default='isa',
                       help='Subdirectory under tests-dir to scan (default: isa)')
    parser.add_argument('--pattern', '-p',
                       default='rv32ui-p-*',
                       help='Test file pattern (default: rv32ui-p-*)')
    parser.add_argument('--timeout',
                       type=int, default=10,
                       help='Per-test timeout in seconds')
    parser.add_argument('--output', '-o',
                       help='Output file path')
    parser.add_argument('--verbose', '-v',
                       action='store_true',
                       help='Enable verbose output')
    parser.add_argument('--ooo',
                       action='store_true',
                       help='Run tests on OOO CPU')
    parser.add_argument('--workers', '-w',
                       type=int, default=4,
                       help='Worker count for parallel testing (default: 4, 0=auto)')
    parser.add_argument('--allow-implicit-pass',
                       action='store_true',
                       help='Allow implicit pass when no PASS/FAIL marker is found')
    
    args = parser.parse_args()
    
    # Validate simulator path.
    if not os.path.exists(args.simulator):
        log_message("error", LOG_CATEGORY_RUNNER, f"Simulator not found: {args.simulator}")
        log_message("info", LOG_CATEGORY_RUNNER, "Build first: make -C build")
        sys.exit(1)
    
    # Validate tests path.
    if not os.path.exists(args.tests_dir):
        log_message("error", LOG_CATEGORY_RUNNER, f"Tests directory not found: {args.tests_dir}")
        log_message("info", LOG_CATEGORY_RUNNER, "Make sure riscv-tests submodule is initialized and built")
        sys.exit(1)

    tests_subdir_path = os.path.join(args.tests_dir, args.tests_subdir)
    if not os.path.exists(tests_subdir_path):
        log_message("error", LOG_CATEGORY_RUNNER, f"Tests subdirectory not found: {tests_subdir_path}")
        sys.exit(1)
    
    log_message("info", LOG_CATEGORY_RUNNER, "RISC-V simulator batch test tool")
    log_message("info", LOG_CATEGORY_RUNNER, f"Simulator: {args.simulator}")
    log_message("info", LOG_CATEGORY_RUNNER, f"Tests dir: {args.tests_dir}")
    log_message("info", LOG_CATEGORY_RUNNER, f"Tests subdir: {args.tests_subdir}")
    log_message("info", LOG_CATEGORY_RUNNER, f"Pattern: {args.pattern}")
    log_message("info", LOG_CATEGORY_RUNNER, f"Timeout: {args.timeout}s")
    log_message("info", LOG_CATEGORY_RUNNER, "-" * 50)
    
    # Run tests.
    runner = TestRunner(
        args.simulator,
        args.tests_dir,
        tests_subdir=args.tests_subdir,
        allow_implicit_pass=args.allow_implicit_pass,
        verbose=args.verbose
    )
    results = runner.run_test_suite(args.pattern, args.timeout, args.ooo, args.workers)
    
    # Print summary.
    runner.print_summary()
    
    # Save results.
    if args.output:
        runner.save_results(args.output)
        log_message("info", LOG_CATEGORY_RUNNER, f"Detailed results saved to: {args.output}")
    
    # Return exit code.
    if len(results['failed']) + len(results['timeout']) + len(results['error']) > 0:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == '__main__':
    main()
