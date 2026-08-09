import os
import shutil
import subprocess
from pathlib import Path


def test_doorbell_config_contract_executable(tmp_path: Path):
    compiler = shutil.which("g++")
    assert compiler is not None
    env = os.environ.copy()
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
    executable = tmp_path / "doorbell_config_contract_test.exe"
    command = [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-I{Path('main').resolve().as_posix()}",
        Path("tests/xiaoxin_doorbell_config_contract_test.cc").resolve().as_posix(),
        Path("main/doorbell_config_contract.cc").resolve().as_posix(),
        "-o",
        executable.resolve().as_posix(),
    ]
    result = subprocess.run(command, capture_output=True, text=True, env=env, check=False)
    assert result.returncode == 0, result.stdout + result.stderr
    run = subprocess.run([str(executable)], capture_output=True, text=True, env=env, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "xiaoxin doorbell config contract tests passed" in run.stdout
