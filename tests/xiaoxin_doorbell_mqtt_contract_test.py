import os
import shutil
import subprocess
from pathlib import Path


def test_doorbell_mqtt_contract_executable(tmp_path: Path):
    compiler = shutil.which("g++")
    assert compiler is not None, "g++ is required for the doorbell mqtt contract test"
    compiler_path = Path(compiler)
    env = os.environ.copy()
    env["PATH"] = str(compiler_path.parent) + os.pathsep + env.get("PATH", "")
    executable = tmp_path / "xiaoxin_doorbell_mqtt_contract_test.exe"
    result = subprocess.run(
        [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{Path('main').resolve().as_posix()}",
            Path("tests/xiaoxin_doorbell_mqtt_contract_test.cc").resolve().as_posix(),
            Path("main/doorbell_mqtt_contract.cc").resolve().as_posix(),
            "-o",
            executable.resolve().as_posix(),
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    run = subprocess.run(
        [str(executable)], capture_output=True, text=True, env=env, check=False
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert "xiaoxin doorbell mqtt contract tests passed" in run.stdout
