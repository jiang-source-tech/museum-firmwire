#!/usr/bin/env python3
"""Fail closed when a Xiaoxin OTA bootstrap release is incomplete or mismatched.

This checks the artifacts which ESP-IDF will flash during the one-time USB
bootstrap.  It deliberately does not talk to a serial port and is safe to use
in CI.  The actual device flash and the following Wi-Fi OTA cycle remain
hardware acceptance steps.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Iterable


FLASH_SIZE = 16 * 1024 * 1024
PARTITION_TABLE = "partitions/xiaoxin-ota-16m.csv"
BOARD_KCONFIG = "CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_46"


@dataclass(frozen=True)
class Partition:
    name: str
    part_type: str
    subtype: str
    offset: int
    size: int

    @property
    def end(self) -> int:
        return self.offset + self.size


EXPECTED_PARTITIONS = {
    "nvs": ("data", "nvs", 0x9000, 0x6000),
    "phy_init": ("data", "phy", 0xF000, 0x1000),
    "otadata": ("data", "ota", 0x10000, 0x2000),
    "ota_0": ("app", "ota_0", 0x20000, 0x600000),
    "ota_1": ("app", "ota_1", 0x620000, 0x600000),
    "assets": ("data", "spiffs", 0xC20000, 0x3E0000),
}

EXPECTED_FLASH_FILES = {
    0x0000: "bootloader/bootloader.bin",
    0x8000: "partition_table/partition-table.bin",
    0x10000: "ota_data_initial.bin",
    0x20000: "ai_pet.bin",
    0xC20000: "generated_assets.bin",
}

EXPECTED_FLASH_METADATA = {
    "bootloader": (0x0000, "bootloader/bootloader.bin"),
    "partition-table": (0x8000, "partition_table/partition-table.bin"),
    "otadata": (0x10000, "ota_data_initial.bin"),
    "app": (0x20000, "ai_pet.bin"),
    "assets": (0xC20000, "generated_assets.bin"),
}


def normalize_artifact_path(value: object) -> str | None:
    """Return a portable relative artifact path, rejecting unsafe values."""
    if not isinstance(value, str) or not value:
        return None

    windows_path = PureWindowsPath(value)
    posix_path = PurePosixPath(value.replace("\\", "/"))
    if (
        windows_path.is_absolute()
        or windows_path.drive
        or posix_path.is_absolute()
        or ".." in windows_path.parts
        or ".." in posix_path.parts
    ):
        return None
    return "/".join(posix_path.parts)


def parse_kconfig(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key.startswith("CONFIG_"):
            values[key] = value.strip().strip('"')
    return values


def parse_generated_kconfig(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    pattern = re.compile(r'^set\((CONFIG_[A-Z0-9_]+) "(.*)"\)$')
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        match = pattern.match(raw_line.strip())
        if match:
            values[match.group(1)] = match.group(2)
    return values


def parse_partition_table(path: Path) -> dict[str, Partition]:
    partitions: dict[str, Partition] = {}
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [field.strip() for field in line.split(",")]
        if len(fields) < 5:
            raise ValueError(f"{path}:{line_number}: expected at least five CSV fields")
        name, part_type, subtype, offset, size = fields[:5]
        if not offset or not size:
            raise ValueError(f"{path}:{line_number}: offset and size must be explicit")
        if name in partitions:
            raise ValueError(f"{path}:{line_number}: duplicate partition {name!r}")
        partitions[name] = Partition(name, part_type, subtype, int(offset, 0), int(size, 0))
    return partitions


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ranges_overlap(first_start: int, first_end: int, second_start: int, second_end: int) -> bool:
    return first_start < second_end and second_start < first_end


def validate_partition_layout(repo_root: Path, errors: list[str]) -> dict[str, Partition]:
    table_path = repo_root / PARTITION_TABLE
    if not table_path.is_file():
        errors.append(f"missing partition table: {table_path}")
        return {}

    try:
        partitions = parse_partition_table(table_path)
    except (OSError, ValueError) as error:
        errors.append(f"cannot parse {table_path}: {error}")
        return {}

    for name, expected in EXPECTED_PARTITIONS.items():
        actual = partitions.get(name)
        if actual is None:
            errors.append(f"{PARTITION_TABLE} is missing required partition {name}")
            continue
        actual_tuple = (actual.part_type, actual.subtype, actual.offset, actual.size)
        if actual_tuple != expected:
            errors.append(
                f"{PARTITION_TABLE}:{name} is {actual_tuple!r}; expected {expected!r}"
            )

    if "factory" in partitions:
        errors.append(f"{PARTITION_TABLE} must not contain a factory application partition")
    if partitions and max(partition.end for partition in partitions.values()) != FLASH_SIZE:
        errors.append(f"{PARTITION_TABLE} must end exactly at 0x{FLASH_SIZE:x}")
    return partitions


def validate_config(repo_root: Path, build_dir: Path, errors: list[str]) -> None:
    source_config_expectations = {
        "CONFIG_IDF_TARGET": "esp32s3",
        "CONFIG_ESPTOOLPY_FLASHSIZE_16MB": "y",
        "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE": "y",
        BOARD_KCONFIG: "y",
        "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME": PARTITION_TABLE,
        "CONFIG_PARTITION_TABLE_FILENAME": PARTITION_TABLE,
    }
    sdkconfig_path = repo_root / "sdkconfig"
    if not sdkconfig_path.is_file():
        errors.append(f"missing configured sdkconfig: {sdkconfig_path}")
    else:
        configured = parse_kconfig(sdkconfig_path)
        for key, expected in source_config_expectations.items():
            if configured.get(key) != expected:
                errors.append(f"sdkconfig:{key} must be {expected!r}, got {configured.get(key)!r}")

    for defaults_name in ("sdkconfig.defaults", "sdkconfig.defaults.xiaozhi"):
        defaults_path = repo_root / defaults_name
        if not defaults_path.is_file():
            errors.append(f"missing {defaults_name}")
            continue
        defaults = parse_kconfig(defaults_path)
        if defaults.get("CONFIG_PARTITION_TABLE_CUSTOM_FILENAME") != PARTITION_TABLE:
            errors.append(
                f"{defaults_name}:CONFIG_PARTITION_TABLE_CUSTOM_FILENAME must be {PARTITION_TABLE!r}"
            )

    generated_config_path = build_dir / "config" / "sdkconfig.cmake"
    if not generated_config_path.is_file():
        errors.append(
            f"missing generated build configuration: {generated_config_path}; run idf.py build first"
        )
        return
    generated = parse_generated_kconfig(generated_config_path)
    build_expectations = {
        "CONFIG_IDF_TARGET": "esp32s3",
        "CONFIG_ESPTOOLPY_FLASHSIZE_16MB": "y",
        "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE": "y",
        BOARD_KCONFIG: "y",
        "CONFIG_PARTITION_TABLE_FILENAME": PARTITION_TABLE,
    }
    for key, expected in build_expectations.items():
        if generated.get(key) != expected:
            errors.append(
                f"{generated_config_path.relative_to(repo_root)}:{key} must be {expected!r}, "
                f"got {generated.get(key)!r}; rebuild this directory"
            )


def validate_flash_arguments(
    repo_root: Path,
    build_dir: Path,
    partitions: dict[str, Partition],
    errors: list[str],
) -> dict[int, Path]:
    args_path = build_dir / "flasher_args.json"
    if not args_path.is_file():
        errors.append(f"missing {args_path}; run idf.py build first")
        return {}
    try:
        arguments = json.loads(args_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        errors.append(f"cannot parse {args_path}: {error}")
        return {}

    settings = arguments.get("flash_settings")
    if not isinstance(settings, dict) or settings.get("flash_size") != "16MB":
        errors.append("flasher_args.json must declare flash_settings.flash_size as '16MB'")
    extra_args = arguments.get("extra_esptool_args")
    if not isinstance(extra_args, dict) or extra_args.get("chip") != "esp32s3":
        errors.append("flasher_args.json must target esp32s3")

    raw_flash_files = arguments.get("flash_files")
    if not isinstance(raw_flash_files, dict):
        errors.append("flasher_args.json has no flash_files object")
        return {}

    flash_files: dict[int, str] = {}
    for raw_offset, raw_path in raw_flash_files.items():
        try:
            offset = int(raw_offset, 0)
        except (TypeError, ValueError):
            errors.append(f"invalid flash offset {raw_offset!r}")
            continue
        normalized = normalize_artifact_path(raw_path)
        if normalized is None:
            errors.append(f"unsafe flash artifact path at 0x{offset:x}: {raw_path!r}")
            continue
        flash_files[offset] = normalized

    for offset, expected_path in EXPECTED_FLASH_FILES.items():
        actual_path = flash_files.get(offset)
        if actual_path != expected_path:
            errors.append(
                f"flasher_args.json at 0x{offset:x} must reference {expected_path!r}, got {actual_path!r}"
            )
    unexpected_offsets = sorted(set(flash_files) - set(EXPECTED_FLASH_FILES))
    for offset in unexpected_offsets:
        errors.append(f"flasher_args.json has an unexpected flash entry at 0x{offset:x}: {flash_files[offset]!r}")

    for metadata_name, (expected_offset, expected_path) in EXPECTED_FLASH_METADATA.items():
        metadata = arguments.get(metadata_name)
        if not isinstance(metadata, dict):
            errors.append(f"flasher_args.json is missing {metadata_name!r} metadata")
            continue
        try:
            actual_offset = int(metadata.get("offset"), 0)
        except (TypeError, ValueError):
            actual_offset = -1
        actual_path = normalize_artifact_path(metadata.get("file"))
        if actual_offset != expected_offset or actual_path != expected_path:
            errors.append(
                f"flasher_args.json {metadata_name!r} metadata must be "
                f"0x{expected_offset:x}/{expected_path}"
            )

    artifacts: dict[int, Path] = {}
    build_root = build_dir.resolve()
    for offset, expected_path in EXPECTED_FLASH_FILES.items():
        if flash_files.get(offset) != expected_path:
            continue
        artifact_path = (build_root / Path(expected_path)).resolve()
        try:
            artifact_path.relative_to(build_root)
        except ValueError:
            errors.append(f"artifact escapes build directory: {expected_path!r}")
            continue
        if not artifact_path.is_file():
            errors.append(
                f"missing flash artifact {artifact_path}; run 'idf.py -B {build_dir} blank_ota_data' "
                "after idf.py build when ota_data_initial.bin is missing"
            )
            continue
        artifacts[offset] = artifact_path

    partition_table = artifacts.get(0x8000)
    if partition_table is not None and not 32 <= partition_table.stat().st_size <= 0x1000:
        errors.append(f"partition table artifact has invalid size: {partition_table.stat().st_size} bytes")

    bootloader = artifacts.get(0x0000)
    app = artifacts.get(0x20000)
    for name, artifact in (("bootloader", bootloader), ("application", app)):
        if artifact is not None and artifact.read_bytes()[:1] != b"\xe9":
            errors.append(f"{name} artifact does not have an ESP image header: {artifact}")
    if bootloader is not None and bootloader.stat().st_size > 0x8000:
        errors.append(f"bootloader exceeds its 0x8000-byte region: {bootloader.stat().st_size} bytes")

    otadata = artifacts.get(0x10000)
    expected_otadata_size = EXPECTED_PARTITIONS["otadata"][3]
    if otadata is not None:
        contents = otadata.read_bytes()
        if len(contents) != expected_otadata_size:
            errors.append(
                f"ota_data_initial.bin must be exactly {expected_otadata_size} bytes, got {len(contents)}"
            )
        elif contents != b"\xff" * expected_otadata_size:
            errors.append("ota_data_initial.bin must be entirely erased (0xff); generate it with blank_ota_data")

    bounds = {
        0x0000: (0x0000, 0x8000, "bootloader"),
        0x8000: (0x8000, 0x9000, "partition table"),
        0x10000: (partitions.get("otadata", Partition("", "", "", 0, 0)).offset,
                  partitions.get("otadata", Partition("", "", "", 0, 0)).end,
                  "otadata"),
        0x20000: (partitions.get("ota_0", Partition("", "", "", 0, 0)).offset,
                  partitions.get("ota_0", Partition("", "", "", 0, 0)).end,
                  "ota_0"),
        0xC20000: (partitions.get("assets", Partition("", "", "", 0, 0)).offset,
                    partitions.get("assets", Partition("", "", "", 0, 0)).end,
                    "assets"),
    }
    intervals: list[tuple[int, int, str]] = []
    nvs = partitions.get("nvs")
    for offset, artifact in artifacts.items():
        end = offset + artifact.stat().st_size
        lower, upper, name = bounds[offset]
        if offset != lower or end > upper:
            errors.append(
                f"{artifact.name} does not fit its {name} region: 0x{offset:x}..0x{end:x}, "
                f"allowed 0x{lower:x}..0x{upper:x}"
            )
        if nvs is not None and ranges_overlap(offset, end, nvs.offset, nvs.end):
            errors.append(f"{artifact.name} overlaps NVS; bootstrap must preserve NVS")
        intervals.append((offset, end, artifact.name))
    for index, (start, end, name) in enumerate(sorted(intervals)):
        for next_start, _, next_name in sorted(intervals)[index + 1 :]:
            if next_start >= end:
                break
            errors.append(f"flash artifacts overlap: {name} and {next_name}")

    return artifacts


def validate_release(repo_root: Path, build_dir: Path) -> list[str]:
    """Return all release-blocking errors.  An empty list means the preflight passed."""
    errors: list[str] = []
    repo_root = repo_root.resolve()
    build_dir = build_dir.resolve()
    validate_config(repo_root, build_dir, errors)
    partitions = validate_partition_layout(repo_root, errors)
    validate_flash_arguments(repo_root, build_dir, partitions, errors)
    return errors


def print_manifest(build_dir: Path) -> None:
    print("PASS: Xiaoxin 16 MiB OTA bootstrap release artifacts are internally consistent.")
    for offset, relative_path in EXPECTED_FLASH_FILES.items():
        artifact = build_dir / relative_path
        print(
            f"  0x{offset:06x}  {relative_path:<40} "
            f"{artifact.stat().st_size:>8} bytes  sha256={sha256(artifact)}"
        )


def main(argv: Iterable[str] | None = None) -> int:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        default="build",
        help="ESP-IDF build directory, relative to the repository root by default (default: build)",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root,
        help=argparse.SUPPRESS,
    )
    args = parser.parse_args(argv)
    selected_root = args.repo_root.resolve()
    selected_build = Path(args.build_dir)
    if not selected_build.is_absolute():
        selected_build = selected_root / selected_build

    errors = validate_release(selected_root, selected_build)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print_manifest(selected_build)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
