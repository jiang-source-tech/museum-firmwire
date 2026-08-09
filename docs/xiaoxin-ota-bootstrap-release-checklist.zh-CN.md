# 小芯 16 MiB OTA：首刷、发布与真机验收清单

这份清单只适用于小芯的 ESP32-S3 1.46 英寸板卡，以及
`partitions/xiaoxin-ota-16m.csv` 定义的 16 MiB A/B 布局。它把现有
`factory + assets` 布局转换为 `ota_0 + ota_1 + assets` 布局；首刷必须通过
USB 完成。没有 `ota_0`、`ota_1` 和 `otadata` 的旧设备不能靠当前 Wi-Fi OTA
代码远程改写分区表。

## 已知构建陷阱与固定做法

ESP-IDF 5.5 的普通 `idf.py build` 对应 Ninja 的 `all` 目标。它会生成应用、
bootloader、分区表和 `flasher_args.json`，但**不依赖**
`blank_ota_data`。`flasher_args.json` 却已经引用了
`ota_data_initial.bin`；只有 `flash` 目标会自动拉起该文件。

因此，不能在 `idf.py build` 后直接复制或合并 `flasher_args.json` 里的文件。
必须额外执行：

```powershell
idf.py -B $build blank_ota_data
```

该目标由 ESP-IDF 的 `gen_empty_partition.py` 按 `otadata` 分区大小生成全
`0xff` 的 `ota_data_initial.bin`。不要手工创建、截断或复用这个二进制。
`scripts/release.py` 现在会在其 `flasher_args.json` 包含该文件时先调用此目标，
以保证 `merge-bin` 不再引用缺失产物。

## 可复现的发布构建

在已经加载 ESP-IDF 5.5 环境的 PowerShell 中执行。若尚未加载环境，先运行安装
目录下的 `export.ps1`；以下示例假定 `IDF_PATH` 已指向对应 ESP-IDF 目录。

```powershell
. "$env:IDF_PATH\export.ps1"

# 每次发布使用新的目录，不复用不明来源的 build 产物。
$build = "build-release-<version>"

idf.py -B $build build
idf.py -B $build blank_ota_data
python tools/verify_xiaoxin_ota_release.py --build-dir $build
idf.py -B $build merge-bin
```

`verify_xiaoxin_ota_release.py` 可直接用于 CI 或人工放行，失败时返回非零状态。
它检查：

- 源配置和生成配置都选择 `esp32s3`、16 MiB、小芯板型和
  `partitions/xiaoxin-ota-16m.csv`；
- 分区精确为 `nvs`、`phy_init`、`otadata`、`ota_0`、`ota_1`、`assets`，且不再有
  `factory` 应用分区；
- `flasher_args.json` 只写入 bootloader、分区表、空白 `otadata`、`ota_0` 和
  assets，绝不触碰 NVS；
- 每个文件存在、起始地址正确、镜像不越过所属分区，并验证 ESP 镜像头；
- `ota_data_initial.bin` 恰为 `0x2000` 字节且全部为 `0xff`。

通过后保存脚本输出的 SHA-256、Git 提交、固件版本、`flasher_args.json`、
分区表和发布的 `ai_pet.bin`。`merged-binary.bin` 仅用于 USB bootstrap；后续 OTA
服务器只发布校验过的应用镜像以及与其匹配的元数据。

当前实测构建的容量基线是：`ai_pet.bin = 0x56db00`，每个 OTA 槽为
`0x600000`，剩余 `0x92500`（约 10%）。容量低于零或预检报越界，发布直接停止。

## OTA 传输配置

仓库中 `sdkconfig`、`sdkconfig.defaults` 和 `sdkconfig.defaults.xiaozhi` 所指向的
`http://124.221.253.206:8003/xiaoxin/ota/` 是受控开发 bootstrap 配置，并显式设置
`CONFIG_OTA_ALLOW_INSECURE_HTTP=y`。它只用于隔离开发网络和首次联调；不要把它当作
生产发布配置。

生产发版前必须同时完成以下配置：

1. 给 `CONFIG_OTA_URL` 设置稳定的 `https://<domain>/xiaoxin/ota/`，并关闭
   `CONFIG_OTA_ALLOW_INSECURE_HTTP`。
2. 在服务端 `xiaoxin_control.ota_release.public_ota_url` 配置相同的 HTTPS URL；保持
   `allow_insecure_http: false`。
3. 确认反向代理证书链能被 ESP-IDF CRT bundle 校验，再发布 canary。没有这三项，不得把
   当前 IP/HTTP 配置发布给稳定设备。

## 首次 USB bootstrap

### 1. 刷写前确认与备份

以下步骤针对当前部署的 `partitions/v2/16m.csv` factory 设备：其 NVS 是
`0x9000..0xefff`（大小 `0x6000`），新布局保留了这一区间。任何分区表来源不明、
Flash 容量不是 16 MiB、或 NVS 区域不同的设备，不要套用这套命令。

```powershell
$port = "COMx"                    # 改为实际串口
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"

esptool.py --chip esp32s3 --port $port flash_id

# 必做：保留整片 Flash，包含可恢复的旧分区表、固件和设备状态。
esptool.py --chip esp32s3 --port $port read_flash 0x000000 0x1000000 "xiaoxin-$stamp-full-16MiB.bin"

# 便于快速比对的两个单独备份。
esptool.py --chip esp32s3 --port $port read_flash 0x008000 0x1000 "xiaoxin-$stamp-partition-table.bin"
esptool.py --chip esp32s3 --port $port read_flash 0x009000 0x6000 "xiaoxin-$stamp-nvs.bin"

Get-FileHash "xiaoxin-$stamp-*.bin" -Algorithm SHA256
```

这里**禁止**执行 `esptool.py erase_flash`、`idf.py erase-flash` 或任何会擦除
`0x9000..0xefff` 的手工写入命令。标准 `idf.py flash` 使用经过预检的
`flasher_args.json`，写入的是：

| 地址 | 内容 |
| --- | --- |
| `0x0000` | bootloader |
| `0x8000` | partition table |
| `0x10000` | ESP-IDF 生成的空白 `otadata` |
| `0x20000` | 初始 `ota_0` 应用镜像 |
| `0xc20000` | assets 镜像 |

它不写入 NVS。新表没有 factory 应用分区；空白 `otadata` 在没有 factory 可选时使
bootloader 从 `ota_0` 启动。

### 2. 执行首刷

只在上一节的构建和预检都通过后执行：

```powershell
idf.py -B $build -p $port flash
```

不要手填旧文档里的地址，也不要把 `merged-binary.bin` 与另一份分区表混用。断电、
写错表或设备不属于上述旧布局时，使用刚才的完整 Flash 备份经 USB 恢复，而不是尝试
用 OTA 修复分区表。

### 3. 首刷后的最小验收

1. 记录启动日志中的版本、设备 ID、Wi-Fi 连接结果、当前启动分区和 assets 加载结果。
2. 确认设备从 `ota_0` 正常启动，并在激活和联网健康门槛完成后将该镜像标记为有效。
3. 确认原有 Wi-Fi/激活等 NVS 持久化状态仍在；若不在，停止批量首刷并从备份调查。
4. 在一台试验设备上先完成完整 A/B OTA 验收，再进入批量 bootstrap。

## 后续 OTA 发布和真机验收

每个固件版本均从“可复现的发布构建”重新生成 `ai_pet.bin`，将其 SHA-256、精确
字节数、版本、目标板型和分区布局 ID 写入服务端发布记录。服务端返回的值必须与该次
构建产物逐字节匹配；设备应拒绝缺字段、哈希不匹配、尺寸不匹配、板型不匹配或布局
不匹配的 offer。

在试验设备上按顺序验收：

1. 设备从 `ota_0` 运行，发布版本 B；确认只向非运行的 `ota_1` 写入，下载哈希和尺寸
   通过后才切换启动分区。
2. 重启并完成联网、激活和应用健康门槛；确认 B 被标记有效且设备稳定留在 `ota_1`。
3. 发布版本 C；确认相同流程从 `ota_1` 写回 `ota_0`。
4. 人为提供错误哈希、错误尺寸、错误板型和错误布局 ID 的 offer；每一种都必须拒绝，
   当前可启动镜像不能改变。
5. 在“已设置待验证镜像、尚未标记有效”的窗口强制复位，确认 bootloader 回滚到上一个
   已验证槽。这个测试只能在专用试验设备上做。
6. 在下载中断电、断网和服务器 5xx 后复测：当前已验证版本仍可启动，下一次检查可恢复。
7. 复测 Wi-Fi、激活、设备 ID、NVS 设置和 assets；A/B 切换不得破坏它们。

预检和编译通过只证明发布物在静态层面自洽；没有完成上述 USB 首刷和 A/B 真机循环，
不能把 OTA 流程标记为硬件验收完成。
