# 小芯真实通知中心触发说明

日期：2026-06-19
分支：codex/xiaoxin-real-notifications

## 本轮修订结论

通知中心的目标不是做聊天记录入口，而是承载用户需要被提醒、需要处理、或者能反映设备状态的事件。

- 普通聊天回复不进入通知中心，避免和对话页重复，也避免制造噪声。
- 上课提醒是第一类核心业务通知：提前一段时间告诉用户下一节课是什么、在哪里上课。
- 真实课表数据源后续再接入，本阶段先把课程提醒定义成可注入事件，方便后续从服务器、小程序、本地课表或其他来源接入。
- 电池电量目前来自电压估算，不能准确表达剩余百分比，通知文案应使用“电量偏低，请尽快充电”这类状态提示，不展示精确百分比。

## 通知中心定位

通知中心收纳三类信息：

- 设备状态：低电量、WiFi 断开、配网中、OTA 更新。
- 日程提醒：上课提醒、定时提醒。
- 失败提示：语音识别失败等需要用户知道的短期异常。

通知中心不收纳：

- 普通聊天回复。
- 普通对话内容。
- 音量变化、临时 toast、页面内即时反馈。

## 数据入口

通知中心通过事件注入接口接收外部事件：

```c
void xiaoxin_card_pager_notification_upsert_event(
    xiaoxin_card_pager_handle_t handle,
    const xiaoxin_notification_event_t* event);

void xiaoxin_card_pager_notification_remove_event(
    xiaoxin_card_pager_handle_t handle,
    xiaoxin_notification_event_type_t type);
```

`upsert` 表示同类型事件重复到达时更新已有卡片，而不是追加重复卡片。这样 WiFi、低电量、课程提醒等事件源可以反复上报状态，通知页仍然保持干净。

## 通知类型和触发方式

### 电量偏低

事件类型：`XIAOXIN_NOTIFICATION_EVENT_LOW_BATTERY`

当前触发方式：

- 系统通过 `NotificationBatteryLevelPercent()` 获得一个百分比。
- 低于或等于阈值时注入低电量通知。
- 恢复到阈值以上时移除低电量通知。

注意：当前百分比是基于电压估算的粗略值，不能准确代表真实剩余电量。通知文案不应该写“剩余 18%”这类精确描述，应写成：

```text
电量偏低，请尽快充电
```

后续如果硬件接入电量计芯片，或完成电池曲线校准，再考虑展示更可信的百分比。

### WiFi 断开或配网中

事件类型：`XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED`

触发方式：

- WiFi 断开时注入通知。
- 进入配网状态时注入或更新通知。
- WiFi 连接成功后移除通知。

建议文案：

```text
WiFi 已断开，请检查网络
```

或：

```text
正在等待配网
```

### OTA 更新

事件类型：`XIAOXIN_NOTIFICATION_EVENT_OTA_UPDATE`

当前为预留类型。后续 OTA 模块接入后，可在以下时机注入通知：

- 检测到可用更新。
- 下载中或安装中。
- 更新失败，需要用户重试。

OTA 完成或用户确认后移除通知。

### 语音识别失败

事件类型：`XIAOXIN_NOTIFICATION_EVENT_VOICE_RECOGNITION_FAILED`

触发方式：

- ASR 模块返回识别失败、超时或网络异常时注入通知。
- 下次识别成功后可以移除通知。

这类通知优先级应低于课程提醒和设备关键状态。它适合提醒“刚才没听清”，不适合长期占据通知页。

### 上课提醒

事件类型：`XIAOXIN_NOTIFICATION_EVENT_REMINDER`

这是当前最重要的业务通知。真实课表数据后续接入，本阶段先使用可注入的提醒事件模拟。

第一版事件示例：

```c
xiaoxin_notification_event_t event = {
    .type = XIAOXIN_NOTIFICATION_EVENT_REMINDER,
    .priority = 1,
    .title = "上课提醒",
    .body = "15 分钟后 高等数学 @ 3教 204",
    .tag = "课程",
    .ttl_ms = 0,
};

xiaoxin_card_pager_notification_upsert_event(card_pager, &event);
```

建议触发逻辑：

1. 上层提供下一节课记录：课程名、教室、开始时间、提前提醒分钟数。
2. 本地调度器定期检查当前时间。
3. 当 `开始时间 - 当前时间 <= 提前提醒分钟数` 时，注入上课提醒。
4. 同一节课同一次上课只提醒一次，避免重复刷屏。

建议课表记录结构：

```c
typedef struct {
    const char* course_id;
    const char* course_name;
    const char* classroom;
    int64_t starts_at_unix_ms;
    uint16_t remind_before_min;
} xiaoxin_course_reminder_t;
```

真实数据源可以后续来自服务器、小程序、本地配置文件或校园课表接口。通知中心只关心最终被转换出来的 `xiaoxin_notification_event_t`，因此后续更换数据源不需要改通知页渲染逻辑。

### 聊天回复

普通聊天回复不再作为通知中心的产品方向。

原因：

- 用户已经在对话页能看到回复。
- 通知页重复展示聊天内容会降低信息密度。
- 真正需要提醒用户的是课程、设备异常、网络状态等离开对话页后仍然重要的信息。

后续代码整理时，应移除 `SetChatMessage("assistant", ...)` 自动注入通知的逻辑，并停止使用 `XIAOXIN_NOTIFICATION_EVENT_CHAT_REPLY`。如果枚举短期保留，应标记为兼容字段，不再实际渲染为通知卡片。

## 优先级建议

| 通知 | 优先级 | 说明 |
| --- | --- | --- |
| 上课提醒 | 1 | 最重要的业务提醒，应排在最前 |
| 电量偏低 | 2 | 影响设备继续使用 |
| WiFi 断开或配网中 | 3 | 影响联网能力 |
| OTA 更新 | 4 | 需要用户知道，但通常不紧急 |
| 语音识别失败 | 5 | 短期异常，可低优先级展示 |
| 聊天回复 | 不进入通知中心 | 不再作为通知中心事件 |

## 第一阶段接入方案

本阶段先完成“接口和模拟数据可用”，不等待真实课表源。

建议步骤：

1. 移除聊天回复自动通知。
2. 增加课程提醒注入示例或 mock provider。
3. 将低电量文案改为不展示精确百分比。
4. 保留 `xiaoxin_card_pager_notification_upsert_event()` 作为统一入口。
5. 后续真实课表数据接入时，只需要把课表记录转换为提醒事件。

## 测试和验收

后续代码调整建议覆盖以下场景：

- assistant 聊天回复不会生成通知卡片。
- 注入课程提醒事件后，通知页出现“上课提醒”卡片。
- 同一节课同一开始时间不会重复生成多张卡片。
- 低电量通知文案不包含精确百分比。
- WiFi 断开、恢复连接时，通知卡片能正确出现和移除。

## 后续代码调整清单

- 移除聊天回复到通知中心的自动映射。
- 增加课程提醒事件 helper 或 mock provider。
- 将低电量通知文案改为“电量偏低，请尽快充电”。
- 为课程提醒补充单元测试。
- 等真实课表来源确定后，再实现课表同步、提醒去重和本地调度。
