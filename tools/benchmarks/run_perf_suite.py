#!/usr/bin/env python3
"""统一性能基准运行器。

功能：
- 从 manifest 读取基准项（本地 path 或 glob）
- 运行模拟器（in-order / ooo / both）
- 解析关键统计（Instructions/Cycles/IPC/branch_mispredicts/pipeline_stalls）
- 为每个 benchmark/mode 自动落盘详细 stats 文件
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
RE_DETAIL_SCALAR = re.compile(r"^(\S+)\s+([0-9]+(?:\.[0-9]+)?)\s+#", re.MULTILINE)


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
    stats_path: Optional[str]
    topdown_executing_pct: Optional[float]
    topdown_frontend_bound_pct: Optional[float]
    topdown_backend_bound_pct: Optional[float]
    issue_slots: Optional[int]
    issue_utilized_slots: Optional[int]
    commit_slots: Optional[int]
    commit_utilized_slots: Optional[int]
    rob_occupancy_avg: Optional[float]
    store_buffer_occupancy_avg: Optional[float]
    l1i_hits: Optional[int]
    l1i_misses: Optional[int]
    l1i_stall_cycles: Optional[int]
    l1d_hits: Optional[int]
    l1d_misses: Optional[int]
    l1d_stall_cycles_load: Optional[int]
    l1d_stall_cycles_store: Optional[int]
    l1d_prefetch_requests: Optional[int]
    l1d_prefetch_issued: Optional[int]
    l1d_prefetch_useful_hits: Optional[int]
    l1d_prefetch_unused_evictions: Optional[int]
    l1d_prefetch_dropped_already_resident: Optional[int]
    l1d_prefetch_dropped_set_throttle: Optional[int]
    load_replays: Optional[int]
    load_replays_rob_store_addr_unknown: Optional[int]
    load_replays_rob_store_overlap: Optional[int]
    load_replays_store_buffer_overlap: Optional[int]
    loads_blocked_by_store: Optional[int]
    predictor_control_incorrect: Optional[int]
    predictor_jalr_mispredicts: Optional[int]
    branch_profile_top0: Optional[str]
    jalr_profile_top0: Optional[str]
    load_profile_top0: Optional[str]
    store_profile_top0: Optional[str]


DETAIL_INT_FIELDS = {
    "issue_slots": "cpu.issue.slots",
    "issue_utilized_slots": "cpu.issue.utilized_slots",
    "commit_slots": "cpu.commit.slots",
    "commit_utilized_slots": "cpu.commit.utilized_slots",
    "l1i_hits": "cpu.cache.l1i.hits",
    "l1i_misses": "cpu.cache.l1i.misses",
    "l1i_stall_cycles": "cpu.cache.l1i.stall_cycles",
    "l1d_hits": "cpu.cache.l1d.hits",
    "l1d_misses": "cpu.cache.l1d.misses",
    "l1d_stall_cycles_load": "cpu.cache.l1d.stall_cycles_load",
    "l1d_stall_cycles_store": "cpu.cache.l1d.stall_cycles_store",
    "l1d_prefetch_requests": "cpu.cache.l1d.prefetch_requests",
    "l1d_prefetch_issued": "cpu.cache.l1d.prefetch_issued",
    "l1d_prefetch_useful_hits": "cpu.cache.l1d.prefetch_useful_hits",
    "l1d_prefetch_unused_evictions": "cpu.cache.l1d.prefetch_unused_evictions",
    "l1d_prefetch_dropped_already_resident": "cpu.cache.l1d.prefetch_dropped_already_resident",
    "l1d_prefetch_dropped_set_throttle": "cpu.cache.l1d.prefetch_dropped_set_throttle",
    "load_replays": "cpu.memory.load_replays",
    "load_replays_rob_store_addr_unknown": "cpu.memory.load_replays.rob_store_addr_unknown",
    "load_replays_rob_store_overlap": "cpu.memory.load_replays.rob_store_overlap",
    "load_replays_store_buffer_overlap": "cpu.memory.load_replays.store_buffer_overlap",
    "loads_blocked_by_store": "cpu.memory.loads_blocked_by_store",
    "predictor_control_incorrect": "cpu.predictor.control.incorrect",
    "predictor_jalr_mispredicts": "cpu.predictor.jalr.mispredicts",
}

DETAIL_FLOAT_FIELDS = {
    "topdown_executing_pct": "cpu.topdown.cycles.executing_pct",
    "topdown_frontend_bound_pct": "cpu.topdown.cycles.frontend_bound_pct",
    "topdown_backend_bound_pct": "cpu.topdown.cycles.backend_bound_pct",
    "rob_occupancy_avg": "cpu.rob.occupancy_avg",
    "store_buffer_occupancy_avg": "cpu.store_buffer.occupancy_avg",
}

DETAIL_PROFILE_FIELDS = {
    "branch_profile_top0": "cpu.branch_profile.top[0].pc",
    "jalr_profile_top0": "cpu.jalr_profile.top[0].pc",
    "load_profile_top0": "cpu.load_profile.top[0].pc",
    "store_profile_top0": "cpu.store_profile.top[0].pc",
}


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


def _maybe_int(value: Optional[str]) -> Optional[int]:
    if value is None:
        return None
    return int(float(value))


def _maybe_float(value: Optional[str]) -> Optional[float]:
    if value is None:
        return None
    return float(value)


def parse_detailed_stats_text(text: str) -> Dict[str, Any]:
    scalar_values: Dict[str, str] = {}
    for key, value in RE_DETAIL_SCALAR.findall(text):
        scalar_values[key] = value

    profile_values: Dict[str, Optional[str]] = {}
    lines = text.splitlines()
    for field_name, prefix in DETAIL_PROFILE_FIELDS.items():
        profile_values[field_name] = next((line for line in lines if line.startswith(prefix)), None)

    selected_metrics: Dict[str, Any] = {}
    for field_name, detail_key in DETAIL_INT_FIELDS.items():
        selected_metrics[field_name] = _maybe_int(scalar_values.get(detail_key))
    for field_name, detail_key in DETAIL_FLOAT_FIELDS.items():
        selected_metrics[field_name] = _maybe_float(scalar_values.get(detail_key))
    selected_metrics.update(profile_values)

    basic_fallback = {
        "instructions": _maybe_int(scalar_values.get("cpu.commit.retired")),
        "cycles": _maybe_int(scalar_values.get("cpu.cycles")),
        "ipc": _maybe_float(scalar_values.get("cpu.ipc")),
        "branch_mispredicts": _maybe_int(
            scalar_values.get("cpu.branch.conditional_mispredicts")
        ),
        "pipeline_stalls": _maybe_int(scalar_values.get("cpu.stall.total")),
    }

    return {
        "scalars": scalar_values,
        "selected_metrics": selected_metrics,
        "basic_fallback": basic_fallback,
    }


def merge_basic_stats(
    basic_stats: Dict[str, Optional[float]], detail_stats: Dict[str, Any]
) -> Dict[str, Optional[float]]:
    merged = dict(basic_stats)
    fallback = detail_stats.get("basic_fallback", {})
    for key in ("instructions", "cycles", "ipc", "branch_mispredicts", "pipeline_stalls"):
        if merged.get(key) is None:
            merged[key] = fallback.get(key)
    if merged.get("ipc") is None and merged.get("instructions") is not None and merged.get("cycles"):
        cycles = merged["cycles"]
        if cycles:
            merged["ipc"] = float(merged["instructions"]) / float(cycles)
    return merged


def sanitize_path_component(value: str) -> str:
    sanitized = re.sub(r"[^A-Za-z0-9._-]+", "_", value)
    return sanitized.strip("._") or "item"


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
    stats_path: Path,
    ooo_l1d_prefetch: str,
) -> RunRecord:
    cmd = [
        str(simulator),
        "-e",
        "-m",
        str(memory_size),
        f"--max-instructions={max_instructions}",
        f"--max-ooo-cycles={max_ooo_cycles}",
        "--ooo" if mode == "ooo" else "--in-order",
    ]
    if mode == "ooo":
        cmd.append(f"--stats-file={stats_path}")
        if ooo_l1d_prefetch != "auto":
            cmd.append(f"--l1d-next-line-prefetch={ooo_l1d_prefetch}")
    cmd.append(str(target.path))

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

        if mode != "ooo":
            stats_path.write_text(merged_output, encoding="utf-8")
        elif not stats_path.exists():
            stats_path.write_text(merged_output, encoding="utf-8")

        detail_stats = parse_detailed_stats_text(stats_path.read_text(encoding="utf-8"))
        stats = merge_basic_stats(parse_stats(merged_output), detail_stats)

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
            stats_path=str(stats_path),
            topdown_executing_pct=detail_stats["selected_metrics"]["topdown_executing_pct"],
            topdown_frontend_bound_pct=detail_stats["selected_metrics"]["topdown_frontend_bound_pct"],
            topdown_backend_bound_pct=detail_stats["selected_metrics"]["topdown_backend_bound_pct"],
            issue_slots=detail_stats["selected_metrics"]["issue_slots"],
            issue_utilized_slots=detail_stats["selected_metrics"]["issue_utilized_slots"],
            commit_slots=detail_stats["selected_metrics"]["commit_slots"],
            commit_utilized_slots=detail_stats["selected_metrics"]["commit_utilized_slots"],
            rob_occupancy_avg=detail_stats["selected_metrics"]["rob_occupancy_avg"],
            store_buffer_occupancy_avg=detail_stats["selected_metrics"]["store_buffer_occupancy_avg"],
            l1i_hits=detail_stats["selected_metrics"]["l1i_hits"],
            l1i_misses=detail_stats["selected_metrics"]["l1i_misses"],
            l1i_stall_cycles=detail_stats["selected_metrics"]["l1i_stall_cycles"],
            l1d_hits=detail_stats["selected_metrics"]["l1d_hits"],
            l1d_misses=detail_stats["selected_metrics"]["l1d_misses"],
            l1d_stall_cycles_load=detail_stats["selected_metrics"]["l1d_stall_cycles_load"],
            l1d_stall_cycles_store=detail_stats["selected_metrics"]["l1d_stall_cycles_store"],
            l1d_prefetch_requests=detail_stats["selected_metrics"]["l1d_prefetch_requests"],
            l1d_prefetch_issued=detail_stats["selected_metrics"]["l1d_prefetch_issued"],
            l1d_prefetch_useful_hits=detail_stats["selected_metrics"]["l1d_prefetch_useful_hits"],
            l1d_prefetch_unused_evictions=detail_stats["selected_metrics"]["l1d_prefetch_unused_evictions"],
            l1d_prefetch_dropped_already_resident=detail_stats["selected_metrics"][
                "l1d_prefetch_dropped_already_resident"
            ],
            l1d_prefetch_dropped_set_throttle=detail_stats["selected_metrics"][
                "l1d_prefetch_dropped_set_throttle"
            ],
            load_replays=detail_stats["selected_metrics"]["load_replays"],
            load_replays_rob_store_addr_unknown=detail_stats["selected_metrics"][
                "load_replays_rob_store_addr_unknown"
            ],
            load_replays_rob_store_overlap=detail_stats["selected_metrics"][
                "load_replays_rob_store_overlap"
            ],
            load_replays_store_buffer_overlap=detail_stats["selected_metrics"][
                "load_replays_store_buffer_overlap"
            ],
            loads_blocked_by_store=detail_stats["selected_metrics"]["loads_blocked_by_store"],
            predictor_control_incorrect=detail_stats["selected_metrics"][
                "predictor_control_incorrect"
            ],
            predictor_jalr_mispredicts=detail_stats["selected_metrics"][
                "predictor_jalr_mispredicts"
            ],
            branch_profile_top0=detail_stats["selected_metrics"]["branch_profile_top0"],
            jalr_profile_top0=detail_stats["selected_metrics"]["jalr_profile_top0"],
            load_profile_top0=detail_stats["selected_metrics"]["load_profile_top0"],
            store_profile_top0=detail_stats["selected_metrics"]["store_profile_top0"],
        )
    except subprocess.TimeoutExpired:
        elapsed = time.perf_counter() - start
        if not stats_path.exists():
            stats_path.write_text(
                f"# timeout after {timeout_sec}s while running {target.name} ({mode})\n",
                encoding="utf-8",
            )
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
            stats_path=str(stats_path),
            topdown_executing_pct=None,
            topdown_frontend_bound_pct=None,
            topdown_backend_bound_pct=None,
            issue_slots=None,
            issue_utilized_slots=None,
            commit_slots=None,
            commit_utilized_slots=None,
            rob_occupancy_avg=None,
            store_buffer_occupancy_avg=None,
            l1i_hits=None,
            l1i_misses=None,
            l1i_stall_cycles=None,
            l1d_hits=None,
            l1d_misses=None,
            l1d_stall_cycles_load=None,
            l1d_stall_cycles_store=None,
            l1d_prefetch_requests=None,
            l1d_prefetch_issued=None,
            l1d_prefetch_useful_hits=None,
            l1d_prefetch_unused_evictions=None,
            l1d_prefetch_dropped_already_resident=None,
            l1d_prefetch_dropped_set_throttle=None,
            load_replays=None,
            load_replays_rob_store_addr_unknown=None,
            load_replays_rob_store_overlap=None,
            load_replays_store_buffer_overlap=None,
            loads_blocked_by_store=None,
            predictor_control_incorrect=None,
            predictor_jalr_mispredicts=None,
            branch_profile_top0=None,
            jalr_profile_top0=None,
            load_profile_top0=None,
            store_profile_top0=None,
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
        "stats_path",
        "topdown_executing_pct",
        "topdown_frontend_bound_pct",
        "topdown_backend_bound_pct",
        "issue_slots",
        "issue_utilized_slots",
        "commit_slots",
        "commit_utilized_slots",
        "rob_occupancy_avg",
        "store_buffer_occupancy_avg",
        "l1i_hits",
        "l1i_misses",
        "l1i_stall_cycles",
        "l1d_hits",
        "l1d_misses",
        "l1d_stall_cycles_load",
        "l1d_stall_cycles_store",
        "l1d_prefetch_requests",
        "l1d_prefetch_issued",
        "l1d_prefetch_useful_hits",
        "l1d_prefetch_unused_evictions",
        "l1d_prefetch_dropped_already_resident",
        "l1d_prefetch_dropped_set_throttle",
        "load_replays",
        "load_replays_rob_store_addr_unknown",
        "load_replays_rob_store_overlap",
        "load_replays_store_buffer_overlap",
        "loads_blocked_by_store",
        "predictor_control_incorrect",
        "predictor_jalr_mispredicts",
        "branch_profile_top0",
        "jalr_profile_top0",
        "load_profile_top0",
        "store_profile_top0",
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
    parser.add_argument(
        "--ooo-l1d-prefetch",
        choices=["auto", "on", "off"],
        default="auto",
        help="OOO L1D next-line prefetcher 开关（默认 auto，沿用模拟器默认值）",
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

    ensure_output_dir(output_dir)
    stats_dir = output_dir / "stats"
    ensure_output_dir(stats_dir)

    records: List[RunRecord] = []
    for mode in modes:
        for idx, target in enumerate(runnable_targets, start=1):
            print(
                f"[RUN] ({mode}) {idx}/{len(runnable_targets)} "
                f"{target.suite}:{target.name}"
            )
            stats_path = stats_dir / (
                f"{mode}-"
                f"{idx:03d}-"
                f"{sanitize_path_component(target.suite)}-"
                f"{sanitize_path_component(target.name)}.stats"
            )
            rec = run_one(
                simulator=simulator_path,
                target=target,
                mode=mode,
                memory_size=args.memory_size,
                max_instructions=args.max_instructions,
                max_ooo_cycles=args.max_ooo_cycles,
                timeout_sec=args.timeout,
                stats_path=stats_path,
                ooo_l1d_prefetch=args.ooo_l1d_prefetch,
            )
            records.append(rec)
            print(
                f"[DONE] ({mode}) {target.name} status={rec.status} "
                f"rc={rec.return_code} elapsed={rec.elapsed_sec:.2f}s"
            )

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
