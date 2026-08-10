# 小芯宠物情绪 GIF 映射说明

> 日期：2026-06-24
> 范围：Waveshare ESP32-S3 Touch LCD 1.46 小芯/泡泡宠物固件
> 目标：说明不同 GIF 如何表达宠物情绪，以及服务端 `llm.emotion` 如何映射到宠物 GIF 动画。

## 1. 设计原则

当前版本采用“少量核心情绪稳定运行”的策略，不追求每个 emotion 字符串一一对应一个动画。服务端、设备状态和本地交互先被归一到少量核心触发事件，再由宠物状态机选择 GIF。

这样做有三个目的：

- 避免 UI 代码里散落大量 `strstr()` 情绪判断。
- 让缺失的动画可以有明确 fallback，不阻塞固件运行。
- 控制运行时资源压力：同一时间只播放一个宠物 GIF，切换状态时释放旧 GIF 控制器。

## 2. 资源约束

目标开发板参数：

- SRAM：512 KB
- ROM：384 KB
- PSRAM：8 MB
- Flash：16 MB

当前 `main/assets/images/*.gif` 共 17 个 GIF，总大小约 1.47 MB。现有 16 MB 分区表为 `factory` app 分区预留 12 MB，为 `assets` 分区预留 3 MB。因此，Flash 侧可以承受少量新增情绪 GIF。

运行期更需要谨慎的是 SRAM/PSRAM：

- 只允许一个宠物 GIF 处于播放状态。
- 切换 GIF 前必须停止并释放旧的 `LvglGif` 控制器。
- 不叠加播放多个情绪特效 GIF。
- 新增 GIF 建议控制在 6 到 10 帧、80 到 150 KB 左右。
- GIF 前景视觉尺寸需要加入 `PaopaoGifVisualLongestForState()`，避免状态切换时宠物忽大忽小。

## 3. 当前 GIF 含义

| GIF | 当前状态 | 表达含义 |
| --- | --- | --- |
| `idle.gif` | `PAOPAO_PET_STATE_IDLE` | 平静、默认待机 |
| `working.gif` | `PAOPAO_PET_STATE_WORKING` | 工作中、任务执行中 |
| `speaking_fixed.gif` | `PAOPAO_PET_STATE_SPEAKING` | 说话中 |
| `thinking.gif` | `PAOPAO_PET_STATE_THINKING` | 思考、困惑、处理中 |
| `waiting.gif` | `PAOPAO_PET_STATE_WAITING` | 等待用户、监听中、需要输入 |
| `done.gif` | `PAOPAO_PET_STATE_DONE` | 完成、确认、轻反馈 |
| `sleeping.gif` | `PAOPAO_PET_STATE_SLEEPING` | 睡眠、休息 |
| `jumping.gif` | `PAOPAO_PET_STATE_JUMPING` | 拖动、玩耍、活跃 |
| `failed.gif` | `PAOPAO_PET_STATE_FAILING` | 系统错误、失败、识别失败 |
| `giddy.gif` | `PAOPAO_PET_STATE_GIDDY` | 被摇晃、眩晕 |
| `review.gif` | `PAOPAO_PET_STATE_REVIEW` | 检查、观察、空闲小动作 |
| `happy.gif` | `PAOPAO_PET_STATE_HAPPY` | 开心、喜欢、积极反馈 |
| `crying.gif` | `PAOPAO_PET_STATE_CRYING` | 哭、伤心、委屈 |
| `anxiety.gif` | `PAOPAO_PET_STATE_ANXIETY` | 焦虑、担心、紧张 |
| `tired.gif` | `PAOPAO_PET_STATE_TIRED` | 疲惫、低电量、没精神 |
| `stamp.gif` | `PAOPAO_PET_STATE_STAMP` | 跺脚、生气、不满、抗议 |
| `waving.gif` | 未接入状态机 | 打招呼、欢迎，后续可接入 |

## 4. 服务端 emotion 归一规则

服务端下发 `type=llm` 且包含 `emotion` 字段时，`Application` 会调用 `display->SetEmotion(emotion)`。Waveshare 1.46 显示层再调用 `paopao_pet_trigger_for_emotion()` 将字符串归一为宠物触发事件。

| 服务端 emotion 关键词 | 触发事件 | 最终 GIF |
| --- | --- | --- |
| `neutral`, `calm`, `relaxed`, `microchip` | `PAOPAO_PET_TRIGGER_SERVICE_NEUTRAL` | 不打断当前状态 |
| `happy`, `laugh`, `loving`, `excited`, `cool` | `PAOPAO_PET_TRIGGER_SERVICE_HAPPY` | `happy.gif` |
| `cry`, `sad`, `unhappy`, `upset`, `lonely` | `PAOPAO_PET_TRIGGER_SERVICE_CRYING` | `crying.gif` |
| `angry`, `annoyed`, `frustrated`, `mad`, `impatient` | `PAOPAO_PET_TRIGGER_SERVICE_ANGRY` | `stamp.gif` |
| `anxious`, `worried`, `nervous`, `scared`, `afraid` | `PAOPAO_PET_TRIGGER_SERVICE_ANXIOUS` | `anxiety.gif` |
| `tired`, `weak`, `low_battery` | `PAOPAO_PET_TRIGGER_SERVICE_TIRED` | `tired.gif` |
| `sleep`, `sleepy`, `sleeping` | `PAOPAO_PET_TRIGGER_SERVICE_SLEEP` | `sleeping.gif` |
| `think`, `confused`, `curious` | `PAOPAO_PET_TRIGGER_SERVICE_THINKING` | `thinking.gif` |
| `error`, `fail`, `shock` | `PAOPAO_PET_TRIGGER_SERVICE_FAILING` | `failed.gif` |
| 未识别或空值 | `PAOPAO_PET_TRIGGER_NONE` | 不切换 |

关键词匹配大小写不敏感，按子串匹配。归一规则的实现位置：

- `main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_emotion.h`
- `main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_emotion.c`

## 5. GIF 触发方式

当前宠物 GIF 的触发入口分为四类：

- 设备/对话状态：由显示层 `SetStatus()` 和 `SetChatMessage()` 触发基础状态。
- 服务端情绪：由 `SetEmotion()` 接收服务端 `llm.emotion` 后触发短反应。
- 本地交互：由屏幕触摸和 IMU 摇晃触发短反应。
- 状态机 tick：由空闲计时触发睡眠或空闲小动作。

| GIF | 触发方式 |
| --- | --- |
| `idle.gif` | 初始状态；`SetStatus(standby)`；短反应结束后回到 idle |
| `waiting.gif` | `SetStatus(listening)`；`PAOPAO_PET_TRIGGER_WAKE` |
| `thinking.gif` | `SetStatus(thinking)`；`SetChatMessage(role=user)`；服务端 emotion 包含 `think`, `confused`, `curious` |
| `speaking_fixed.gif` | `SetStatus(speaking)`；`SetChatMessage(role=assistant)` |
| `done.gif` | 屏幕短点按；显式派发 `PAOPAO_PET_TRIGGER_TASK_DONE` 时作为完成/确认反馈 |
| `sleeping.gif` | idle 安静约 60 秒；屏幕长按切换睡眠；服务端 emotion 包含 `sleep`, `sleepy`, `sleeping` |
| `jumping.gif` | 屏幕横向拖动，左拖或右拖都会触发 |
| `failed.gif` | `SetStatus(error)` 进入错误锁定状态；语音错误经 mood 层短反应；服务端 emotion 包含 `error`, `fail`, `shock` 时作为失败短反应 |
| `giddy.gif` | IMU 检测到设备摇晃；服务端 `giddy` 当前不会触发 |
| `review.gif` | idle 状态保持一段时间后的空闲小动作，首次约 20 秒后触发 |
| `happy.gif` | 服务端 emotion 包含 `happy`, `laugh`, `loving`, `excited`, `cool`；WiFi 恢复 |
| `crying.gif` | 服务端 emotion 包含 `cry`, `sad`, `unhappy`, `upset`, `lonely` |
| `anxiety.gif` | 服务端 emotion 包含 `anxious`, `worried`, `nervous`, `scared`, `afraid`；WiFi 断开或配网中 |
| `tired.gif` | 服务端 emotion 包含 `tired`, `weak`, `low_battery`；本机电量稳定进入一格低电状态时 |
| `stamp.gif` | 服务端 emotion 包含 `angry`, `annoyed`, `frustrated`, `mad`, `impatient`；用于表达跺脚、生气、不满 |
| `working.gif` | 状态和资源已存在，但当前状态机没有实际入口触发 |
| `waving.gif` | 资源已存在，但当前尚未接入状态机 |

说明：当前本机电量映射只覆盖一格低电到 `tired.gif`。0 格严重低电、充电识别和电量恢复反馈暂不作为宠物 GIF 触发条件。

本地触摸还有页面上下文限制：当当前页面不允许宠物交互时，触摸会优先处理卡片页面手势，不一定触发宠物 GIF。IMU 摇晃在触摸进行中或触摸刚结束后的短时间内会被抑制，避免拖动屏幕时误触发 `giddy.gif`。

## 6. 状态优先级

当前状态机保持以下约束：

- `FAILING` 是锁定状态，普通服务端情绪不能覆盖错误状态。
- `GIDDY` 只由本地摇晃触发，服务端 `giddy` 不会触发眩晕动画。
- `neutral` 不清除正在进行的 `listening`、`speaking` 等基础语音状态。
- 睡眠状态下，普通服务端情绪不会打断睡眠；语音状态和本地交互可以唤醒。
- 本地点击、拖动、摇晃仍然优先给即时反馈。

## 7. 动画持续时间

服务端情绪类动画大多是短反应：

- 普通开心、思考：约 1.6 秒。
- 哭泣、焦虑、疲惫、生气、失败：约 2.2 秒。
- 拖动跳跃：约 0.9 秒。
- 反应结束后回到当前基础状态，例如 `idle`、`speaking` 或 `waiting`。

如果后续服务端 emotion 频率变高，建议增加全局冷却：同类 emotion 在 1.5 到 2 秒内不重复触发，错误、低电量、用户本地交互除外。

## 7.1 情绪策略层

P1 情绪系统在 `paopao_pet_trigger` 之前增加了 `paopao_pet_mood` 策略层。这个层不直接选择 GIF 文件，而是把设备状态和用户事件归一为现有的 `paopao_pet_trigger_event_t`，再交给 trigger 层继续做状态机和 GIF 选择。

- 低电量进入：电池供电下出现 low 边缘时，建议 `PAOPAO_PET_TRIGGER_SERVICE_TIRED`，最终显示 `tired.gif`，冷却 30 秒。
- 0 格严重低电、充电识别和电量恢复：当前不作为宠物 GIF 触发条件。
- WiFi 断开或配网中：网络状态从 connected 变为 disconnected/configuring 时，建议 `PAOPAO_PET_TRIGGER_SERVICE_ANXIOUS`，最终显示 `anxiety.gif`，冷却 20 秒。
- WiFi 恢复：网络状态恢复 connected 时，建议 `PAOPAO_PET_TRIGGER_SERVICE_HAPPY`，最终显示 `happy.gif`，冷却 10 秒。
- 语音错误：`SetStatus(error)` 或错误状态文案进入显示层时，建议 `PAOPAO_PET_TRIGGER_SERVICE_FAILING`，最终显示 `failed.gif`，冷却 3 秒。
- 服务端 `emotion`：先经 `paopao_pet_trigger_for_emotion()` 归一，再由 mood 层做 1.8 秒冷却。
- 触摸、拖动、摇晃：继续走本地即时 trigger，同时更新 mood 分数。

BOOT 按键不作为 P1 情绪系统输入。当前实现里它保留为系统、调试、设置入口或后续产品决策使用，不纳入 mood 策略映射。

## 7.2 设备状态表达覆盖

当前代码已经覆盖这些“设备自己表达状态”的路径：

| 设备或业务状态 | 当前表达 | 是否已自动接入 |
| --- | --- | --- |
| 电池供电下进入一格低电量 | `tired.gif`，表现为没精神 | 已接入。由 `HandleBatterySnapshot()` 读取 `snapshot.low_edge`，在 `ShowLowBatteryNotification()` 之后派发 `PAOPAO_PET_MOOD_EVENT_BATTERY_LOW` |
| 电量从低电量恢复 | `happy.gif`，表现为恢复轻反馈 | 未接入。当前不作为宠物 GIF 触发条件 |
| WiFi 断开或配网中 | `anxiety.gif`，表现为焦虑/担心 | 已接入。由系统浮层网络状态变化驱动 |
| WiFi 恢复连接 | `happy.gif`，表现为轻微成功反馈 | 已接入。由网络状态恢复 connected 驱动 |
| 语音识别或错误状态 | `failed.gif`，表现为失败/再想想 | 已接入。`SetStatus(error)` 会进入 mood 语音错误路径 |
| 明确任务完成 | `done.gif`，表现为完成/确认 | 状态机已支持 `PAOPAO_PET_TRIGGER_TASK_DONE`，但当前没有把所有业务任务完成事件自动绑定到这个 trigger |
| 屏幕短点按 | `done.gif`，表现为本地轻反馈 | 已接入。它复用完成动画，但语义是本地点击反馈，不等同于业务任务完成 |

因此，一格低电量已经会用自己的方式表达；网络异常和错误状态已经会用自己的方式表达。任务完成的表达能力已经在状态机中存在，但业务层还需要在真正的完成事件处主动派发 `PAOPAO_PET_TRIGGER_TASK_DONE`。

## 8. 缺失或待接入动画

当前核心情绪已经有可用 GIF，不再强制需要新增：

- 生气：使用 `stamp.gif` 表达跺脚抗议。
- 伤心/哭：使用 `crying.gif`。
- 焦虑：使用 `anxiety.gif`。
- 疲惫：使用 `tired.gif`。

后续可选新增或接入：

| 动画 | 优先级 | 原因 |
| --- | --- | --- |
| `waving.gif` 接入状态机 | P1 | 已有资源，可用于欢迎、唤醒、打招呼 |
| `sad.gif` | P2 | 如果 `crying.gif` 太强烈，可补轻微难过 |
| `confused.gif` | P2 | 如果 `thinking.gif` 不够困惑，可补 |
| `listening.gif` | P2 | 当前监听借用 `waiting.gif`，可更精准 |
| `low_battery.gif` | P3 | 当前低电量可先用 `tired.gif` |

## 9. 测试覆盖

相关测试：

- `tests/paopao_pet_emotion_test.c`：验证服务端 emotion 关键词归一，以及核心情绪触发到宠物状态的映射。
- `tests/paopao_pet_mood_test.c`：验证情绪策略层的分数、冷却、低电量、WiFi、语音错误、本地交互和服务端 emotion 归一后的触发。
- `tests/paopao_pet_trigger_test.c`：验证状态机行为、锁定状态、本地交互和核心情绪反应。
- `tests/paopao_pet_gif_assets_test.c`：验证每个宠物状态对应的 GIF 文件名。
- `tests/paopao_gif_probe_decode_test.c`：采样解码已接入状态机的 GIF，确认尺寸与基础可解码性。

## 10. 修改映射时的步骤

新增或调整情绪 GIF 时，按这个顺序操作：

1. 将 GIF 放入 `main/assets/images/`。
2. 在 `paopao_pet_state.h` 增加状态。
3. 在 `paopao_pet_gif_assets.c` 增加状态到文件名映射。
4. 在 `esp32-s3-touch-lcd-1.46.cc` 增加嵌入符号、二进制资源映射和前景最长边。
5. 在 `paopao_pet_trigger.h/.c` 增加触发事件到状态的规则。
6. 在 `paopao_pet_emotion.c` 增加服务端 emotion 关键词归一。
7. 更新对应测试。
8. 同步更新本文档中的 GIF 含义、emotion 归一规则和 GIF 触发方式。
9. 跑本地单测和 `idf.py build`。
