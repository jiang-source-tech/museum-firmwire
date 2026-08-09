# 小芯 16MiB OTA bootstrap

## 目的

`partitions/xiaoxin-ota-16m.csv` 是小芯 ESP32-S3 设备的一次性 USB
bootstrap 布局。bootstrap 完成后，固件从 `ota_0` 或 `ota_1` 启动，后续
Wi-Fi OTA 可以把新镜像写入非运行槽并切换启动槽。

这份布局不是对仍使用 `factory + assets` 设备的远程升级方案。旧布局没有
`otadata` 和 OTA 应用槽，现有 OTA 写入路径无法把分区表远程转换为该布局。

## 布局

| 分区 | 地址 | 大小 | 用途 |
| --- | ---: | ---: | --- |
| `nvs` | `0x9000` | `0x6000` | 保持现有设备设置、Wi-Fi 和激活状态 |
| `phy_init` | `0xf000` | `0x1000` | PHY 初始化数据 |
| `otadata` | `0x10000` | `0x2000` | A/B 启动选择和回滚状态 |
| `ota_0` | `0x20000` | `0x600000` | 应用槽 A |
| `ota_1` | `0x620000` | `0x600000` | 应用槽 B |
| `assets` | `0xc20000` | `0x3e0000` | 默认资源和语音模型 |

所有分区恰好结束于 `0x1000000`（16MiB）。`assets` 是运行时和 CMake
烧录流程要求的标签；不要用旧的仅含 `model` 的 16MiB 表替换它。

## 一次性 USB bootstrap

1. 在 ESP-IDF 环境中重新配置并构建，使 `build/flasher_args.json` 反映此表。
2. 确认生成物包含 bootloader、partition table、`ota_data_initial.bin`、
   `ota_0` 应用镜像和 `assets` 镜像。
3. 保持外部供电稳定，使用构建生成的 `idf.py flash` 或
   `flasher_args.json` 完成一次 USB 刷写。
4. 不要执行 `erase_flash`：NVS 地址与大小被保留，bootstrap 应保留其内容。
5. 首次启动后记录运行槽、版本、设备 ID 和资源加载日志；随后才可验证
   `ota_0 -> ota_1 -> ota_0` 的远程升级和回滚。

分区表切换期间断电会要求使用 USB 重刷恢复。当前切片仅提供 A/B 分区与
bootstrap 前提；HTTPS、manifest 完整性校验、安装安全门槛和发布放量策略
属于后续 OTA 切片。

## 重置语义

该布局没有 `factory` 应用分区。因此“恢复出厂”按钮只清除 NVS 持久化数据
并重启，不擦除 `otadata`，从而不会改变已经验证的 OTA 固件选择。
