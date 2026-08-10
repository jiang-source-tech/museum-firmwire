# 小新服务端数据接入与演示控制方案

日期：2026-07-11
范围：小新硬件端、服务端数据后台、宣传视频演示控制

## 1. 背景

小新项目的目标不是只在硬件端做一套静态展示页面，而是让硬件端真实接入服务端数据，并把服务端产生的事件、文本、音频和状态同步到设备上。

当前硬件端已经具备以下基础能力：

- 宠物主页：可以展示宠物动画、状态和基础交互。
- 总览页：已经通过设备级 MQTT Overview retained 快照接入天气、课程和待办数据，并保留 WebSocket 更新兼容路径。
- 通知页：已经具备通知卡片、优先级排序、TTL 过期和清理等基础能力；普通通知不会弹出悬浮窗。
- 语音对话：已经通过 OTA 配置获取服务端协议配置，并通过 MQTT 或 WebSocket 音频链路接收服务端下发的语音数据。

因此，后续方向应是：服务端负责真实业务数据、提醒规则、语音内容和演示控制；硬件端负责展示、播放、交互和本地状态反馈。

## 2. 目标

第一阶段目标：

1. 硬件端可以接收服务端下发的数据，并在总览页和通知页真实显示。
2. 服务端提供一个可视化后台，方便直接修改天气、课程、待办、通知、演示状态等数据。
3. 服务端提供测试命令或调试按钮，可以立即触发某一项功能，方便宣传视频拍摄。
4. 不需要等待真实时间点，例如“不必真的等到上课前 15 分钟”，也可以手动触发上课提醒。
5. 语音提醒复用现有服务端语音链路，不在硬件端另起一套独立音频系统。

第二阶段目标：

1. 服务端接入真实课表、待办、天气等业务来源。
2. 服务端根据真实规则自动触发提醒。
3. 硬件端在保持同一套 UI 和事件接口的前提下，从演示数据平滑切换到真实数据。

## 3. 产品定位

硬件端是用户身边的展示和交互终端。

服务端是业务大脑，负责：

- 管理课程、待办、天气、设备绑定等数据。
- 判断什么时候需要提醒用户。
- 生成通知文案。
- 生成或下发语音播报内容。
- 提供演示控制和调试命令。

硬件端负责：

- 显示总览页数据。
- 显示通知页卡片。
- 保存通知卡片并刷新通知页状态。
- 播放服务端下发的语音。
- 上报设备在线、网络、电量、当前页面等状态。

## 4. 总体架构

```text
服务端管理后台
  ├─ 数据管理：天气、课程、待办、通知
  ├─ 演示控制：一键触发功能展示
  ├─ 设备管理：在线状态、设备绑定、推送目标
  └─ 语音生成：TTS 文案或音频流

服务端推送通道
  ├─ 常驻设备 MQTT：在线状态、唤醒信号、Overview retained 快照
  ├─ WebSocket/对话协议：通知正文、业务事件、页面控制
  └─ 音频通道：复用现有 MQTT/WebSocket OPUS 音频下发

小新硬件端
  ├─ Overview 数据模型
  ├─ Notification 事件模型
  ├─ Notification 通知卡片仓库与分页
  ├─ AudioService 音频播放
  └─ 本地串口/调试命令
```

### 4.1 已实现的设备级 MQTT 协议

OTA 响应中的 `doorbell_mqtt` 配置决定常驻 MQTT 客户端使用的 broker、客户端身份、用户名、密码和三个 topic。正式服务端应为设备 ID `<device_id>` 下发以下精确 topic：

| Topic | 方向 | QoS/retain | 固件行为 |
| --- | --- | --- | --- |
| `device/<device_id>/status` | 设备 → 服务端 | QoS 1，retain=true | 连接成功发布 `online`；异常掉线由 broker 按 LWT 发布 `offline` |
| `device/<device_id>/notification` | 服务端 → 设备 | 订阅 QoS 1；唤醒消息不应 retain | 接受 `{"type":"wake"}`，仅用于让空闲设备建立通知 WebSocket/语音链路 |
| `device/<device_id>/overview` | 服务端 → 设备 | 订阅 QoS 1；服务端发布 retain=true | 接收并校验设备专属 Overview 完整快照，更新天气、课程和待办卡片 |

三个 topic 都由 OTA 配置原样使用；固件不会根据 broker 地址自行拼接或回退到公共 topic。消息只按完整 topic 精确匹配，未知 topic 不解析。MQTT 常驻链路已经不是“仅用于唤醒”：`notification` 保留轻量唤醒职责，`overview` 独立承载服务端数据快照。

Overview 必须是完整 retained 快照，而不是字段增量。当前固件接受的基本结构如下：

```json
{
  "type": "xiaoxin_overview_update",
  "version": 1,
  "device_id": "<device_id>",
  "revision": 42,
  "bound": true,
  "generated_at": "2026-07-11T10:10:00+08:00",
  "weather": {
    "configured": true,
    "available": true,
    "province": "浙江",
    "city": "杭州",
    "date": "2026-07-11",
    "summary": "多云 26C",
    "detail": "湿度 72% · 东风 2级",
    "fetched_at": "2026-07-11T10:00:00+08:00"
  },
  "course": {
    "configured": true,
    "available_today": true,
    "title": "高等数学 10:10",
    "detail": "3教-204 · 还有24分钟"
  },
  "todo": {
    "configured": true,
    "count": 2,
    "detail": "实验报告 · 晚自习"
  }
}
```

校验规则：`version` 必须精确等于 1；`device_id` 必须与本机一致；`revision` 必须是正整数并严格大于本次启动中最后接受的 revision；`bound` 必须是布尔值；三个卡片对象及其字段必须完整、类型正确且满足 UTF-8/长度限制。超过 2048 字节、含 NUL、尾随垃圾、错误设备 ID、重复或倒退 revision 的消息全部丢弃。MQTT Overview 不允许携带 `notifications` 字段，避免绕过现有 WebSocket 通知中心。设备重启后内存中的 revision 从 0 重新开始，因此 broker 的最新 retained 快照会再次生效。

### 4.2 网络位置心跳

激活完成时，固件从 OTA 检查 URL 的 origin 推导心跳地址。例如：

```text
http://host:8003/xiaoxin/ota/
  -> http://host:8003/api/xiaoxin/device/location-heartbeat
```

固件后台发送 `POST`，body 固定为 `{}`，并携带：

- `Content-Type: application/json`
- `Device-Id: <device_id>`
- `Device-Username: <doorbell_mqtt.username>`
- `Authorization: Bearer <doorbell_mqtt.password>`

服务端根据 TCP peer IP 判断设备公网/网络位置，body 不包含 `public_ip`、租户或其他业务字段。启动激活成功后发送一次；之后只有真实断网再重连才发送。重复连接事件会被消抖，在途请求保持 single-flight。HTTP 和 HTTPS 都支持；HTTPS 使用系统 CA bundle，且禁止自动重定向，避免凭据被转发到其他 origin。凭据、Authorization 和请求 body 不写日志。心跳失败不会重启设备、打开 WebSocket、唤醒通知流程或影响语音。

## 5. 数据类型

### 5.1 总览页数据

服务端应能下发总览页所需摘要数据。

MQTT Overview 使用 4.1 节的版本化完整快照。以下字段是当前固件实际接受的有绑定示例：

```json
{
  "type": "xiaoxin_overview_update",
  "version": 1,
  "device_id": "<device_id>",
  "revision": 42,
  "bound": true,
  "generated_at": "2026-07-11T10:10:00+08:00",
  "weather": {
    "configured": true,
    "available": true,
    "province": "浙江",
    "city": "杭州",
    "date": "2026-07-11",
    "summary": "多云 26C",
    "detail": "湿度 72% · 东风 2级",
    "fetched_at": "2026-07-11T10:00:00+08:00"
  },
  "course": {
    "configured": true,
    "available_today": true,
    "title": "高等数学 10:10",
    "detail": "3教-204 · 还有24分钟"
  },
  "todo": {
    "configured": true,
    "count": 2,
    "detail": "实验报告 · 晚自习"
  }
}
```

硬件端通过全部校验后更新 `xiaoxin_overview_state_t` 并刷新总览页；无效或陈旧快照不产生 UI、通知、WebSocket 或语音副作用。

### 5.2 通知事件

服务端应能主动下发通知事件。

建议字段：

```json
{
  "type": "xiaoxin_event",
  "event": "course_reminder",
  "id": "course-reminder-math-20260627-1010",
  "title": "上课提醒",
  "body": "15分钟后 高等数学 @ 3教204",
  "tag": "课程",
  "priority": 1,
  "ttlMs": 0,
  "speak": true,
  "speakText": "小新提醒你，十五分钟后有高等数学课，地点在三教二零四。"
}
```

硬件端收到后：

1. 将事件转换成通知卡片。
2. 通知页出现对应卡片。
3. 不打断当前宠物界面；如果 `speak=true`，再进入独立的语音播报流程。

### 5.3 语音播报

语音提醒应复用现有语音对话音频链路。

推荐流程：

```text
服务端下发 xiaoxin_event，speak=true
        ↓
硬件端显示通知
        ↓
硬件端打开或保持音频通道
        ↓
服务端发送 tts start
        ↓
服务端发送 OPUS 音频包
        ↓
硬件端 audio_service 播放
        ↓
服务端发送 tts stop
```

如果短期内服务端还不能主动生成完整音频流，可以先支持两种演示方式：

- 服务端发送 `speakText`，由现有对话/TTS 服务生成音频。
- 硬件端播放本地预置提示音，作为宣传视频临时方案。

正式产品应以服务端生成或控制语音内容为准。

## 6. 服务端可视化后台

后台需要服务宣传拍摄和真实使用两个目标。

### 6.1 数据管理页

建议提供以下区域：

- 设备选择：选择要控制的小新设备。
- 总览数据：编辑天气、课程、待办摘要。
- 通知数据：创建、修改、删除通知事件。
- 课程提醒：编辑课程名称、教室、开始时间、提醒提前量。
- 语音文案：编辑提醒时要播报的文本。

### 6.2 演示控制页

演示控制页用于宣传视频拍摄，不依赖真实时间。

建议按钮：

- 显示总览页演示数据。
- 触发上课提醒。
- 触发天气更新。
- 触发待办提醒。
- 触发 OTA 更新通知。
- 触发 WiFi 异常通知。
- 触发语音识别失败提示。
- 清空通知。
- 回到宠物主页。
- 播放提醒语音。

每个按钮都应立即向硬件端发送一条明确命令，方便拍摄时精准控制画面。

### 6.3 设备状态页

建议展示：

- 设备是否在线。
- 当前连接协议：MQTT 或 WebSocket。
- 当前音频通道是否打开。
- 当前页面：Home、Notifications、Overview。
- 最近一次收到的服务端事件。
- 最近一次硬件端上报状态。
- 网络状态、电量状态、固件版本。

## 7. 演示命令设计

为了拍宣传视频，需要有命令可以直接测试某一项功能。

这些命令可以同时存在于两个入口：

- 服务端后台按钮。
- 硬件端串口调试命令。

### 7.1 服务端演示命令

建议定义：

| 命令 | 用途 |
| --- | --- |
| `demo.overview` | 下发完整总览页演示数据，并让设备切到总览页 |
| `demo.courseReminder` | 立即触发上课提醒通知和语音 |
| `demo.weather` | 更新天气卡片 |
| `demo.todo` | 更新今日待办卡片 |
| `demo.notifications` | 注入一组通知卡片并打开通知页 |
| `demo.ota` | 触发 OTA 更新通知 |
| `demo.networkWarning` | 触发 WiFi 异常通知 |
| `demo.voiceError` | 触发语音识别失败通知 |
| `demo.speak` | 只测试服务端语音播报 |
| `demo.clear` | 清空演示数据和通知 |
| `demo.home` | 回到宠物主页 |

### 7.2 硬件端串口命令

建议硬件端保留一组本地命令，用于服务端不可用或拍摄现场网络不稳定时兜底。

| 命令 | 用途 |
| --- | --- |
| `demo_overview` | 写入本地演示总览数据并打开总览页 |
| `demo_notify` | 写入本地演示通知并打开通知页 |
| `demo_course` | 触发本地课程提醒卡片 |
| `demo_voice` | 播放本地提示音或请求服务端播报 |
| `demo_clear` | 清空本地演示通知 |
| `notify_test` | 保留现有通知测试命令 |

串口命令不是正式业务入口，只用于开发、测试和宣传拍摄兜底。

## 8. 服务端到硬件端消息建议

### 8.1 总览更新

通过 `device/<device_id>/overview` 发布 4.1 节定义的完整 JSON，并设置 QoS 1、retain=true。服务端每次内容变化都递增该设备的 `revision`；不得复用 revision，也不得向多个设备广播同一个 `device_id` payload。

### 8.2 通知事件

```json
{
  "type": "xiaoxin_event",
  "event": "course_reminder",
  "id": "course-reminder-demo",
  "title": "上课提醒",
  "body": "15分钟后 高等数学 @ 3教204",
  "tag": "课程",
  "priority": 1,
  "ttlMs": 0,
  "speak": true,
  "speakText": "小新提醒你，十五分钟后有高等数学课，地点在三教二零四。"
}
```

### 8.3 页面控制

```json
{
  "type": "xiaoxin_command",
  "command": "open_page",
  "page": "overview"
}
```

可选页面：

- `home`
- `overview`
- `notifications`

### 8.4 清空通知

```json
{
  "type": "xiaoxin_command",
  "command": "clear_notifications"
}
```

### 8.5 演示命令

```json
{
  "type": "xiaoxin_demo",
  "command": "course_reminder"
}
```

硬件端可以把 `xiaoxin_demo` 当成开发和拍摄专用命令，不与正式业务事件混在一起。

## 9. 硬件端接入要求

硬件端当前已经实现以下接入边界，后续修改必须保持兼容：

1. WebSocket/对话协议在 `OnIncomingJson` 中识别小新业务消息：
   - `xiaoxin_overview_update`
   - `xiaoxin_event`

   本文第 7、8 节的 `xiaoxin_command` 和 `xiaoxin_demo` 仍是演示控制建议，不属于当前固件已经验证的协议入口。

2. MQTT 常驻客户端按 OTA 配置订阅 notification 和 overview 精确 topic，并将通过校验的 Overview 快照写入现有总览模型。

3. 服务端数据缓存包括：
   - 天气摘要。
   - 下一节课摘要。
   - 今日待办摘要。
   - 最近一次服务端更新时间。

4. `BuildOverviewState()` 不再只返回空数据：
   - 有服务端数据时显示服务端数据。
   - 无服务端数据时显示未同步或未配置状态。
   - 串口 demo 数据可以作为本地 fallback。

5. 通知事件统一走现有通知中心：
   - 服务端课程提醒映射为 `XIAOXIN_NOTIFICATION_EVENT_REMINDER`。
   - OTA 映射为 `XIAOXIN_NOTIFICATION_EVENT_OTA_UPDATE`。
   - WiFi 异常映射为网络类通知。
   - 语音失败映射为语音识别失败通知。

6. 语音播报复用现有音频链路：
   - 不在通知模块中直接处理音频解码。
   - 通知模块只表达“需要播报”。
   - 播报由服务端和现有 `audio_service_` 链路完成。

## 10. 服务端接入要求

服务端需要补齐以下能力：

1. 设备管理：
   - 设备注册。
   - 设备在线状态。
   - 设备绑定用户。
   - 向指定设备推送消息。

2. 数据管理：
   - 课程表。
   - 今日待办。
   - 天气摘要。
   - 设备演示数据。

3. 事件调度：
   - 根据时间自动触发课程提醒。
   - 同一节课同一次提醒不能重复触发。
   - 支持手动立即触发。

4. 语音能力：
   - 根据提醒生成 `speakText`。
   - 调用 TTS 生成音频。
   - 通过现有音频协议向硬件端发送音频。

5. 可视化后台：
   - 修改总览页数据。
   - 创建通知事件。
   - 一键触发演示命令。
   - 查看设备状态和最近事件。

## 11. 宣传视频拍摄流程建议

拍摄时不依赖真实时间，全部用后台按钮或串口命令触发。

建议流程：

1. 设备显示宠物主页。
2. 后台点击“显示总览页演示数据”。
3. 设备切到总览页，显示天气、课程、待办、设备状态。
4. 后台点击“触发上课提醒”。
5. 设备将上课提醒保存为通知卡片，不遮挡当前宠物界面。
6. 服务端发送语音，设备播报上课提醒。
7. 用户滑入通知页，查看课程提醒卡片。
8. 后台点击“触发 OTA 更新”或“触发待办提醒”，展示多通知能力。
9. 手动滑动或清理通知，拍摄交互效果。
10. 后台点击“回到宠物主页”，设备回到宠物动画。

## 12. 分阶段实施

### 阶段一：拍摄可用

目标：马上满足宣传视频功能展示。

范围：

- 服务端或本地串口可以触发总览演示数据。
- 服务端或本地串口可以触发通知卡片。
- 课程提醒可以立即触发。
- 可选支持本地提示音或服务端 TTS 播报。

验收：

- 不等真实时间也能展示上课提醒。
- 总览页不再是空数据。
- 通知页能展示多条真实风格通知。
- 拍摄现场即使服务端不稳定，也能用串口命令兜底。

### 阶段二：服务端后台可控

目标：用可视化后台替代手工串口操作。

范围：

- 后台可以编辑总览页数据。
- 后台可以点击按钮触发通知。
- 后台可以触发语音播报。
- 后台可以查看设备在线状态。

验收：

- 拍摄时主要通过后台按钮完成演示。
- 数据修改后硬件端能实时或近实时刷新。

### 阶段三：真实业务接入

目标：从演示数据切换到真实数据。

范围：

- 接入真实课表。
- 接入天气服务。
- 接入待办系统。
- 服务端自动调度提醒。
- 硬件端缓存最近一次数据，断网时显示最近状态或离线提示。

验收：

- 到达真实提醒时间时，服务端自动触发通知和语音。
- 人工演示命令仍然可用，但不影响正式业务数据。

## 13. 关键原则

1. 演示数据和真实数据使用同一套硬件 UI。
2. 服务端事件和本地串口命令都应转成同一套硬件端模型。
3. 音频播报复用现有服务端语音链路。
4. 硬件端不负责复杂业务判断，只负责展示、播放和交互。
5. 拍摄演示命令必须可以立即触发，不依赖真实时间。
6. 后台按钮是宣传拍摄的主控台，串口命令是兜底方案。

## 14. 下一步建议

建议后续按以下顺序联调：

1. 服务端 OTA 为每台设备下发可用的 `doorbell_mqtt` 凭据以及三个精确设备 topic。
2. 服务端向 Overview topic 发布 QoS 1 retained 完整快照，并为每台设备维护严格递增 revision。
3. 保持 notification topic 只承担 `{"type":"wake"}` 信号；通知正文、通知中心事件和语音继续走现有 WebSocket/对话链路。
4. 在真实设备上刷入本次构建产物，观察 status retained、Overview retained、断网重连心跳和 UI 更新。本文当前只记录自动化测试、host 模型测试和固件编译结果，不代表已经完成刷机或真机网络验证。

## 15. 2026-07-11 SUBACK 最终修复报告

问题：ESP-IDF 的 `MQTT_EVENT_SUBSCRIBED` 既可能携带成功的 granted QoS，也可能通过 `error_handle` 或返回码表示订阅拒绝。旧实现只按 MID 记录成功，因此 Broker 拒绝订阅时会误报 `subscription confirmed`，并清除待确认 MID。

修复：固件使用同一生产 helper 校验 `error_handle`、`data_len` 和全部 granted QoS。只有非空、协议合法且无拒绝码的 SUBACK 才按 MID 确认 notification/overview；`MQTT_ERROR_TYPE_SUBSCRIBE_FAILED`、空数据、任一 `0x80` 或非法大于 2 的返回码均输出固定脱敏错误，保留待确认状态，并触发一次性安全重连。重连先请求断开，再在 `MQTT_EVENT_DISCONNECTED` 中调用并检查 `esp_mqtt_client_reconnect()`，由下一次 `OnConnected()` 重新提交订阅；重复失败不会重复请求断开，析构/停止期间不会重连。

验证证据：

- RED：新增 host/source 测试最初 3 项失败，原因分别为生产 SUBACK helper、`OnSubscribed()` 和安全重连入口尚不存在。
- GREEN：SUBACK focused 测试 `3 passed`；相邻 focused 回归 `30 passed`。
- 全量：`python -m pytest tests -q`，`276 passed`。
- Host：SUBACK、MQTT 分片重组、Overview 权威状态、位置心跳状态、Overview model、card pager 全部退出码 0。
- 固件：ESP-IDF 5.5.4 `idf.py build` 退出码 0，生成 `build/ai_pet.bin`。
- 本轮未刷机，未声称完成真机 Broker 拒绝/恢复验收。
