import builtins
import importlib.util
from pathlib import Path


def load_build_default_assets_module():
    script_path = Path("scripts/build_default_assets.py")
    spec = importlib.util.spec_from_file_location("build_default_assets", script_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_custom_wake_word_display_is_read_as_utf8_on_windows_locale(tmp_path, monkeypatch):
    module = load_build_default_assets_module()
    display_name = "\u5c0f\u82af"
    sdkconfig = tmp_path / "sdkconfig"
    sdkconfig.write_text(
        "\n".join(
            [
                "CONFIG_USE_CUSTOM_WAKE_WORD=y",
                'CONFIG_CUSTOM_WAKE_WORD="xiao xin"',
                f'CONFIG_CUSTOM_WAKE_WORD_DISPLAY="{display_name}"',
                "CONFIG_CUSTOM_WAKE_WORD_THRESHOLD=20",
            ]
        ),
        encoding="utf-8",
    )

    real_open = builtins.open

    def locale_sensitive_open(path, mode="r", *args, **kwargs):
        if "b" not in mode and "encoding" not in kwargs:
            kwargs["encoding"] = "gbk"
        return real_open(path, mode, *args, **kwargs)

    monkeypatch.setattr(module.io, "open", locale_sensitive_open)

    config = module.read_custom_wake_word_from_sdkconfig(str(sdkconfig))

    assert config["display"].encode("unicode_escape") == display_name.encode("unicode_escape")
