# 金潮杯博物馆 16 MiB OTA 发布与首刷验收清单

本清单只适用于金潮杯博物馆 Waveshare ESP32-S3 1.46 目标板，以及技术文件 `partitions/xiaoxin-ota-16m.csv` 定义的 16 MiB A/B 布局。`xiaoxin` 只出现在底层兼容文件名中，不是产品名称。

## 发布构建

在已经加载 ESP-IDF 5.5 的 PowerShell 中执行：

```powershell
. "$env:IDF_PATH\export.ps1"
$build = "build-release-<version>"

idf.py -B $build build
idf.py -B $build blank_ota_data
python tools/verify_xiaoxin_ota_release.py --build-dir $build
idf.py -B $build merge-bin
```

`verify_xiaoxin_ota_release.py` 必须返回成功。它应检查：

- 目标芯片为 `esp32s3`、Flash 为 16 MiB，且使用 `partitions/xiaoxin-ota-16m.csv`；
- 分区只有 `nvs`、`phy_init`、`otadata`、`ota_0`、`ota_1` 和 `assets`；
- `flasher_args.json` 中的每个文件存在、地址正确、镜像不越界；
- `ota_data_initial.bin` 大小为 `0x2000` 且内容全部为 `0xff`；
- 应用镜像头和目标板身份校验通过。

发布记录至少保存：版本、中文 Git 提交、目标板、分区布局 ID、镜像字节数、SHA-256、构建目录和校验脚本输出。`merged-binary.bin` 仅用于 USB bootstrap，服务器 OTA 不应把它当作应用镜像发布。

## OTA 配置

开发和生产 URL 必须分开管理。当前仓库未确认生产域名、目录、容器或健康入口，因此不得把历史 IP、旧项目路径或旧服务端配置复制到生产。

生产配置在放行前必须满足：

1. 使用稳定的 HTTPS OTA URL，并关闭不安全 HTTP 选项；
2. 服务端发布记录与设备使用的 URL、版本和布局 ID 一致；
3. 证书链能由设备 CRT bundle 校验；
4. canary 设备先完成一次完整 A/B 循环，再扩大范围。

## USB 首刷前

以下命令只适用于确认 Flash 为 16 MiB、NVS 区域为 `0x9000..0xefff` 的试验设备。来源不明的设备先停止，不套用本文。

```powershell
$port = "COMx"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"

esptool.py --chip esp32s3 --port $port flash_id
esptool.py --chip esp32s3 --port $port read_flash 0x000000 0x1000000 "museum-$stamp-full-16MiB.bin"
esptool.py --chip esp32s3 --port $port read_flash 0x008000 0x1000 "museum-$stamp-partition-table.bin"
esptool.py --chip esp32s3 --port $port read_flash 0x009000 0x6000 "museum-$stamp-nvs.bin"
Get-FileHash "museum-$stamp-*.bin" -Algorithm SHA256
```

禁止执行 `esptool.py erase_flash`、`idf.py erase-flash` 或任何会擦除 NVS 的手工写入命令。完整 Flash 备份必须留在仓库外的受控位置，不能提交到 Git。

## USB 首刷

只有发布构建和预检均通过后，才可执行：

```powershell
idf.py -B $build -p $port flash
```

首刷后记录：

1. 固件版本、中文 Git 提交和设备 ID；
2. 当前启动槽、分区表和 assets 加载结果；
3. Wi-Fi、激活、`museum_state` 显示和普通语音链路结果；
4. NVS 中原有设置是否仍在。

任何一项异常都应停止批量首刷，并使用备份调查。

## A/B OTA 真机循环

在专用试验设备上按顺序完成：

1. 槽 A 运行时发布版本 B，确认只写入槽 B；
2. 下载哈希、尺寸、目标板和布局 ID 校验通过后切换到槽 B；
3. 重启并完成联网、激活和应用健康门槛，确认 B 被标记有效；
4. 再发布版本 C，确认能从槽 B 写回槽 A；
5. 分别提供错误哈希、错误尺寸、错误板型和错误布局 ID，确认全部拒绝且当前镜像不变；
6. 在待验证窗口强制复位，确认回滚到上一个已验证槽；
7. 复测下载中断、断网和服务器 5xx，确认已验证版本仍可启动。

## 验收边界

静态预检和 Python 测试只能证明源码和发布物合同，不能代替屏幕观察、麦克风/ASR、音频效果或真实 OTA 循环。服务端启动成功也不能证明旧线上提醒已经停止；生产日志、数据目录和运行版本必须单独取证。
