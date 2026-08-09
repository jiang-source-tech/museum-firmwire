import os
import shutil
import subprocess
from pathlib import Path


def test_overview_model_executable(tmp_path: Path):
    compiler = shutil.which("gcc")
    assert compiler is not None
    env = os.environ.copy()
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
    executable = tmp_path / "overview_model_test.exe"
    source_dir = Path(
        "main/boards/waveshare/esp32-s3-touch-lcd-1.46"
    ).resolve()
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-I{source_dir.as_posix()}",
        Path("tests/xiaoxin_overview_model_test.c").resolve().as_posix(),
        (source_dir / "xiaoxin_overview_model.c").as_posix(),
        "-o",
        executable.resolve().as_posix(),
    ]
    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        env=env,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    run = subprocess.run(
        [str(executable)],
        capture_output=True,
        text=True,
        env=env,
        check=False,
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert "xiaoxin_overview_model tests passed" in run.stdout
