# 金潮杯博物馆 16 MiB OTA Bootstrap

## 目的

`partitions/xiaoxin-ota-16m.csv` 是当前 Waveshare ESP32-S3 1.46 目标板使用的一次性 USB bootstrap 布局。分区文件和校验脚本仍保留历史 `xiaoxin` 文件名，这是底层兼容标识，不代表产品身份。

bootstrap 完成后，固件从 `ota_0` 或 `ota_1` 启动；后续 Wi-Fi OTA 只写入非运行槽，并在镜像完整性和启动健康检查通过后切换启动槽。

这不是把旧 `factory + assets` 设备远程转换为 A/B 布局的方案。旧设备没有 `otadata` 和双 OTA 应用槽，必须先通过 USB 刷写并保留 NVS。

## 分区布局

| 分区 | 地址 | 大小 | 用途 |
| --- | ---: | ---: | --- |
| `nvs` | `0x9000` | `0x6000` | 设备设置、Wi-Fi 和激活状态 |
| `phy_init` | `0xf000` | `0x1000` | PHY 初始化数据 |
| `otadata` | `0x10000` | `0x2000` | A/B 启动选择和回滚状态 |
| `ota_0` | `0x20000` | `0x600000` | 应用槽 A |
| `ota_1` | `0x620000` | `0x600000` | 应用槽 B |
| `assets` | `0xc20000` | `0x3e0000` | 运行时资源 |

所有分区应恰好结束于 `0x1000000`（16 MiB）。不要用其他分区表替换该文件。

## 一次性 USB bootstrap

1. 在已加载 ESP-IDF 5.5 的环境中重新配置并构建。
2. 确认构建产物包含 bootloader、分区表、`ota_data_initial.bin`、`ota_0` 和 `assets`。
3. 使用构建生成的 `idf.py flash` 或 `flasher_args.json` 刷写。
4. 禁止执行 `erase_flash`；bootstrap 必须保留 NVS。
5. 首次启动后记录版本、设备 ID、运行槽和资源加载结果，再进行 A/B OTA 验证。

分区表切换期间断电只能通过 USB 和完整 Flash 备份恢复。没有完成专用试验设备上的 A/B 循环，不得宣称 OTA 真机验收完成。

## 当前边界

- 生产服务器目录、域名、容器和健康入口尚未在项目中确认，本文不提供可直接执行的生产部署命令。
- OTA URL、发布元数据和服务端发布记录必须由部署方案确认后填写；不得沿用历史旧项目地址。
- 服务端发布记录必须绑定镜像 SHA-256、字节数、版本、目标板和分区布局 ID。
