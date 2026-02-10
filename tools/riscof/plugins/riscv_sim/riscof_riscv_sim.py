import logging
import os
import shlex
import shutil
import subprocess

import riscof.utils as utils
from riscof.pluginTemplate import pluginTemplate

logger = logging.getLogger()


class riscv_sim(pluginTemplate):
    __model__ = "riscv_sim"
    __version__ = "0.1.0"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        config = kwargs.get("config")
        config_dir = kwargs.get("config_dir", os.getcwd())
        if config is None:
            logger.error("Config node for riscv_sim missing.")
            raise SystemExit(1)

        self.pluginpath = os.path.abspath(config["pluginpath"])
        self.isa_spec = os.path.abspath(config["ispec"])
        self.platform_spec = os.path.abspath(config["pspec"])
        self.target_run = str(config.get("target_run", "1")) != "0"
        self.num_jobs = int(config.get("jobs", 1))

        sim_path = config.get("simulator", "./build/risc-v-sim")
        if os.path.isabs(sim_path):
            self.simulator = sim_path
        else:
            self.simulator = os.path.abspath(os.path.join(config_dir, sim_path))

        self.cpu_mode = str(config.get("cpu_mode", "in_order")).lower()
        self.mem_size = int(config.get("mem_size", 2164260864))
        self.signature_granularity = int(config.get("signature_granularity", 8))

        env_dir = config.get("env_dir")
        if env_dir:
            if os.path.isabs(env_dir):
                self.env_dir = env_dir
            else:
                self.env_dir = os.path.abspath(os.path.join(config_dir, env_dir))
        else:
            self.env_dir = os.path.join(self.pluginpath, "env")

        self.xlen = "64"
        self.mabi = "lp64"
        self.work_dir = ""
        self.suite_dir = ""
        self.archtest_env = ""

    def initialise(self, suite, work_dir, archtest_env):
        self.work_dir = work_dir
        self.suite_dir = suite
        self.archtest_env = archtest_env

    def build(self, isa_yaml, platform_yaml):
        ispec = utils.load_yaml(isa_yaml)["hart0"]
        self.xlen = "64" if 64 in ispec["supported_xlen"] else "32"
        if self.xlen == "64":
            self.mabi = "lp64"
        else:
            self.mabi = "ilp32e" if "E" in ispec["ISA"] else "ilp32"

        gcc = f"riscv{self.xlen}-unknown-elf-gcc"
        nm = f"riscv{self.xlen}-unknown-elf-nm"
        if shutil.which(gcc) is None:
            logger.error("%s: executable not found.", gcc)
            raise SystemExit(1)
        if shutil.which(nm) is None:
            logger.error("%s: executable not found.", nm)
            raise SystemExit(1)
        if not os.path.exists(self.simulator):
            logger.error("simulator not found: %s", self.simulator)
            raise SystemExit(1)
        if not os.path.exists(os.path.join(self.env_dir, "link.ld")):
            logger.error("link.ld not found in env_dir: %s", self.env_dir)
            raise SystemExit(1)

    def _read_symbols(self, elf_path):
        nm_bin = f"riscv{self.xlen}-unknown-elf-nm"
        result = subprocess.run(
            [nm_bin, elf_path],
            check=True,
            capture_output=True,
            text=True,
        )

        begin = None
        end = None
        tohost = None
        fromhost = None
        for line in result.stdout.splitlines():
            parts = line.strip().split()
            if len(parts) < 3:
                continue
            symbol = parts[-1]
            if symbol == "begin_signature":
                begin = int(parts[0], 16)
            elif symbol == "end_signature":
                end = int(parts[0], 16)
            elif symbol == "tohost":
                tohost = int(parts[0], 16)
            elif symbol == "fromhost":
                fromhost = int(parts[0], 16)

        if begin is None or end is None:
            raise RuntimeError(
                f"signature symbols not found in {elf_path}: begin={begin}, end={end}"
            )
        if begin >= end:
            raise RuntimeError(
                f"invalid signature range in {elf_path}: begin=0x{begin:x}, end=0x{end:x}"
            )
        if tohost is None:
            raise RuntimeError(f"tohost symbol not found in {elf_path}")
        if fromhost is None:
            fromhost = tohost + 0x40
        return begin, end, tohost, fromhost

    def runTests(self, testList):
        gcc = f"riscv{self.xlen}-unknown-elf-gcc"
        cpu_flag = "--in-order"
        if self.cpu_mode in {"ooo", "out_of_order", "out-of-order"}:
            cpu_flag = "--ooo"

        for testname in testList:
            testentry = testList[testname]
            test = testentry["test_path"]
            test_dir = testentry["work_dir"]
            os.makedirs(test_dir, exist_ok=True)

            elf_name = "dut.elf"
            elf_path = os.path.join(test_dir, elf_name)
            log_path = os.path.join(test_dir, self.name[:-1] + ".log")
            sig_path = os.path.join(test_dir, self.name[:-1] + ".signature")

            compile_cmd = [
                gcc,
                f"-march={testentry['isa'].lower()}",
                f"-mabi={self.mabi}",
                "-static",
                "-mcmodel=medany",
                "-fvisibility=hidden",
                "-nostdlib",
                "-nostartfiles",
                "-g",
                "-T",
                os.path.join(self.env_dir, "link.ld"),
                "-I",
                self.env_dir,
                "-I",
                self.archtest_env,
                test,
                "-o",
                elf_name,
            ]
            for macro in testentry["macros"]:
                compile_cmd.append("-D" + macro)

            logger.debug("Compiling: %s", " ".join(shlex.quote(p) for p in compile_cmd))
            subprocess.run(compile_cmd, cwd=test_dir, check=True)

            if not self.target_run:
                continue

            begin, end, tohost, fromhost = self._read_symbols(elf_path)
            sim_cmd = [
                self.simulator,
                "-e",
                "-m",
                str(self.mem_size),
                cpu_flag,
                elf_name,
                f"--isa={testentry['isa']}",
                f"--tohost-addr=0x{tohost:x}",
                f"--fromhost-addr=0x{fromhost:x}",
                f"--signature-file={sig_path}",
                f"--signature-start=0x{begin:x}",
                f"--signature-end=0x{end:x}",
                f"--signature-granularity={self.signature_granularity}",
            ]

            logger.debug("Running DUT: %s", " ".join(shlex.quote(p) for p in sim_cmd))
            with open(log_path, "w", encoding="utf-8") as logf:
                subprocess.run(sim_cmd, cwd=test_dir, check=True, stdout=logf, stderr=logf)
