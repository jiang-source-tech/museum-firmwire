import os
import shutil
import subprocess
from pathlib import Path


def test_ota_release_audit_model_is_executable(tmp_path: Path) -> None:
    compiler = shutil.which("g++")
    assert compiler is not None, "g++ is required for the OTA release audit model test"

    executable = tmp_path / "ota_release_audit_model_test.exe"
    command = [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-I{Path('main').resolve().as_posix()}",
        Path("tests/ota_release_audit_model_test.cc").resolve().as_posix(),
        "-o",
        executable.resolve().as_posix(),
    ]
    env = os.environ.copy()
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
    compile_result = subprocess.run(command, capture_output=True, text=True, env=env, check=False)
    assert compile_result.returncode == 0, compile_result.stdout + compile_result.stderr

    run_result = subprocess.run([str(executable)], capture_output=True, text=True, env=env, check=False)
    assert run_result.returncode == 0, run_result.stdout + run_result.stderr
    assert "ota release audit model tests passed" in run_result.stdout
