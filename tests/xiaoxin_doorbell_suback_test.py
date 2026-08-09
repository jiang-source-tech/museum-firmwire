import os
import shutil
import subprocess
from pathlib import Path


MODEL_TEST = Path("tests/xiaoxin_doorbell_suback_test.cc")
INCLUDE_DIR = Path("main")
DOORBELL = Path("main/doorbell_mqtt.cc")
DOORBELL_HEADER = Path("main/doorbell_mqtt.h")


def read_source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"function body not found: {signature}")


def test_doorbell_suback_model_executable(tmp_path: Path):
    compiler = shutil.which("g++")
    assert compiler is not None, "g++ is required for the native SUBACK test"
    compiler_path = Path(compiler)
    env = os.environ.copy()
    env["PATH"] = str(compiler_path.parent) + os.pathsep + env.get("PATH", "")

    executable = tmp_path / "xiaoxin_doorbell_suback_test.exe"
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
    assert "xiaoxin doorbell suback tests passed" in run_result.stdout


def test_suback_event_validates_result_before_confirming_mid():
    source = read_source(DOORBELL)
    header = read_source(DOORBELL_HEADER)
    handler = function_body(source, "void DoorbellMqtt::MqttEventHandler")
    subscribed = function_body(source, "void DoorbellMqtt::OnSubscribed")

    assert '#include "doorbell_mqtt_suback.h"' in header
    assert "self->OnSubscribed(event);" in handler
    assert "event->error_handle" in subscribed
    assert "MQTT_ERROR_TYPE_SUBSCRIBE_FAILED" in subscribed
    assert "event->data" in subscribed
    assert "event->data_len" in subscribed
    assert "EvaluateDoorbellMqttSuback" in subscribed
    assert subscribed.index("EvaluateDoorbellMqttSuback") < subscribed.index(
        "notification_subscribe_mid_ = -1"
    )
    assert subscribed.index("EvaluateDoorbellMqttSuback") < subscribed.index(
        "overview_subscribe_mid_ = -1"
    )
    assert "subscription confirmed" in subscribed
    assert "subscription rejected or malformed" in subscribed


def test_suback_failure_requests_one_safe_reconnect_cycle():
    source = read_source(DOORBELL)
    header = read_source(DOORBELL_HEADER)
    subscribed = function_body(source, "void DoorbellMqtt::OnSubscribed")
    reconnect = function_body(
        source, "void DoorbellMqtt::RequestReconnectAfterSubscriptionFailure"
    )
    handler = function_body(source, "void DoorbellMqtt::MqttEventHandler")

    assert "subscription_reconnect_pending_" in header
    assert "RequestReconnectAfterSubscriptionFailure();" in subscribed
    assert "subscription_reconnect_pending_.exchange(true)" in reconnect
    assert "esp_mqtt_client_disconnect(client_)" in reconnect
    assert "subscription_reconnect_pending_.exchange(false)" in handler
    assert "esp_mqtt_client_reconnect(self->client_)" in handler
    assert "ESP_OK" in handler
    assert "stopping_" in header
    assert "stopping_.store(true)" in source
    assert "!self->stopping_.load()" in handler
