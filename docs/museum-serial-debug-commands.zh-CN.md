# 金潮杯博物馆串口调试命令

本文档记录金潮杯博物馆 1.46 固件当前可用的串口调试命令，以及在 VSCode ESP-IDF Monitor 中使用时的限制。

## 使用方式

1. 烧录并启动固件后，打开 VSCode 的 ESP-IDF Monitor。
2. 看到提示符 `museum>` 后，输入一条命令并按回车。
3. 每次只输入一条完整命令，例如 `notify_test`。不要连续粘贴成 `notify_testnotify_test`。

## 当前命令

| 命令 | 用途 | 成功输出 |
| --- | --- | --- |
| `notify_test` | 在博物馆主界面显示一条临时测试通知。 | `notify_test: displayed temporary notification` |
| `museum_listen` | 请求打开语音链路并进入聆听状态。 | `museum_listen: listening requested` |
| `boot_diag` | 打印上一次和本次启动诊断轨迹。 | `previous boot (...)` / `current boot (...)` |
| `battery` | 打印当前电池监测状态。 | `battery: voltage=...` |
| `runtime_health` | 打印运行健康快照。 | `runtime: current=...` |

## `notify_test`

固件通过临时通知接口显示 `Serial debug notification`，不会创建旧通知卡片，也不会打开已移除的分页页面。

```text
notify_test: displayed temporary notification
```

如果显示 `notify_test: display is not ready`，等待启动完成后重试。

## `museum_listen`

请求打开当前配置的语音通道并开始聆听。该命令只验证语音链路入口，不代表麦克风、ASR 或真机整链路验收已经通过。

## `boot_diag`

适用于排查电池启动卡住、反复重启以及 Wi-Fi、OTA 或应用初始化阶段异常。低电关机后重新接入 USB，再用该命令读取上一次启动记录。

## `runtime_health`

输出本次运行、上次运行、最长运行、重启原因、欠压次数、短运行连续次数、供电判断，以及持久化的低电主动关机信息：

```text
runtime: current=<duration> last=<duration> max=<duration> reset=<kind> brownout=<count> short_streak=<count> battery=<0|1> low_shutdowns=<count> low_mv=<mV> low_stage=<startup|runtime>
```

低电关机后设备已断电，需重新接 USB 才能执行命令。

## `battery`

输出最近 ADC 电压、样本年龄、状态、电源来源、显示档位、可靠性和低电关机等待状态：

```text
battery: voltage=<mV> age=<ms> state=<unknown|normal|low|critical> source=<unknown|battery|external> percent=<0-100> level=<0-4> reliable=<0|1> shutdown_pending=<0|1>
```

USB 接入会改变供电条件，`battery` 适合检查采样和状态机，不适合单独验收纯电池低电关机阈值。

## Monitor 注意事项

- 控制台使用 USB Serial/JTAG；确认选择的是实际设备串口。
- 日志可能插入命令行，始终以 `museum>` 后的输入为准。
- `Warning: Writing to serial is timing out` 通常表示端口或 console 类型不正确。
- 文字接口、串口命令和模拟设备测试不能表述为麦克风、ASR 或真机验收通过。

## 维护位置

串口命令注册位置：

```text
main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc
```

命令在 `InitializeDebugConsole()` 中注册。修改命令时同步更新本文件和 `tests/museum_serial_debug_command_test.py`。
