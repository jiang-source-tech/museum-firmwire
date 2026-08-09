import importlib.util
import json
from pathlib import Path
import sys


REPO_ROOT = Path(__file__).resolve().parents[1]
PREFLIGHT_PATH = REPO_ROOT / "tools" / "verify_xiaoxin_ota_release.py"

spec = importlib.util.spec_from_file_location("xiaoxin_ota_release_preflight", PREFLIGHT_PATH)
assert spec and spec.loader
preflight = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = preflight
spec.loader.exec_module(preflight)


def write_fixture(repo_root: Path) -> Path:
    (repo_root / "partitions").mkdir(parents=True)
    (repo_root / "build" / "config").mkdir(parents=True)
    (repo_root / "build" / "bootloader").mkdir(parents=True)
    (repo_root / "build" / "partition_table").mkdir(parents=True)

    (repo_root / "partitions" / "xiaoxin-ota-16m.csv").write_text(
        "\n".join(
            (
                "nvs,data,nvs,0x9000,0x6000,",
                "phy_init,data,phy,0xf000,0x1000,",
                "otadata,data,ota,0x10000,0x2000,",
                "ota_0,app,ota_0,0x20000,0x600000,",
                "ota_1,app,ota_1,0x620000,0x600000,",
                "assets,data,spiffs,0xc20000,0x3e0000,",
            )
        ),
        encoding="utf-8",
    )
    sdkconfig = "\n".join(
        (
            "CONFIG_IDF_TARGET=\"esp32s3\"",
            "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
            "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y",
            "CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_46=y",
            "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions/xiaoxin-ota-16m.csv\"",
            "CONFIG_PARTITION_TABLE_FILENAME=\"partitions/xiaoxin-ota-16m.csv\"",
        )
    )
    (repo_root / "sdkconfig").write_text(sdkconfig, encoding="utf-8")
    for defaults_name in ("sdkconfig.defaults", "sdkconfig.defaults.xiaozhi"):
        (repo_root / defaults_name).write_text(
            "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions/xiaoxin-ota-16m.csv\"\n",
            encoding="utf-8",
        )
    (repo_root / "build" / "config" / "sdkconfig.cmake").write_text(
        "\n".join(
            (
                'set(CONFIG_IDF_TARGET "esp32s3")',
                'set(CONFIG_ESPTOOLPY_FLASHSIZE_16MB "y")',
                'set(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE "y")',
                'set(CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_46 "y")',
                'set(CONFIG_PARTITION_TABLE_FILENAME "partitions/xiaoxin-ota-16m.csv")',
            )
        ),
        encoding="utf-8",
    )

    build_dir = repo_root / "build"
    (build_dir / "bootloader" / "bootloader.bin").write_bytes(b"\xe9" + b"b" * 63)
    (build_dir / "partition_table" / "partition-table.bin").write_bytes(b"p" * 3072)
    (build_dir / "ota_data_initial.bin").write_bytes(b"\xff" * 0x2000)
    (build_dir / "ai_pet.bin").write_bytes(b"\xe9" + b"a" * 1023)
    (build_dir / "generated_assets.bin").write_bytes(b"asset")

    flash_files = {f"0x{offset:x}": path for offset, path in preflight.EXPECTED_FLASH_FILES.items()}
    metadata = {
        name: {"offset": f"0x{offset:x}", "file": path, "encrypted": "false"}
        for name, (offset, path) in preflight.EXPECTED_FLASH_METADATA.items()
    }
    (build_dir / "flasher_args.json").write_text(
        json.dumps(
            {
                "flash_settings": {"flash_size": "16MB"},
                "extra_esptool_args": {"chip": "esp32s3"},
                "flash_files": flash_files,
                **metadata,
            }
        ),
        encoding="utf-8",
    )
    return build_dir


def test_release_preflight_accepts_complete_bootstrap_artifacts(tmp_path: Path) -> None:
    build_dir = write_fixture(tmp_path)

    assert preflight.validate_release(tmp_path, build_dir) == []


def test_release_preflight_rejects_missing_esp_idf_generated_otadata(tmp_path: Path) -> None:
    build_dir = write_fixture(tmp_path)
    (build_dir / "ota_data_initial.bin").unlink()

    errors = preflight.validate_release(tmp_path, build_dir)

    assert any("ota_data_initial.bin" in error and "blank_ota_data" in error for error in errors)


def test_release_preflight_rejects_nonblank_otadata(tmp_path: Path) -> None:
    build_dir = write_fixture(tmp_path)
    (build_dir / "ota_data_initial.bin").write_bytes(b"\x00" * 0x2000)

    errors = preflight.validate_release(tmp_path, build_dir)

    assert any("entirely erased" in error for error in errors)


def test_release_packager_generates_otadata_before_merging_flash_args() -> None:
    source = (REPO_ROOT / "scripts" / "release.py").read_text(encoding="utf-8")

    assert "def ensure_ota_data_artifact" in source
    assert 'idf.py blank_ota_data' in source
    assert source.index("ensure_ota_data_artifact()") < source.index('os.system("idf.py merge-bin")')
