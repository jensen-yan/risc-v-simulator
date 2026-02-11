#!/usr/bin/env python3
"""统一性能基准运行器。

功能：
- 从 manifest 读取基准项（本地 path 或 glob）
- 运行模拟器（in-order / ooo / both）
- 解析关键统计（Instructions/Cycles/IPC/branch_mispredicts/pipeline_stalls）
- 输出 CSV + JSON，便于后续做趋势分析
"""

from __future__ import annotations

import argparse
import csv
import fnmatch
import json
import re
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional


RE_INSTRUCTIONS = re.compile(r"Instructions:\s*(\d+)")
RE_CYCLES = re.compile(r"Cycles:\s*(\d+)")
RE_IPC = re.compile(r"\bIPC:\s*([0-9]+(?:\.[0-9]+)?)")
RE_BRANCH_MISPREDICTS = re.compile(
    r"(?:branch_mispredicts|Branch\s+Mispredicts):\s*(\d+)"
)
RE_PIPELINE_STALLS = re.compile(r"(?:pipeline_stalls|Pipeline\s+Stalls):\s*(\d+)")


@dataclass
class BenchmarkTarget:
    suite: str
    name: str
    path: Path
    optional: bool


@dataclass
class RunRecord:
    suite: str
    benchmark: str
    file: str
    mode: str
    status: str
    return_code: int
    elapsed_sec: float
    instructions: Optional[int]
    cycles: Optional[int]
    ipc: Optional[float]
    branch_mispredicts: Optional[int]
    pipeline_stalls: Optional[int]


def _extract_last_int(pattern: re.Pattern[str], text: str) -> Optional[int]:
    matches = pattern.findall(text)
    if not matches:
        return None
    return int(matches[-1])


def _extract_last_float(pattern: re.Pattern[str], text: str) -> Optional[float]:
    matches = pattern.findall(text)
    if not matches:
        return None
    return float(matches[-1])


def parse_stats(output: str) -> Dict[str, Optional[float]]:
    instructions = _extract_last_int(RE_INSTRUCTIONS, output)
    cycles = _extract_last_int(RE_CYCLES, output)
    ipc = _extract_last_float(RE_IPC, output)
    branch_mispredicts = _extract_last_int(RE_BRANCH_MISPREDICTS, output)
    pipeline_stalls = _extract_last_int(RE_PIPELINE_STALLS, output)

    if ipc is None and instructions is not None and cycles and cycles > 0:
        ipc = instructions / cycles

    return {
        "instructions": instructions,
        "cycles": cycles,
        "ipc": ipc,
        "branch_mispredicts": branch_mispredicts,
        "pipeline_stalls": pipeline_stalls,
    }


def load_manifest(manifest_path: Path, repo_root: Path) -> List[BenchmarkTarget]:
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    raw_items = data.get("benchmarks", [])
    targets: List[BenchmarkTarget] = []

    if not isinstance(raw_items, list):
        raise ValueError("manifest 字段 'benchmarks' 必须是数组")

    for idx, item in enumerate(raw_items):
        if not isinstance(item, dict):
            raise ValueError(f"manifest 第 {idx} 项不是对象")

        suite = str(item.get("suite", "unknown"))
        name = str(item.get("name", f"item_{idx}"))
        optional = bool(item.get("optional", False))

        path_field = item.get("path")
        glob_field = item.get("glob")

        if bool(path_field) == bool(glob_field):
            raise ValueError(f"manifest 第 {idx} 项必须且只能包含 path 或 glob")

        if path_field:
            path = (repo_root / str(path_field)).resolve()
            targets.append(BenchmarkTarget(suite=suite, name=name, path=path, optional=optional))
            continue

        pattern = str(glob_field)
        matches = sorted((repo_root).glob(pattern))
        for matched in matches:
            if not matched.is_file():
                continue
            derived_name = f"{name}::{matched.stem}"
            targets.append(
                BenchmarkTarget(
                    suite=suite,
                    name=derived_name,
                    path=matched.resolve(),
                    optional=optional,
                )
            )

        if not matches:
            # glob 未命中时也加入一个“虚拟项”，后续统一走缺失告警流程
            targets.append(
                BenchmarkTarget(
                    suite=suite,
                    name=f"{name}::(glob-empty)",
                    path=(repo_root / pattern).resolve(),
                    optional=optional,
                )
            )

    return targets


def run_one(
    simulator: Path,
    target: BenchmarkTarget,
    mode: str,
    memory_size: int,
    max_instructions: int,
    max_ooo_cycles: int,
    timeout_sec: int,
) -> RunRecord:
    cmd = [
        str(simulator),
        "-e",
        "-m",
        str(memory_size),
        f"--max-instructions={max_instructions}",
        f"--max-ooo-cycles={max_ooo_cycles}",
        "--ooo" if mode == "ooo" else "--in-order",
        str(target.path),
    ]

    start = time.perf_counter()
    try:
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout_sec,
            check=False,
        )
        elapsed = time.perf_counter() - start
        merged_output = proc.stdout
        if proc.stderr:
            merged_output += "\n" + proc.stderr

        stats = parse_stats(merged_output)

        has_pass_marker = "=== TEST RESULT: PASS ===" in merged_output
        has_fail_marker = "=== TEST RESULT: FAIL ===" in merged_output

        # 仅以明确 PASS 标记作为成功条件，避免“到达指令/周期上限后依然返回 0”被误判为通过。
        if proc.returncode == 0 and has_pass_marker and not has_fail_marker:
            status = "passed"
        else:
            status = "failed"

        return RunRecord(
            suite=target.suite,
            benchmark=target.name,
            file=str(target.path),
            mode=mode,
            status=status,
            return_code=proc.returncode,
            elapsed_sec=elapsed,
            instructions=(
                int(stats["instructions"]) if stats["instructions"] is not None else None
            ),
            cycles=int(stats["cycles"]) if stats["cycles"] is not None else None,
            ipc=float(stats["ipc"]) if stats["ipc"] is not None else None,
            branch_mispredicts=(
                int(stats["branch_mispredicts"])
                if stats["branch_mispredicts"] is not None
                else None
            ),
            pipeline_stalls=(
                int(stats["pipeline_stalls"])
                if stats["pipeline_stalls"] is not None
                else None
            ),
        )
    except subprocess.TimeoutExpired:
        elapsed = time.perf_counter() - start
        return RunRecord(
            suite=target.suite,
            benchmark=target.name,
            file=str(target.path),
            mode=mode,
            status="timeout",
            return_code=-1,
            elapsed_sec=elapsed,
            instructions=None,
            cycles=None,
            ipc=None,
            branch_mispredicts=None,
            pipeline_stalls=None,
        )


def ensure_output_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def write_csv(path: Path, records: List[RunRecord]) -> None:
    fieldnames = [
        "suite",
        "benchmark",
        "file",
        "mode",
        "status",
        "return_code",
        "elapsed_sec",
        "instructions",
        "cycles",
        "ipc",
        "branch_mispredicts",
        "pipeline_stalls",
    ]
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for rec in records:
            writer.writerow(asdict(rec))


def write_json(path: Path, records: List[RunRecord], command: List[str]) -> None:
    payload: Dict[str, Any] = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "command": command,
        "records": [asdict(x) for x in records],
    }
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def print_summary(records: List[RunRecord]) -> None:
    total = len(records)
    passed = sum(1 for r in records if r.status == "passed")
    failed = sum(1 for r in records if r.status == "failed")
    timeout = sum(1 for r in records if r.status == "timeout")

    print("=== Benchmark Summary ===")
    print(f"Total: {total}")
    print(f"Passed: {passed}")
    print(f"Failed: {failed}")
    print(f"Timeout: {timeout}")

    by_mode: Dict[str, List[RunRecord]] = {}
    for rec in records:
        by_mode.setdefault(rec.mode, []).append(rec)

    for mode, mode_records in sorted(by_mode.items()):
        mode_passed = sum(1 for r in mode_records if r.status == "passed")
        print(f"[{mode}] {mode_passed}/{len(mode_records)} passed")


def main() -> int:
    parser = argparse.ArgumentParser(description="统一性能基准运行器")
    parser.add_argument(
        "--manifest",
        default="benchmarks/manifest/default.json",
        help="benchmark manifest 路径（相对仓库根目录）",
    )
    parser.add_argument(
        "--simulator",
        default="build/risc-v-sim",
        help="模拟器路径（相对仓库根目录）",
    )
    parser.add_argument(
        "--cpu-mode",
        choices=["in-order", "ooo", "both"],
        default="both",
        help="CPU 模式",
    )
    parser.add_argument(
        "--memory-size",
        type=int,
        default=2164260864,
        help="模拟器内存大小（字节）",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=60,
        help="每个 benchmark 的超时秒数",
    )
    parser.add_argument(
        "--max-instructions",
        type=int,
        default=5000000,
        help="In-order 指令上限，0 表示不限",
    )
    parser.add_argument(
        "--max-ooo-cycles",
        type=int,
        default=50000,
        help="OOO 周期上限，0 表示不限",
    )
    parser.add_argument(
        "--output-dir",
        default="benchmarks/results/latest",
        help="输出目录（CSV/JSON）",
    )
    parser.add_argument(
        "--filter",
        default="*",
        help="按 benchmark 名称过滤，支持 shell 通配符（默认 *）",
    )
    parser.add_argument(
        "--strict-missing",
        action="store_true",
        help="若 manifest 中存在缺失文件则直接失败",
    )

    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    manifest_path = (repo_root / args.manifest).resolve()
    simulator_path = (repo_root / args.simulator).resolve()
    output_dir = (repo_root / args.output_dir).resolve()

    if not manifest_path.exists():
        print(f"[ERROR] manifest 不存在: {manifest_path}")
        return 1

    if not simulator_path.exists():
        print(f"[ERROR] 模拟器不存在: {simulator_path}")
        return 1

    try:
        targets = load_manifest(manifest_path, repo_root)
    except Exception as exc:  # pylint: disable=broad-except
        print(f"[ERROR] 读取 manifest 失败: {exc}")
        return 1

    if args.filter != "*":
        targets = [t for t in targets if fnmatch.fnmatch(t.name, args.filter)]

    modes = ["in-order", "ooo"] if args.cpu_mode == "both" else [args.cpu_mode]

    missing_required = 0
    runnable_targets: List[BenchmarkTarget] = []
    for t in targets:
        if t.path.exists() and t.path.is_file():
            runnable_targets.append(t)
        else:
            level = "WARN" if t.optional else "ERROR"
            print(f"[{level}] benchmark 缺失: {t.name} -> {t.path}")
            if not t.optional:
                missing_required += 1

    if missing_required > 0 and args.strict_missing:
        print(f"[ERROR] 缺失必选 benchmark: {missing_required} 个")
        return 1

    if not runnable_targets:
        print("[ERROR] 没有可运行 benchmark")
        return 1

    records: List[RunRecord] = []
    for mode in modes:
        for idx, target in enumerate(runnable_targets, start=1):
            print(
                f"[RUN] ({mode}) {idx}/{len(runnable_targets)} "
                f"{target.suite}:{target.name}"
            )
            rec = run_one(
                simulator=simulator_path,
                target=target,
                mode=mode,
                memory_size=args.memory_size,
                max_instructions=args.max_instructions,
                max_ooo_cycles=args.max_ooo_cycles,
                timeout_sec=args.timeout,
            )
            records.append(rec)
            print(
                f"[DONE] ({mode}) {target.name} status={rec.status} "
                f"rc={rec.return_code} elapsed={rec.elapsed_sec:.2f}s"
            )

    ensure_output_dir(output_dir)
    csv_path = output_dir / "results.csv"
    json_path = output_dir / "results.json"

    write_csv(csv_path, records)
    write_json(json_path, records, command=sys.argv)

    print_summary(records)
    print(f"CSV:  {csv_path}")
    print(f"JSON: {json_path}")

    if any(r.status != "passed" for r in records):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
