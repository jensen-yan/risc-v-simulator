import csv
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
BATCH_RUNNER_PATH = REPO_ROOT / "tools" / "benchmarks" / "run_checkpoint_batch.py"


def load_batch_runner_module():
    if not BATCH_RUNNER_PATH.exists():
        raise AssertionError(f"batch runner script 不存在: {BATCH_RUNNER_PATH}")
    spec = importlib.util.spec_from_file_location("run_checkpoint_batch", BATCH_RUNNER_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError("无法加载 batch runner 模块")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class CheckpointBatchRunnerTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = Path(tempfile.mkdtemp(prefix="checkpoint_batch_runner_test_"))
        self.checkpoint_root = self.temp_dir / "checkpoints"
        self.output_dir = self.temp_dir / "output"
        self.checkpoint_root.mkdir(parents=True)

    def tearDown(self):
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    def create_checkpoint(self, workload: str, point_id: str, weight: str = "0.100000") -> Path:
        point_dir = self.checkpoint_root / workload / point_id
        point_dir.mkdir(parents=True, exist_ok=True)
        checkpoint_path = point_dir / f"_{point_id}_{weight}_.zstd"
        checkpoint_path.write_bytes(b"fake zstd payload")
        return checkpoint_path

    def write_checkpoint_list(self, lines):
        list_path = self.temp_dir / "spec06_0.3c.lst"
        list_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return list_path

    def write_cluster_config(self):
        cluster_path = self.temp_dir / "cluster.json"
        cluster_path.write_text(
            json.dumps(
                {
                    "bzip2_source": {"insts": "100", "points": {"555": "0.25"}},
                    "bzip2_html": {"insts": "100", "points": {"7052": "0.75"}},
                }
            ),
            encoding="utf-8",
        )
        return cluster_path

    def write_fake_simulator(self):
        script_path = self.temp_dir / "fake_simulator.py"
        script_path.write_text(
            textwrap.dedent(
                """\
                #!/usr/bin/env python3
                import json
                import sys
                from pathlib import Path

                def parse_arg(prefix: str):
                    for arg in sys.argv[1:]:
                        if arg.startswith(prefix):
                            return arg[len(prefix):]
                    raise SystemExit(f"missing arg: {prefix}")

                checkpoint = Path(parse_arg("--checkpoint="))
                output_dir = Path(parse_arg("--checkpoint-output-dir="))
                output_dir.mkdir(parents=True, exist_ok=True)

                point_dir = checkpoint.parent
                workload_dir = point_dir.parent
                point_id = point_dir.name
                workload = workload_dir.name
                entry_name = f"{workload}_{point_id}"

                if entry_name == "failbench_2":
                    result = {
                        "status": "abort",
                        "success": False,
                        "failure_reason": "illegal_instruction",
                        "message": "fake illegal instruction",
                        "benchmark": workload,
                        "workload_name": workload,
                        "slice_id": point_id,
                        "weight": 0.125,
                        "instructions_measure": 0,
                        "cycles_measure": 0,
                        "ipc_measure": 0.0,
                    }
                    (output_dir / "abort").write_text("", encoding="utf-8")
                    (output_dir / "error.txt").write_text("fake illegal instruction\\n", encoding="utf-8")
                    exit_code = 1
                elif entry_name == "bzip2_source_555":
                    result = {
                        "status": "completed",
                        "success": True,
                        "failure_reason": "none",
                        "message": "ok",
                        "benchmark": workload,
                        "workload_name": workload,
                        "slice_id": point_id,
                        "weight": 0.125,
                        "instructions_measure": 1000,
                        "cycles_measure": 500,
                        "ipc_measure": 2.0,
                    }
                    (output_dir / "completed").write_text("", encoding="utf-8")
                    exit_code = 0
                else:
                    result = {
                        "status": "completed",
                        "success": True,
                        "failure_reason": "none",
                        "message": "ok",
                        "benchmark": workload,
                        "workload_name": workload,
                        "slice_id": point_id,
                        "weight": 0.500,
                        "instructions_measure": 300,
                        "cycles_measure": 600,
                        "ipc_measure": 0.5,
                    }
                    (output_dir / "completed").write_text("", encoding="utf-8")
                    exit_code = 0

                (output_dir / "stats.txt").write_text("fake stats\\n", encoding="utf-8")
                (output_dir / "summary.csv").write_text(
                    "status,success,failure_reason,benchmark,workload_name,slice_id,weight,instructions_measure,cycles_measure,ipc_measure\\n",
                    encoding="utf-8",
                )
                (output_dir / "result.json").write_text(json.dumps(result), encoding="utf-8")

                print(f"fake simulator stdout: {entry_name}")
                print(f"fake simulator stderr: {entry_name}", file=sys.stderr)
                sys.exit(exit_code)
                """
            ),
            encoding="utf-8",
        )
        script_path.chmod(0o755)
        return script_path

    def write_exit_only_simulator(self):
        script_path = self.temp_dir / "exit_only_simulator.py"
        script_path.write_text(
            textwrap.dedent(
                """\
                #!/usr/bin/env python3
                import sys
                print("simulator exited before writing result", file=sys.stderr)
                sys.exit(17)
                """
            ),
            encoding="utf-8",
        )
        script_path.chmod(0o755)
        return script_path

    def test_parse_checkpoint_list_and_resolve_checkpoint_path(self):
        self.create_checkpoint("bzip2_source", "555", "0.026526")
        self.create_checkpoint("bzip2_html", "7052", "0.302542")
        list_path = self.write_checkpoint_list(
            [
                "# comment",
                "",
                "bzip2_source_555 bzip2_source/555 0 0 20 20",
                "bzip2_html_7052 bzip2_html/7052 0 0 20 20",
            ]
        )

        module = load_batch_runner_module()
        entries = module.parse_checkpoint_list(list_path)

        self.assertEqual([entry.entry_name for entry in entries], ["bzip2_source_555", "bzip2_html_7052"])
        self.assertEqual(entries[0].relative_point_dir.as_posix(), "bzip2_source/555")

        resolved = module.resolve_checkpoint_path(self.checkpoint_root, entries[0])
        self.assertEqual(resolved.name, "_555_0.026526_.zstd")

    def test_batch_runner_emits_per_slice_logs_and_aggregate_summary(self):
        self.create_checkpoint("bzip2_source", "555", "0.026526")
        self.create_checkpoint("bzip2_html", "7052", "0.302542")
        self.create_checkpoint("failbench", "2", "0.125000")
        list_path = self.write_checkpoint_list(
            [
                "bzip2_source_555 bzip2_source/555 0 0 20 20",
                "bzip2_html_7052 bzip2_html/7052 0 0 20 20",
                "failbench_2 failbench/2 0 0 20 20",
            ]
        )
        cluster_path = self.write_cluster_config()
        fake_sim = self.write_fake_simulator()

        completed = subprocess.run(
            [
                "python3",
                str(BATCH_RUNNER_PATH),
                "--simulator",
                str(fake_sim),
                "--checkpoint-list",
                str(list_path),
                "--checkpoint-root",
                str(self.checkpoint_root),
                "--output-dir",
                str(self.output_dir),
                "--jobs",
                "2",
                "--specific-benchmarks",
                "bzip2_*,failbench_*",
                "--cluster-config",
                str(cluster_path),
            ],
            capture_output=True,
            text=True,
            cwd=REPO_ROOT,
        )

        self.assertNotEqual(completed.returncode, 0, msg=completed.stdout + completed.stderr)

        for entry_name in ("bzip2_source_555", "bzip2_html_7052", "failbench_2"):
            entry_dir = self.output_dir / entry_name
            self.assertTrue((entry_dir / "command.txt").exists())
            self.assertTrue((entry_dir / "stdout.log").exists())
            self.assertTrue((entry_dir / "stderr.log").exists())
            self.assertTrue((entry_dir / "result.json").exists())

        with (self.output_dir / "batch_summary.csv").open(encoding="utf-8") as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual(len(rows), 3)

        aggregate = json.loads((self.output_dir / "aggregate.json").read_text(encoding="utf-8"))
        self.assertEqual(aggregate["total"], 3)
        self.assertEqual(aggregate["success_count"], 2)
        self.assertEqual(aggregate["failure_count"], 1)
        self.assertAlmostEqual(aggregate["aggregate_ipc_by_sum"], 1300 / 1100, places=6)
        self.assertAlmostEqual(aggregate["weighted_ipc"], 0.875, places=6)
        self.assertEqual(aggregate["failed_entries"], ["failbench_2"])
        self.assertEqual(aggregate["failure_reasons"], {"failbench_2": "illegal_instruction"})

    def test_resolve_checkpoint_path_rejects_missing_or_ambiguous_matches(self):
        module = load_batch_runner_module()

        missing_entry = type("Entry", (), {"relative_point_dir": Path("mcf/9"), "entry_name": "mcf_9"})()
        with self.assertRaisesRegex(ValueError, "未找到"):
            module.resolve_checkpoint_path(self.checkpoint_root, missing_entry)

        dup_dir = self.checkpoint_root / "mcf" / "9"
        dup_dir.mkdir(parents=True)
        (dup_dir / "_9_0.1_.zstd").write_bytes(b"a")
        (dup_dir / "_9_0.2_.zstd").write_bytes(b"b")
        with self.assertRaisesRegex(ValueError, "存在多个"):
            module.resolve_checkpoint_path(self.checkpoint_root, missing_entry)

    def test_parse_checkpoint_list_rejects_unsafe_relative_path(self):
        list_path = self.write_checkpoint_list(["evil ../../etc 0 0 20 20"])
        module = load_batch_runner_module()

        with self.assertRaisesRegex(ValueError, "非法"):
            module.parse_checkpoint_list(list_path)

    def test_batch_runner_handles_missing_simulator_without_crashing_batch(self):
        self.create_checkpoint("bzip2_source", "555", "0.026526")
        list_path = self.write_checkpoint_list(["bzip2_source_555 bzip2_source/555 0 0 20 20"])

        completed = subprocess.run(
            [
                "python3",
                str(BATCH_RUNNER_PATH),
                "--simulator",
                str(self.temp_dir / "missing_simulator"),
                "--checkpoint-list",
                str(list_path),
                "--checkpoint-root",
                str(self.checkpoint_root),
                "--output-dir",
                str(self.output_dir),
            ],
            capture_output=True,
            text=True,
            cwd=REPO_ROOT,
        )

        self.assertNotEqual(completed.returncode, 0)
        entry_dir = self.output_dir / "bzip2_source_555"
        self.assertTrue((entry_dir / "abort").exists())
        self.assertTrue((entry_dir / "error.txt").exists())
        result = json.loads((entry_dir / "result.json").read_text(encoding="utf-8"))
        self.assertFalse(result["success"])
        self.assertEqual(result["failure_reason"], "spawn_error")
        aggregate = json.loads((self.output_dir / "aggregate.json").read_text(encoding="utf-8"))
        self.assertEqual(aggregate["failure_reasons"], {"bzip2_source_555": "spawn_error"})

    def test_batch_runner_cleans_stale_result_before_rerun_and_marks_missing_result_as_failure(self):
        self.create_checkpoint("bzip2_source", "555", "0.026526")
        list_path = self.write_checkpoint_list(["bzip2_source_555 bzip2_source/555 0 0 20 20"])
        good_sim = self.write_fake_simulator()

        first_run = subprocess.run(
            [
                "python3",
                str(BATCH_RUNNER_PATH),
                "--simulator",
                str(good_sim),
                "--checkpoint-list",
                str(list_path),
                "--checkpoint-root",
                str(self.checkpoint_root),
                "--output-dir",
                str(self.output_dir),
            ],
            capture_output=True,
            text=True,
            cwd=REPO_ROOT,
        )
        self.assertEqual(first_run.returncode, 0, msg=first_run.stdout + first_run.stderr)

        bad_sim = self.write_exit_only_simulator()
        second_run = subprocess.run(
            [
                "python3",
                str(BATCH_RUNNER_PATH),
                "--simulator",
                str(bad_sim),
                "--checkpoint-list",
                str(list_path),
                "--checkpoint-root",
                str(self.checkpoint_root),
                "--output-dir",
                str(self.output_dir),
            ],
            capture_output=True,
            text=True,
            cwd=REPO_ROOT,
        )

        self.assertNotEqual(second_run.returncode, 0)
        entry_dir = self.output_dir / "bzip2_source_555"
        self.assertTrue((entry_dir / "abort").exists())
        self.assertTrue((entry_dir / "error.txt").exists())
        result = json.loads((entry_dir / "result.json").read_text(encoding="utf-8"))
        self.assertFalse(result["success"])
        self.assertEqual(result["failure_reason"], "missing_result")

    def test_batch_runner_rejects_empty_filter_result(self):
        self.create_checkpoint("bzip2_source", "555", "0.026526")
        list_path = self.write_checkpoint_list(["bzip2_source_555 bzip2_source/555 0 0 20 20"])
        good_sim = self.write_fake_simulator()

        completed = subprocess.run(
            [
                "python3",
                str(BATCH_RUNNER_PATH),
                "--simulator",
                str(good_sim),
                "--checkpoint-list",
                str(list_path),
                "--checkpoint-root",
                str(self.checkpoint_root),
                "--output-dir",
                str(self.output_dir),
                "--specific-benchmarks",
                "not_found_*",
            ],
            capture_output=True,
            text=True,
            cwd=REPO_ROOT,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertIn("未匹配到任何", completed.stderr)


if __name__ == "__main__":
    unittest.main()
