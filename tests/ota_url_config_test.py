from pathlib import Path


EXPECTED_OTA_URL = ""
LEGACY_OTA_URLS = (
    "http://124.221.253.206:8003/xiaoxin/ota/",
    "http://124.222.121.103:8003/xiaozhi/ota/",
    "http://121.43.33.0:8003/xiaoxin/ota/",
    "https://api.tenclass.net/xiaozhi/ota/",
)


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
                return source[brace + 1:index]
    raise AssertionError(f"function body not found: {signature}")


def read_config_value(path: Path, key: str) -> str:
    prefix = f"{key}="
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith(prefix):
            return line.removeprefix(prefix).strip().strip('"')
    raise AssertionError(f"{key} not found in {path}")


def test_tracked_ota_url_requires_deployment_configuration() -> None:
    repo_root = Path(__file__).resolve().parents[1]

    assert read_config_value(repo_root / "sdkconfig.defaults", "CONFIG_OTA_URL") == EXPECTED_OTA_URL
    assert read_config_value(repo_root / "sdkconfig", "CONFIG_OTA_URL") == EXPECTED_OTA_URL
    assert 'default ""' in (repo_root / "main" / "Kconfig.projbuild").read_text(
        encoding="utf-8"
    )
    assert (
        read_config_value(
            repo_root / "main" / "boards" / "waveshare" / "esp32-s3-touch-lcd-4.3c" / "sdkconfig.4_3c",
            "CONFIG_OTA_URL",
        )
        == EXPECTED_OTA_URL
    )


def test_tracked_ota_configs_do_not_allow_insecure_http() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    for path in (
        repo_root / "sdkconfig",
        repo_root / "sdkconfig.defaults",
        repo_root / "sdkconfig.defaults.xiaozhi",
    ):
        source = path.read_text(encoding="utf-8")
        assert "CONFIG_OTA_ALLOW_INSECURE_HTTP=y" not in source

    kconfig = (repo_root / "main" / "Kconfig.projbuild").read_text(encoding="utf-8")
    block = kconfig[kconfig.index("config OTA_ALLOW_INSECURE_HTTP") :]
    assert "default n" in block


def test_tracked_ota_defaults_do_not_reference_xiaozhi_ota() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    files = [
        repo_root / "sdkconfig.defaults",
        repo_root / "sdkconfig",
        repo_root / "main" / "Kconfig.projbuild",
        repo_root / "main" / "boards" / "waveshare" / "esp32-s3-touch-lcd-4.3c" / "sdkconfig.4_3c",
    ]

    for path in files:
        source = path.read_text(encoding="utf-8")
        assert "xiaozhi/ota" not in source, path


def test_ota_url_migrates_persisted_legacy_override() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    source = (repo_root / "main" / "ota.cc").read_text(encoding="utf-8")
    body = function_body(source, "std::string Ota::GetCheckVersionUrl()")

    for legacy_url in LEGACY_OTA_URLS:
        assert legacy_url in source
    assert 'Settings settings("wifi", true)' in body
    assert 'settings.GetString("ota_url")' in body
    assert "IsLegacyOtaUrl(url)" in body
    assert 'settings.EraseKey("ota_url")' in body
    assert "url.clear()" in body
    assert "url = CONFIG_OTA_URL" in body


def test_ota_server_time_keeps_epoch_in_utc() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    source = (repo_root / "main" / "ota.cc").read_text(encoding="utf-8")
    body = function_body(source, "esp_err_t Ota::CheckVersion()")
    server_time_block = body[body.index('cJSON *server_time = cJSON_GetObjectItem(root, "server_time");') :]
    server_time_block = server_time_block[: server_time_block.index("has_new_version_ = false;")]

    assert 'cJSON *timezone_offset = cJSON_GetObjectItem(server_time, "timezone_offset");' not in server_time_block
    assert "timezone_offset->valueint" not in server_time_block
    assert "ts +=" not in server_time_block
    assert "settimeofday(&tv, NULL);" in server_time_block


def test_settings_erase_operations_are_committed() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    source = (repo_root / "main" / "settings.cc").read_text(encoding="utf-8")

    assert "dirty_ = true;" in function_body(source, "void Settings::EraseKey")
    assert "dirty_ = true;" in function_body(source, "void Settings::EraseAll")


def test_application_reports_ota_states_to_display_notifications() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    source = (repo_root / "main" / "application.cc").read_text(encoding="utf-8")

    assert 'display->UpsertNotification("ota_update", "OTA 更新", "发现新版本", "系统", 4, 0);' in source
    assert 'display->UpsertNotification("ota_update", "OTA 更新", "正在下载并安装更新", "系统", 4, 0);' in source
    assert 'display->UpsertNotification("ota_update", "OTA 更新", "升级失败，请稍后重试", "系统", 4, 0);' in source
    assert 'display->RemoveNotification("ota_update");' in source
