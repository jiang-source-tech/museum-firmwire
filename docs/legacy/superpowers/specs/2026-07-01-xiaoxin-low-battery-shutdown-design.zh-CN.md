# 小新低电主动关机设计

## 背景

小新 1.46C 使用单节 3.7V 锂电池供电时，1500mAh 电池可以运行约 11 小时。尾电阶段出现屏幕背光变暗、系统无法正常工作、尝试重启但启动失败、随后关机的现象。

这类现象更像电池尾电阶段的带载电压塌陷，而不是普通应用逻辑主动重启。单节锂电池的 3.7V 是标称电压；满电约 4.2V，放电中段在 3.7V 附近，接近耗尽时会低于 3.6V。ESP32-S3、屏幕、Wi-Fi、PSRAM 和音频在启动或活动时会产生峰值电流，尾电电池内阻导致端电压进一步下降，可能触发 brownout reset。

当前板卡没有独立 fuel gauge，固件能可靠使用的是 GPIO8 / ADC1_CH7 上的电池分压采样，以及 GPIO7 / `PWR_Control_PIN` 形式的电源保持锁存。目标是在 brownout 之前由固件主动释放电源保持，让设备像普通电子产品一样低电后真正关机，而不是在欠压边缘反复重启。

## 目标

1. 用现有 ADC 电池电压采样建立运行时低电监测。
2. 在低电阶段提示用户，但不关闭项目的核心体验。
3. 在临界低电阶段显示即将关机提示，然后主动释放电源保持。
4. 避免低电 brownout 后自动重启循环。
5. 保留诊断信息，便于确认是否发生低电主动关机或欠压复位。

## 非目标

1. 不新增 fuel gauge、PMIC 或外部硬件依赖。
2. 不实现“临界保护运行模式”。临界电量时不停止动画、Wi-Fi、音频后继续苟活。
3. 不重写整套电量百分比 UI。
4. 不把瞬时 ADC 尖峰直接当作关机依据。
5. 不改变手动长按电源键关机的既有语义。

## 现状

板级代码已经有启动早期的电源识别：

- `InitializeBatteryAdc()` 初始化 GPIO8 / ADC1_CH7。
- `DetectPowerSourceEarly()` 通过 3 次 ADC 采样判断电池或 USB/外部供电。
- `InitializePowerHoldEarly()` 拉高 `PWR_Control_PIN` 维持供电。
- `RequestPowerOff()` 已能关背光、记录 runtime health，并释放电源保持。

仓库中也已有电池状态模型：

- `xiaoxin_battery_level.c` 将锂电池电压映射为百分比。
- `xiaoxin_battery_state.c` 有 NORMAL / LOW / CRITICAL 状态、滤波、滞回和边沿事件。

但当前小新板级 `GetBatteryLevel()` 直接返回 `false`，运行时未把 ADC 周期采样接入电池状态机，也没有低电主动关机路径。

## 设计

### 决策口径

低电和临界关机只以 `xiaoxin_battery_state_update()` 产出的状态机 snapshot 为准。ADC 电压是状态机输入，也是日志和校准依据，但板级代码不再另起一套并行的 LOW / CRITICAL 电压判断。

现有状态机已经通过 `xiaoxin_battery_level.c` 将电压映射为百分比，再用百分比和确认时间判断状态：

| 状态转换 | 状态机阈值 | 当前曲线约等效带载电压 | 当前确认时间 |
| --- | ---: | ---: | ---: |
| UNKNOWN/NORMAL -> LOW | <= 20% | 约 3700mV | idle 20s / voice 45s |
| UNKNOWN -> CRITICAL | <= 10% | 约 3600mV | idle 10s / voice 30s |
| LOW -> CRITICAL | <= 10% | 约 3600mV | idle 10s / voice 30s |
| CRITICAL -> LOW | >= 18% | 约 3680mV | 15s |
| LOW -> NORMAL | >= 30% | 约 3750mV | 10s |

本设计采用这套状态机阈值，不在板级代码里重复定义另一套 LOW / CRITICAL 电压。如果后续硬件实测证明阈值不合适，应先修改 `xiaoxin_battery_level.c` 或 `xiaoxin_battery_state.c` 并配套测试，而不是在板级代码里绕过状态机。

### 状态策略

采用两档策略：

1. 低电提醒
   - 状态机进入 LOW。
   - 显示低电提示。
   - 可以轻度降低背光，但不停止宠物、动画、网络或音频。
   - 系统继续正常运行。

2. 临界主动关机
   - 状态机进入 CRITICAL。
   - 显示“电量不足，即将关机”提示。
   - 短暂延迟后关背光、记录诊断、释放 `PWR_Control_PIN`。
   - 设备真正断电，不调用 `esp_restart()`。

### 阈值调整原则

本次实现不新增板级低电电压阈值。阈值由状态机集中维护：

1. LOW 由 `k_normal_to_low_percent` 控制。
2. CRITICAL 由 `k_low_to_critical_percent` 控制。
3. LOW / CRITICAL 的确认时间由 `required_ms_for()` 对应常量控制。
4. 启动保护使用同一套状态机结果，不单独定义一个更低的启动电压。

如果实测发现 10% / 3600mV 仍然太晚，下一步应把 CRITICAL 阈值上调，例如从 10% 调到 12%-15%，并让所有 LOW / CRITICAL 行为继续共用同一套状态机。

### ADC 采样

新增一个运行时电池监测任务或定时器，复用现有 ADC 初始化：

1. 每 2s 读取多次 ADC，取平均值。
2. 将 ADC 引脚电压乘以分压比，得到电池端电压。
3. 将采样喂给 `xiaoxin_battery_state_update()`。
4. 根据 snapshot 判断 LOW / CRITICAL。
5. 打印节流日志，例如每 60s 输出一次电压、估算百分比、电源来源和状态。

采样失败时不触发关机。连续多次失败只记录诊断，并保持现有状态。

如果后续确认 1.46C 存在可靠的 USB/充电检测 GPIO，应将它转成 `xiaoxin_battery_power_hint_t` 传入状态机。没有硬件提示时，先使用状态机现有的高电压、快速上升、稳定高位和交替采样检测来识别外部供电。

### `GetBatteryLevel()` 语义

小新板级 `GetBatteryLevel(int& level, bool& charging, bool& discharging)` 从最近一次电池状态机 snapshot 生成结果：

1. 当 `percent_reliable == true` 时返回 `true`，`level = display_percent`，范围 0-100。
2. `charging = snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL`。
3. `discharging = snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY`。
4. 当外部供电导致百分比不可靠时返回 `false`，避免把 USB 电压误报成 100% 电池。
5. ADC 尚未初始化、没有有效样本、或电源来源仍 UNKNOWN 时返回 `false`。

这让系统状态 JSON 和 UI 能在电池供电时拿到真实电量，同时避免外部供电时显示虚假的电池百分比。

### 低电提示

LOW 状态首次确认时触发一次用户可见提示：

- 使用 snapshot 的 `low_edge` 标志触发一次性提示，而不是每次看到 `state == LOW` 都重复弹出。
- 显示低电通知或系统 overlay。
- 播放低电提示音可以后续再做，本次不强制。
- 背光是否降低应保守处理：如果降低，必须允许用户在交互中仍然看清屏幕。

本阶段不关闭核心功能。用户仍然可以继续和小新交互，直到进入临界关机。

### 临界关机

CRITICAL 状态确认后进入一次性关机流程：

1. 使用 snapshot 的 `critical_edge` 标志触发关机流程，并标记 `low_battery_shutdown_requested_`，避免重复触发。
2. 显示“电量不足，即将关机”提示，持续约 2-3 秒。
3. 提示等待期间继续采样；如果状态机确认外部供电，或电压明显反弹并产生恢复信号，则取消本次关机并清除一次性关机标志。
4. 调用 runtime health checkpoint，记录低电主动关机原因。
5. 设置背光为 0。
6. 释放 `PWR_Control_PIN`。
7. 进入已有 `WaitForPowerButtonReleaseAndSleep()` 或等效收尾路径。

释放 `PWR_Control_PIN` 是低电关机的核心动作。执行后电源锁存可能立刻打开，VCC 可能在 `esp_deep_sleep_start()` 执行前就下降；这属于预期结果。深睡眠只是外部供电仍存在或电源没有立刻塌陷时的收尾，不是关机成功的必要条件。

如果电压已经接近 brownout，提示可能来不及完整显示，但流程仍应尽快释放电源保持。

### 启动保护

启动早期已经会识别电池供电。新增启动低电保护时要区分两个阶段：

1. 快速保护阶段：`RuntimeHealthProtectionRecommended()` 是启动早期的主要可用信号。它来自 NVS 历史数据，不需要等待 ADC 状态机 10 秒确认窗口。电池供电且 runtime health 建议保护时，尽早显示恢复/低电提示并关机，不进入完整应用启动。
2. 状态机接管阶段：如果没有命中 runtime health 快速保护，系统可以继续完成必要启动，并启动 ADC 周期采样。状态机确认 CRITICAL 后再触发运行期临界关机流程。
3. 最小 UI 阶段：快速保护需要提示用户时，只初始化显示提示所需的最小屏幕和背光路径，不拉起 Wi-Fi、音频和完整应用。
4. 释放电源保持：提示结束后释放 `PWR_Control_PIN`。

这避免尾电电池在重启时反复拉起 Wi-Fi、屏幕和 PSRAM，造成启动失败循环。

状态机的 UNKNOWN -> CRITICAL 路径仍然有价值，但它不是启动早期的快速门禁。它覆盖的是首次有效采样已经处于尾电、并且设备有足够时间完成确认窗口的场景。

### 诊断

新增或扩展诊断信息：

- 最近一次主动低电关机的电压。
- 最近一次低电关机发生在启动阶段还是运行阶段。
- runtime health 中区分主动低电关机和 brownout reset。
- 低电关机记录持久化到 runtime health。设备低电断电后，重新插 USB 可通过串口 `runtime_health` 读取上一次主动低电关机次数、电压和阶段。
- 串口 `battery` 命令只作为 USB 接入后的当前采样辅助诊断；因为 USB Serial/JTAG 连接本身会改变供电来源，它不作为纯电池低电关机的验收手段。

诊断必须是辅助信息，不应阻塞关机。

## 错误处理

1. ADC 初始化失败：保留现有行为，不做自动低电关机。
2. 单次 ADC 异常值：丢弃，不改变状态。
3. 电压在阈值附近抖动：依靠持续时间和状态机滞回抑制反复提示。
4. 临界关机流程重复触发：用一次性标志忽略后续触发。
5. 显示对象未就绪：跳过 UI 提示，直接记录诊断并关机。
6. LOW 恢复：状态机 recovered_edge 出现，或外部供电确认后，隐藏低电提示并恢复正常显示策略。
7. CRITICAL 提示期间插入 USB：如果外部供电确认或电压恢复信号在释放电源保持前到达，取消本次自动关机。

## 测试计划

1. 扩展 `xiaoxin_battery_state_test.c`，覆盖 LOW 与 CRITICAL 的确认时间、滞回、边沿事件和阈值等效电压说明。
2. 新增板级路径测试，确认小新板卡在 `percent_reliable == true` 时 `GetBatteryLevel()` 返回最近 snapshot 的 `display_percent`，并正确填充 charging/discharging。
3. 新增路径测试，确认 `low_edge` 只触发一次低电提示，`critical_edge` 触发主动关机流程而不是 `esp_restart()`。
4. 新增路径测试，确认 CRITICAL 提示期间外部供电确认或恢复信号会取消自动关机。
5. 新增启动保护路径测试，确认电池供电且 runtime health 建议保护时不会继续完整启动；状态机 CRITICAL 只在采样确认后接管运行期关机。
6. 运行现有电池、电源控制、启动诊断和 runtime health 测试。
7. 硬件验证：用可调电源或低电电池观察约 3.7V/20% 附近提示、约 3.6V/10% 附近主动关机、关机后不自动重启。低电关机后重新插 USB，通过 `runtime_health` 确认上一次低电关机记录；不要用插着 USB 时的 `battery` 命令验收纯电池状态。

## 实施顺序

1. 先补测试，锁定低电主动关机行为。
2. 接入运行时 ADC 采样到电池状态机。
3. 接入 LOW 提示。
4. 接入 CRITICAL 主动关机。
5. 加启动低电保护。
6. 补串口/诊断输出。

## 验收标准

1. 电池运行到低电时有用户可见提醒。
2. 临界低电时设备主动关机，不调用重启。
3. 主动关机后不会自动尝试启动完整系统。
4. USB/外部供电不触发低电关机。
5. 现有手动电源键关机不回退。
6. 所有相关自动化测试通过。
