from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
BOOTSTRAP_TABLE = "partitions/xiaoxin-ota-16m.csv"
FLASH_SIZE = 16 * 1024 * 1024


def read_config_value(path: Path, key: str) -> str:
    prefix = f"{key}="
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith(prefix):
            return line.removeprefix(prefix).strip().strip('"')
    raise AssertionError(f"{key} not found in {path}")


def parse_partition_table(path: Path) -> dict[str, tuple[str, str, int, int]]:
    partitions: dict[str, tuple[str, str, int, int]] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        name, part_type, subtype, offset, size, *_ = [field.strip() for field in line.split(",")]
        partitions[name] = (part_type, subtype, int(offset, 0), int(size, 0))
    return partitions


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"function body not found: {signature}")


def test_bootstrap_target_configs_select_the_dual_ota_partition_table() -> None:
    for relative_path in ("sdkconfig", "sdkconfig.defaults", "sdkconfig.defaults.xiaozhi"):
        assert (
            read_config_value(REPO_ROOT / relative_path, "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME")
            == BOOTSTRAP_TABLE
        )

    assert (
        read_config_value(REPO_ROOT / "sdkconfig", "CONFIG_PARTITION_TABLE_FILENAME")
        == BOOTSTRAP_TABLE
    )


def test_bootstrap_partition_table_has_two_six_megabyte_slots_and_assets() -> None:
    partitions = parse_partition_table(REPO_ROOT / BOOTSTRAP_TABLE)

    assert partitions["nvs"] == ("data", "nvs", 0x9000, 0x6000)
    assert partitions["otadata"] == ("data", "ota", 0x10000, 0x2000)
    assert partitions["ota_0"] == ("app", "ota_0", 0x20000, 0x600000)
    assert partitions["ota_1"] == ("app", "ota_1", 0x620000, 0x600000)
    assert partitions["assets"] == ("data", "spiffs", 0xC20000, 0x3E0000)

    assert "factory" not in partitions
    assert max(offset + size for _, _, offset, size in partitions.values()) == FLASH_SIZE
    for name in ("ota_0", "ota_1"):
        _, _, offset, size = partitions[name]
        assert offset % 0x10000 == 0
        assert size % 0x10000 == 0


def test_factory_reset_path_keeps_the_verified_ota_selection() -> None:
    source = (REPO_ROOT / "main/boards/common/system_reset.cc").read_text(encoding="utf-8")
    body = function_body(source, "void SystemReset::ResetPersistentDataAndRestart()")

    assert "ResetNvsFlash();" in body
    assert "RestartInSeconds(3);" in body
    assert "otadata" not in body
    assert "esp_partition" not in body
