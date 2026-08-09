# 小芯真实处境到宠物 GIF 映射设计

日期：2026-07-09

范围：Waveshare ESP32-S3 Touch LCD 1.46 小芯/泡泡宠物固件。

## 背景

当前宠物主页已经具备 GIF 播放、触摸/拖动/摇晃、服务端 emotion、语音状态和网络状态到宠物表现的基础链路。问题不在于重新设计宠物主页，也不在于新增一套轻量状态快照，而是把少数真实设备处境稳定映射到已有宠物 GIF，让用户看到“小芯会对自己的处境有反应”。

本轮设计只复用已有 GIF 和状态机，不新增 GIF 资源，不新增长期人格系统，不新增独立状态快照。

## 目标

1. 当电量稳定进入一格低电状态时，小芯播放 `tired.gif`。
2. 保持 WiFi 断开/恢复、语音错误、长时间无互动、服务端 emotion 和本地交互的现有表现路径，并把它们写入 requirement 状态。
3. 所有真实处境映射只响应状态边沿，避免重复采样导致宠物频繁跳变。
4. 映射不得打断说话、聆听、思考、失败、睡眠等受保护状态。

## 非目标

1. 不做充电识别。当前只有 BAT_ADC/电压推断，没有可靠 CHG/PG/VBUS 状态输入；`power_source == external` 只能作为外部供电推断，不能等同“正在充电”。
2. 不做电量恢复开心反馈。没有可靠充电识别时，恢复反馈容易误导用户。
3. 不做 0 格严重低电映射。0 格是否能稳定出现尚未实机验证，本轮只处理用户可确认的一格低电。
4. 不做能量值、亲密度、长期心情值或新 GIF 资源。

## 映射表

| 真实处境 | 可靠触发 | 宠物触发 | 宠物状态 / GIF | 本轮动作 |
| --- | --- | --- | --- | --- |
| 电量稳定进入一格低电 | `xiaoxin_battery_snapshot_t.low_edge == true` | `PAOPAO_PET_MOOD_EVENT_BATTERY_LOW` -> `PAOPAO_PET_TRIGGER_SERVICE_TIRED` | `PAOPAO_PET_STATE_TIRED` / `tired.gif` | 补实机连接和测试 |
| WiFi 断开或配网中 | 网络状态从 connected 变为 disconnected/configuring | `PAOPAO_PET_MOOD_EVENT_WIFI_DISCONNECTED` -> `PAOPAO_PET_TRIGGER_SERVICE_ANXIOUS` | `PAOPAO_PET_STATE_ANXIETY` / `anxiety.gif` | 保持现状，纳入 requirement |
| WiFi 恢复 | 网络状态恢复 connected | `PAOPAO_PET_MOOD_EVENT_WIFI_CONNECTED` -> `PAOPAO_PET_TRIGGER_SERVICE_HAPPY` | `PAOPAO_PET_STATE_HAPPY` / `happy.gif` | 保持现状，纳入 requirement |
| 语音错误 | status error / voice recognition failed | `PAOPAO_PET_MOOD_EVENT_VOICE_ERROR` -> `PAOPAO_PET_TRIGGER_SERVICE_FAILING` | `PAOPAO_PET_STATE_FAILING` / `failed.gif` | 保持现状，纳入 requirement |
| 长时间无互动 | 现有 idle timeout | 现有 trigger tick | `PAOPAO_PET_STATE_SLEEPING` / `sleeping.gif` | 保持现状，纳入 requirement |
| 服务端 emotion | 服务端 emotion 字段 | `PAOPAO_PET_MOOD_EVENT_SERVICE_EMOTION` | 对应 emotion GIF | 保持现状，纳入 requirement |
| 本地触摸/拖动/摇晃 | 触摸或 IMU 事件 | local trigger + local mood event | done/jumping/giddy 等现有 GIF | 保持现状，纳入 requirement |

## 电量判定

用户可见规则是：当电池图标稳定变成一格，小芯显示疲惫。

实现上不直接监听 `display_level == 1`，而是复用现有电池状态机的 `low_edge`。原因是 `low_edge` 已经经过电压平滑、状态候选、防抖和边沿判断，能避免电压采样抖动导致 GIF 重复触发。

当前状态机中，`LOW` 大致对应估算电量 `<= 20%`，并在电池供电且状态稳定后触发。`LOW` 状态会将四格显示固定为一格。

## 冲突规则

1. 普通真实处境建议不得打断 `SPEAKING`、`THINKING`、`WAITING/LISTENING`、`FAILING`、`SLEEPING`。
2. 低电 tired 是普通真实处境建议，不强行抢占语音链路。
3. 同一次低电状态只触发一次 tired；重复低电采样不重复播放。
4. 本轮不增加恢复开心，因此低电后回到正常电量只影响系统电量显示和通知，不影响宠物 GIF。

## 实现设计

### 电池到宠物 mood 的连接

在板级 `HandleBatterySnapshot()` 中，保留现有低电通知逻辑：

- `snapshot.low_edge` 时继续调用 `ShowLowBatteryNotification()`。
- 同一边沿增加 `DispatchPetMoodEvent(PAOPAO_PET_MOOD_EVENT_BATTERY_LOW)` 或锁内等价调用。

该调用应复用现有 `paopao_pet_mood` 逻辑。`paopao_pet_mood` 已经将 `BATTERY_LOW` 转成 `SERVICE_TIRED`，并带有低电冷却和 `low_battery` 状态，显示层只负责把真实电池边沿送进去。

### 受保护状态

继续使用 `ShouldDispatchMoodSuggestionLocked()` 作为阻断点。它已经禁止普通 mood suggestion 打断失败、睡眠、等待、思考和说话等状态。本轮不改变这条规则。

### Requirement 同步

实现完成后同步更新：

- `docs/xiaoxin-feature-roadmap.zh-CN.md`
- `docs/visualization/xiaoxin-feature-map.yaml`
- 如有必要，更新 `docs/xiaoxin-pet-emotion-gif-mapping.zh-CN.md`

文档必须明确：

- 一格低电已映射到 `tired.gif`。
- WiFi、语音错误、服务端 emotion、本地交互和长时间无互动继续作为已接入或已部分接入能力记录。
- 充电识别、0 格严重低电、恢复反馈和长期人格系统不属于本轮。

## 测试计划

1. 扩展 `tests/xiaoxin_pet_mood_integration_path_test.py`，移除“电池边沿运行路径被移除”的旧断言，改为断言 `HandleBatterySnapshot()` 在 `snapshot.low_edge` 时调用低电通知和宠物 mood 事件。
2. 保留或增强 `paopao_pet_mood_test.c` 中 `BATTERY_LOW -> SERVICE_TIRED` 的纯逻辑测试。
3. 保留受保护状态测试，确认普通 mood suggestion 不打断 speaking/thinking/listening/failing/sleeping。
4. 运行相关 Python source-path 测试和宠物 mood/trigger C 测试。

## 验收标准

1. 电量稳定进入一格低电后，宠物在可响应状态下播放 `tired.gif`。
2. 同一低电状态不会反复触发疲惫动画。
3. 低电触发不会打断说话、聆听、思考、失败或睡眠。
4. 低电通知仍正常进入通知中心，但不创建普通通知悬浮窗。
5. Requirement 文档同步记录本轮完成范围和明确排除项。
