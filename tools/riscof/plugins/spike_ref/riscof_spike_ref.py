import logging
import os
import shlex
import shutil
import subprocess

import riscof.utils as utils
from riscof.pluginTemplate import pluginTemplate

logger = logging.getLogger()


class spike_ref(pluginTemplate):
    __model__ = "spike_ref"
    __version__ = "0.1.0"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        config = kwargs.get("config")
        config_dir = kwargs.get("config_dir", os.getcwd())
        if config is None:
            logger.error("Config node for spike_ref missing.")
            raise SystemExit(1)

        self.pluginpath = os.path.abspath(config["pluginpath"])
        self.isa_spec = os.path.abspath(config["ispec"])
        self.platform_spec = os.path.abspath(config["pspec"])
        self.target_run = str(config.get("target_run", "1")) != "0"
        self.num_jobs = int(config.get("jobs", 1))

        path_prefix = config.get("PATH", "")
        self.spike = os.path.join(path_prefix, "spike") if path_prefix else "spike"
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
        self.archtest_env = ""

    def initialise(self, suite, work_dir, archtest_env):
        self.archtest_env = archtest_env

    def build(self, isa_yaml, platform_yaml):
        ispec = utils.load_yaml(isa_yaml)["hart0"]
        self.xlen = "64" if 64 in ispec["supported_xlen"] else "32"
        if self.xlen == "64":
            self.mabi = "lp64"
        else:
            self.mabi = "ilp32e" if "E" in ispec["ISA"] else "ilp32"

        gcc = f"riscv{self.xlen}-unknown-elf-gcc"
        if shutil.which(gcc) is None:
            logger.error("%s: executable not found.", gcc)
            raise SystemExit(1)
        if shutil.which(self.spike) is None:
            logger.error("%s: executable not found.", self.spike)
            raise SystemExit(1)
        if not os.path.exists(os.path.join(self.env_dir, "link.ld")):
            logger.error("link.ld not found in env_dir: %s", self.env_dir)
            raise SystemExit(1)

    def runTests(self, testList):
        gcc = f"riscv{self.xlen}-unknown-elf-gcc"

        for testname in testList:
            testentry = testList[testname]
            test = testentry["test_path"]
            test_dir = testentry["work_dir"]
            os.makedirs(test_dir, exist_ok=True)

            elf_name = "ref.elf"
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

            logger.debug("Compiling ref: %s", " ".join(shlex.quote(p) for p in compile_cmd))
            subprocess.run(compile_cmd, cwd=test_dir, check=True)

            if not self.target_run:
                continue

            run_cmd = [
                self.spike,
                "--misaligned",
                f"--isa={testentry['isa'].lower()}",
                f"+signature={sig_path}",
                f"+signature-granularity={self.signature_granularity}",
                elf_name,
            ]
            logger.debug("Running ref: %s", " ".join(shlex.quote(p) for p in run_cmd))
            with open(log_path, "w", encoding="utf-8") as logf:
                subprocess.run(run_cmd, cwd=test_dir, check=True, stdout=logf, stderr=logf)
