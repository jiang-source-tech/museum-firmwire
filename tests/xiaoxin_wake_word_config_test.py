from pathlib import Path


SDKCONFIG = Path("sdkconfig")
SDKCONFIG_DEFAULTS = Path("sdkconfig.defaults")
SDKCONFIG_DEFAULTS_ESP32S3 = Path("sdkconfig.defaults.esp32s3")


def read_config(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_xiaoxin_default_firmware_uses_nihao_xiaoxin_wakenet_model():
    configs = [
        read_config(SDKCONFIG_DEFAULTS),
        read_config(SDKCONFIG_DEFAULTS_ESP32S3),
        read_config(SDKCONFIG),
    ]

    for config in configs:
        assert 'CONFIG_CUSTOM_WAKE_WORD="xiao xin"' not in config
        assert "CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y" not in config
        assert "CONFIG_SR_WN_WN9_NIHAOXIAOXIN_TTS=y" in config

    for config in (configs[0], configs[2]):
        assert "CONFIG_USE_AFE_WAKE_WORD=y" in config
        assert "# CONFIG_USE_CUSTOM_WAKE_WORD is not set" in config
