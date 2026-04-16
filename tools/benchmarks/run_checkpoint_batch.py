#!/usr/bin/env python3
"""SPEC06 checkpoint 批量并行运行器。"""

from __future__ import annotations

import argparse
import csv
import fnmatch
import json
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple


@dataclass(frozen=True)
class CheckpointListEntry:
    entry_name: str
    relative_point_dir: Path
    raw_columns: Tuple[str, ...]

    @property
    def workload_name(self) -> str:
        return self.relative_point_dir.parts[0] if len(self.relative_point_dir.parts) >= 1 else ""

    @property
    def slice_id(self) -> str:
        return self.relative_point_dir.parts[1] if len(self.relative_point_dir.parts) >= 2 else ""


@dataclass
class BatchRecord:
    entry_name: str
    workload_name: str
    slice_id: str
    checkpoint_path: str
    output_dir: str
    status: str
    success: bool
    failure_reason: str
    message: str
    instructions_measure: int
    cycles_measure: int
    ipc_measure: float
    weight: float
    return_code: int


def parse_checkpoint_list(list_path: Path) -> List[CheckpointListEntry]:
    entries: List[CheckpointListEntry] = []
    for line_number, raw_line in enumerate(list_path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        columns = tuple(line.split())
        if len(columns) < 2:
            raise ValueError(f"checkpoint list 第 {line_number} 行格式非法: {raw_line}")
        relative_point_dir = Path(columns[1])
        if relative_point_dir.is_absolute() or any(part == ".." for part in relative_point_dir.parts):
            raise ValueError(f"checkpoint list 第 {line_number} 行包含非法路径: {columns[1]}")
        if len(relative_point_dir.parts) != 2:
            raise ValueError(f"checkpoint list 第 {line_number} 行路径层级非法: {columns[1]}")
        entries.append(
            CheckpointListEntry(
                entry_name=columns[0],
                relative_point_dir=relative_point_dir,
                raw_columns=columns,
            )
        )
    return entries


def parse_specific_benchmarks(value: str) -> List[str]:
    if not value.strip():
        return []
    return [item.strip() for item in value.split(",") if item.strip()]


def filter_entries(entries: Sequence[CheckpointListEntry], patterns: Sequence[str]) -> List[CheckpointListEntry]:
    if not patterns:
        return list(entries)
    matched: List[CheckpointListEntry] = []
    for entry in entries:
        if any(
            fnmatch.fnmatch(entry.entry_name, pattern)
            or fnmatch.fnmatch(entry.workload_name, pattern)
            or fnmatch.fnmatch(entry.relative_point_dir.as_posix(), pattern)
            for pattern in patterns
        ):
            matched.append(entry)
    return matched


def resolve_checkpoint_path(checkpoint_root: Path, entry: CheckpointListEntry) -> Path:
    point_dir = checkpoint_root / entry.relative_point_dir
    matches = sorted(point_dir.glob("*.zstd"))
    if not matches:
        raise ValueError(f"未找到 checkpoint 文件: {point_dir}")
    if len(matches) > 1:
        raise ValueError(f"存在多个 checkpoint 文件: {point_dir}")
    return matches[0]


def load_cluster_weights(cluster_path: Optional[Path]) -> Dict[Tuple[str, str], float]:
    if cluster_path is None:
        return {}
    raw_data = json.loads(cluster_path.read_text(encoding="utf-8"))
    weights: Dict[Tuple[str, str], float] = {}
    for workload_name, workload_info in raw_data.items():
        if not isinstance(workload_info, dict):
            continue
        points = workload_info.get("points", {})
        if not isinstance(points, dict):
            continue
        for slice_id, weight in points.items():
            try:
                weights[(str(workload_name), str(slice_id))] = float(weight)
            except (TypeError, ValueError):
                continue
    return weights


def build_simulator_command(args: argparse.Namespace, checkpoint_path: Path, output_dir: Path) -> List[str]:
    command = [
        str(args.simulator),
        "--checkpoint=" + str(checkpoint_path),
        "--checkpoint-output-dir=" + str(output_dir),
        "--warmup-instructions=" + str(args.warmup_instructions),
        "--measure-instructions=" + str(args.measure_instructions),
    ]
    if args.cpu_mode == "in-order":
        command.append("--in-order")
    else:
        command.append("--ooo")
    if args.checkpoint_importer:
        command.append("--checkpoint-importer=" + args.checkpoint_importer)
    if args.checkpoint_restorer:
        command.append("--checkpoint-restorer=" + str(args.checkpoint_restorer))
    if args.checkpoint_recipe:
        command.append("--checkpoint-recipe=" + str(args.checkpoint_recipe))
    for extra_arg in args.extra_sim_arg:
        command.append(extra_arg)
    return command


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def read_result_json(result_path: Path) -> Dict[str, object]:
    if not result_path.exists():
        raise ValueError(f"缺少 result.json: {result_path}")
    data = json.loads(result_path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"result.json 格式非法: {result_path}")
    return data


def synthesize_failure_record(entry: CheckpointListEntry,
                              checkpoint_path: Optional[Path],
                              output_dir: Path,
                              message: str,
                              failure_reason: str,
                              return_code: int) -> BatchRecord:
    return BatchRecord(
        entry_name=entry.entry_name,
        workload_name=entry.workload_name,
        slice_id=entry.slice_id,
        checkpoint_path="" if checkpoint_path is None else str(checkpoint_path),
        output_dir=str(output_dir),
        status="abort",
        success=False,
        failure_reason=failure_reason,
        message=message,
        instructions_measure=0,
        cycles_measure=0,
        ipc_measure=0.0,
        weight=0.0,
        return_code=return_code,
    )


def clear_stale_entry_outputs(output_dir: Path) -> None:
    for name in (
        "command.txt",
        "stdout.log",
        "stderr.log",
        "stats.txt",
        "summary.csv",
        "result.json",
        "completed",
        "abort",
        "error.txt",
    ):
        path = output_dir / name
        if path.exists():
            path.unlink()


def write_failure_artifacts(output_dir: Path, record: BatchRecord) -> None:
    write_text(output_dir / "error.txt", record.message + "\n")
    write_text(output_dir / "abort", "")
    payload = {
        "status": record.status,
        "success": record.success,
        "failure_reason": record.failure_reason,
        "message": record.message,
        "benchmark": record.workload_name,
        "workload_name": record.workload_name,
        "slice_id": record.slice_id,
        "weight": record.weight,
        "instructions_measure": record.instructions_measure,
        "cycles_measure": record.cycles_measure,
        "ipc_measure": record.ipc_measure,
        "checkpoint_path": record.checkpoint_path,
        "entry_name": record.entry_name,
        "output_dir": record.output_dir,
        "return_code": record.return_code,
    }
    write_text(output_dir / "result.json", json.dumps(payload, ensure_ascii=True) + "\n")


def run_single_entry(args: argparse.Namespace, checkpoint_root: Path, entry: CheckpointListEntry) -> BatchRecord:
    output_dir = args.output_dir / entry.entry_name
    output_dir.mkdir(parents=True, exist_ok=True)
    clear_stale_entry_outputs(output_dir)
    stdout_path = output_dir / "stdout.log"
    stderr_path = output_dir / "stderr.log"
    command_path = output_dir / "command.txt"

    checkpoint_path: Optional[Path] = None
    try:
        checkpoint_path = resolve_checkpoint_path(checkpoint_root, entry)
        command = build_simulator_command(args, checkpoint_path, output_dir)
        write_text(command_path, shlex.join(command) + "\n")
    except ValueError as exc:
        write_text(command_path, f"# unresolved\n{exc}\n")
        write_text(stdout_path, "")
        write_text(stderr_path, str(exc) + "\n")
        record = synthesize_failure_record(
            entry,
            checkpoint_path,
            output_dir,
            str(exc),
            "import_error",
            1,
        )
        write_failure_artifacts(output_dir, record)
        return record

    try:
        with stdout_path.open("w", encoding="utf-8") as stdout_stream, stderr_path.open(
            "w", encoding="utf-8"
        ) as stderr_stream:
            completed = subprocess.run(
                command,
                stdout=stdout_stream,
                stderr=stderr_stream,
                text=True,
                check=False,
            )
    except OSError as exc:
        record = synthesize_failure_record(
            entry,
            checkpoint_path,
            output_dir,
            str(exc),
            "spawn_error",
            1,
        )
        write_failure_artifacts(output_dir, record)
        return record

    try:
        result_data = read_result_json(output_dir / "result.json")
        return BatchRecord(
            entry_name=entry.entry_name,
            workload_name=str(result_data.get("workload_name", entry.workload_name)),
            slice_id=str(result_data.get("slice_id", entry.slice_id)),
            checkpoint_path=str(checkpoint_path),
            output_dir=str(output_dir),
            status=str(result_data.get("status", "abort")),
            success=bool(result_data.get("success", False)),
            failure_reason=str(result_data.get("failure_reason", "unknown")),
            message=str(result_data.get("message", "")),
            instructions_measure=int(result_data.get("instructions_measure", 0)),
            cycles_measure=int(result_data.get("cycles_measure", 0)),
            ipc_measure=float(result_data.get("ipc_measure", 0.0)),
            weight=float(result_data.get("weight", 0.0)),
            return_code=int(completed.returncode),
        )
    except Exception as exc:
        stderr_text = stderr_path.read_text(encoding="utf-8") if stderr_path.exists() else ""
        message = f"无法解析单切片结果: {exc}"
        if stderr_text:
            message += f"; stderr={stderr_text.strip()}"
        record = synthesize_failure_record(
            entry,
            checkpoint_path,
            output_dir,
            message,
            "missing_result",
            int(completed.returncode),
        )
        write_failure_artifacts(output_dir, record)
        return record


def write_batch_summary_csv(output_dir: Path, records: Sequence[BatchRecord]) -> None:
    fieldnames = [
        "entry_name",
        "workload_name",
        "slice_id",
        "checkpoint_path",
        "output_dir",
        "status",
        "success",
        "failure_reason",
        "message",
        "instructions_measure",
        "cycles_measure",
        "ipc_measure",
        "weight",
        "return_code",
    ]
    with (output_dir / "batch_summary.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow(asdict(record))


def write_batch_summary_json(output_dir: Path, records: Sequence[BatchRecord]) -> None:
    payload = [asdict(record) for record in records]
    write_text(output_dir / "batch_summary.json", json.dumps(payload, indent=2, ensure_ascii=True) + "\n")


def compute_weighted_ipc(records: Sequence[BatchRecord],
                         cluster_weights: Dict[Tuple[str, str], float]) -> float:
    weighted_sum = 0.0
    total_weight = 0.0
    for record in records:
        if not record.success:
            continue
        weight = cluster_weights.get((record.workload_name, record.slice_id), record.weight)
        if weight <= 0.0:
            continue
        weighted_sum += weight * record.ipc_measure
        total_weight += weight
    return (weighted_sum / total_weight) if total_weight > 0.0 else 0.0


def write_aggregate_json(output_dir: Path,
                         records: Sequence[BatchRecord],
                         cluster_weights: Dict[Tuple[str, str], float]) -> None:
    success_records = [record for record in records if record.success]
    total_instructions = sum(record.instructions_measure for record in success_records)
    total_cycles = sum(record.cycles_measure for record in success_records)
    aggregate_ipc = (float(total_instructions) / float(total_cycles)) if total_cycles > 0 else 0.0
    failed_entries = [record.entry_name for record in records if not record.success]
    failure_reasons = {
        record.entry_name: record.failure_reason for record in records if not record.success
    }
    aggregate = {
        "total": len(records),
        "success_count": len(success_records),
        "failure_count": len(records) - len(success_records),
        "success_rate": (len(success_records) / len(records)) if records else 0.0,
        "total_instructions_measure": total_instructions,
        "total_cycles_measure": total_cycles,
        "aggregate_ipc_by_sum": aggregate_ipc,
        "weighted_ipc": compute_weighted_ipc(records, cluster_weights),
        "failed_entries": failed_entries,
        "failure_reasons": failure_reasons,
    }
    write_text(output_dir / "aggregate.json", json.dumps(aggregate, indent=2, ensure_ascii=True) + "\n")


def run_batch(args: argparse.Namespace) -> int:
    checkpoint_root = Path(args.checkpoint_root).resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    entries = parse_checkpoint_list(args.checkpoint_list)
    patterns = parse_specific_benchmarks(args.specific_benchmarks)
    entries = filter_entries(entries, patterns)
    if patterns and not entries:
        raise ValueError("specific-benchmarks 未匹配到任何 checkpoint 条目")
    cluster_weights = load_cluster_weights(args.cluster_config)

    records: List[BatchRecord] = []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
        futures = [executor.submit(run_single_entry, args, checkpoint_root, entry) for entry in entries]
        for future in futures:
            records.append(future.result())

    records.sort(key=lambda record: record.entry_name)
    write_batch_summary_csv(args.output_dir, records)
    write_batch_summary_json(args.output_dir, records)
    write_aggregate_json(args.output_dir, records, cluster_weights)

    return 0 if all(record.success for record in records) else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run SPEC06 checkpoints in parallel")
    parser.add_argument("--simulator", type=Path, required=True, help="Path to risc-v-sim or compatible wrapper")
    parser.add_argument("--checkpoint-list", type=Path, required=True, help="Path to checkpoint list")
    parser.add_argument("--checkpoint-root", type=Path, required=True, help="Checkpoint root directory")
    parser.add_argument("--output-dir", type=Path, required=True, help="Batch output directory")
    parser.add_argument("--jobs", type=int, default=1, help="Parallel worker count")
    parser.add_argument("--warmup-instructions", type=int, default=20_000_000)
    parser.add_argument("--measure-instructions", type=int, default=20_000_000)
    parser.add_argument("--cpu-mode", choices=("in-order", "ooo"), default="in-order")
    parser.add_argument("--cluster-config", type=Path, default=None)
    parser.add_argument("--checkpoint-importer", default="")
    parser.add_argument("--checkpoint-restorer", type=Path, default=None)
    parser.add_argument("--checkpoint-recipe", type=Path, default=None)
    parser.add_argument("--specific-benchmarks", default="")
    parser.add_argument("--extra-sim-arg", action="append", default=[])
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return run_batch(args)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
