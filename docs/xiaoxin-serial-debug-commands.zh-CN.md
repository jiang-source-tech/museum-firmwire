# 小新串口调试命令

本文档记录当前小新 1.46 固件可用的串口调试命令，以及在 VSCode ESP-IDF Monitor 里使用时需要注意的点。

## 使用方式

1. 烧录并启动固件后，打开 VSCode 的 ESP-IDF Monitor。
2. 看到提示符 `xiaoxin>` 后，输入一条命令并按回车。
3. 每次只输入一条完整命令，例如：

```text
notify_test
```

不要连续粘贴成 `notify_testnotify_test`，否则 REPL 会把它当成一条不存在的命令，并显示 `Unrecognized command`。

## 当前命令列表

| 命令 | 用途 | 成功输出 | 备注 |
| --- | --- | --- | --- |
| `notify_test` | 创建一条测试通知，并切换到通知页面。 | `notify_test: opened notification page` | 用于检查通知卡片写入和通知分页渲染。 |
| `boot_diag` | 打印上一次启动和当前启动的诊断轨迹。 | `previous boot (...)` / `current boot (...)` | 用于排查电池启动卡住、Wi-Fi/OTA/应用初始化阶段失败等问题。 |
| `battery` | 打印当前电池监测状态。 | `battery: voltage=...` | 只能作为 USB 接入后的辅助诊断；接 USB 会让板子进入 USB/外部供电场景，不能用它验收纯电池低电关机。 |
| `runtime_health` | 打印运行健康快照。 | `runtime: current=...` | 用于排查电池供电下反复重启；低电关机后重新插 USB，可用它读取上一次低电关机记录。 |

## `notify_test`

`notify_test` 用来快速检查通知中心卡片和通知分页效果。

执行后固件会做这些事情：

1. 创建或更新一条测试通知。
2. 通知标题为 `Notify test`。
3. 通知正文为 `Serial debug notification`。
4. 通知标签为 `Debug`。
5. 打开通知页面。
6. 不弹出悬浮窗；普通通知只保存在通知中心，供用户进入通知页查看。

如果显示：

```text
notify_test: display is not ready
```

说明命令执行时显示对象还没有准备好，通常等固件启动完成后再输入一次即可。

## `boot_diag`

`boot_diag` 打印上一次启动和当前启动的诊断轨迹。输出通常是两行：

```text
previous boot (battery): boot_start>...
current boot (usb): boot_start>...
```

如果 NVS 里没有对应记录，会显示：

```text
previous boot: <empty>
current boot: <empty>
```

这个命令适合在设备出现“电池启动后卡住、反复重启、OTA 或 Wi-Fi 阶段异常”时使用。低电导致设备关机后，也可以重新插 USB 查看上一次电池启动卡在了哪个阶段。

## `runtime_health`

`runtime_health`：打印本次运行、上次运行、最长运行、最近重启原因、欠压次数、短运行连续次数、当前供电判断，以及持久化保存的低电主动关机次数、最近低电关机电压和阶段。用于排查电池供电下反复重启。

输出格式：

```text
runtime: current=<duration> last=<duration> max=<duration> reset=<kind> brownout=<count> short_streak=<count> battery=<0|1> low_shutdowns=<count> low_mv=<mV> low_stage=<startup|runtime>
```

低电关机后，设备已经断电，不能在纯电池状态下继续输入串口命令。此时重新插 USB 再执行 `runtime_health`，重点看：

- `low_shutdowns`：主动低电关机累计次数。
- `low_mv`：最近一次主动低电关机记录的电池电压。
- `low_stage`：最近一次发生在 `startup` 启动保护阶段，还是 `runtime` 运行阶段。

## `battery`

打印小新 1.46C 当前电池监测状态：最近 ADC 电压、样本年龄、状态机状态、电源来源、显示百分比、显示档位、百分比是否可靠，以及低电关机是否正在等待执行。

输出格式：

```text
battery: voltage=<mV> age=<ms> state=<unknown|normal|low|critical> source=<unknown|battery|external> percent=<0-100> level=<0-4> reliable=<0|1> shutdown_pending=<0|1>
```

注意：当前小新固件的交互控制台通常走 USB Serial/JTAG。插入 USB 线后，板子会同时得到 USB/外部供电，电池状态机可能切到 `source=external`，百分比也可能变成不可靠。因此 `battery` 命令适合确认“固件是否在采样、状态机是否更新、插 USB 后是否识别成外部供电”，不适合验收“纯电池供电下何时 LOW/CRITICAL/关机”。

## VSCode Monitor 注意事项

当前小新固件的交互控制台配置在 USB Serial/JTAG 上。VSCode 里需要选中实际连接小新设备的串口，例如之前使用的 `COM4`。

由于 USB 连接本身会改变供电条件，纯电池低电关机的核心验证应使用可调电源/低电电池观察板上提示、背光熄灭和 GPIO7 释放；关机后再插 USB，用 `runtime_health` 读取上一次低电关机记录。`battery` 命令只看接 USB 后的当前状态。

如果输入命令时出现类似提示：

```text
Warning: Writing to serial is timing out
```

一般表示 Monitor 没有连到可交互的控制台端口，或者当前选择的 console 类型不对。确认点：

1. VSCode ESP-IDF Monitor 选择的是小新设备对应的串口。
2. 固件已经重新烧录了包含串口命令的版本。
3. Monitor 里能看到 `xiaoxin>` 提示符后再输入命令。

串口日志可能会插入到命令行附近，例如电池电压日志会周期性打印。输入命令时以 `xiaoxin>` 后面的内容为准，输完一条命令就按回车。

## 其他板卡命令

仓库里的 `sensecap-watcher` 板卡代码还注册了 `reboot`、`shutdown`、`battery`、`factory_reset`、`read_mac`、`version` 等命令。这些命令属于 SenseCAP Watcher 板卡，不属于当前小新 1.46 固件；在小新 Monitor 里不要按这些命令来测试。

## 维护位置

小新串口命令注册位置：

```text
main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc
```

当前命令在 `InitializeDebugConsole()` 中注册。以后新增、删除或修改串口调试命令时，需要同步维护：

1. 本文档的“当前命令列表”和对应命令章节。
2. 命令的成功输出格式。
3. 使用限制，尤其是命令是否会因为 USB 供电改变被测场景。
4. `tests/xiaoxin_serial_debug_command_test.py` 里的文档覆盖检查。
