from pathlib import Path


SDKCONFIG = Path("sdkconfig")
SDKCONFIG_DEFAULTS = Path("sdkconfig.defaults")
SDKCONFIG_DEFAULTS_ESP32S3 = Path("sdkconfig.defaults.esp32s3")


def read_config(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_museum_firmware_uses_custom_guide_wake_phrase():
    configs = [
        read_config(SDKCONFIG_DEFAULTS),
        read_config(SDKCONFIG_DEFAULTS_ESP32S3),
        read_config(SDKCONFIG),
    ]

    for config in configs:
        assert "CONFIG_SR_WN_WN9_NIHAOXIAOXIN_TTS=y" not in config

    for config in (configs[0], configs[2]):
        assert "CONFIG_USE_AFE_WAKE_WORD=y" not in config
        assert "CONFIG_USE_CUSTOM_WAKE_WORD=y" in config
        assert 'CONFIG_CUSTOM_WAKE_WORD="ni hao jiang jie yuan"' in config
        assert 'CONFIG_CUSTOM_WAKE_WORD_DISPLAY="你好讲解员"' in config
        assert "CONFIG_SR_MN_CN_MULTINET7_QUANT=y" in config
