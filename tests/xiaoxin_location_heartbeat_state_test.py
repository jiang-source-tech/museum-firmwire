import os
import shutil
import subprocess
from pathlib import Path


MODEL_TEST = Path("tests/xiaoxin_location_heartbeat_state_test.cc")
INCLUDE_DIR = Path("main")


def test_location_heartbeat_state_executable(tmp_path: Path):
    compiler = shutil.which("g++")
    assert compiler is not None, "g++ is required for the native heartbeat state test"
    compiler_path = Path(compiler)
    env = os.environ.copy()
    env["PATH"] = str(compiler_path.parent) + os.pathsep + env.get("PATH", "")

    executable = tmp_path / "xiaoxin_location_heartbeat_state_test.exe"
    compile_result = subprocess.run(
        [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{INCLUDE_DIR.resolve().as_posix()}",
            MODEL_TEST.resolve().as_posix(),
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
    assert compile_result.returncode == 0, compile_result.stdout + compile_result.stderr

    run_result = subprocess.run(
        [str(executable)],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
        check=False,
    )
    assert run_result.returncode == 0, run_result.stdout + run_result.stderr
    assert "xiaoxin location heartbeat state tests passed" in run_result.stdout
