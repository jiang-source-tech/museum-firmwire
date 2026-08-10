# 小新宠物行为导演设计

> 日期：2026-06-28
> 范围：Waveshare ESP32-S3 Touch LCD 1.46 小新 / Paopao 宠物 GIF 表现系统
> 目标：让宠物动画从“事件触发式切换”升级为更像真实小宠物的表演节奏。

## 背景

当前 1.46 固件已经有较多宠物 GIF 资源，例如 `idle.gif`、`thinking.gif`、`speaking_fixed.gif`、`happy.gif`、`review.gif`、`tired.gif`、`sleeping.gif`、`crying.gif`、`anxiety.gif`、`stamp.gif` 等。

实际体验中，用户常看到的动画仍集中在 `idle`、`thinking`、`speaking`、少量本地交互反馈。服务端通过 WebSocket 返回的 `{"type":"llm","emotion":"happy"}` 会进入 `SetEmotion()`，但在宠物处于 `SPEAKING / WAITING / THINKING` 等保护态时，当前 mood 建议不会真正派发表情动画。随后 TTS stop 把设备带回 idle，用户看到的仍是 `idle.gif`。

这说明问题不只是某个 GIF 没触发，而是表现系统太机械：事件来了就切一个动画，反应结束就回 idle，缺少过渡、余韵、轻微随机和短期记忆。

## 设计目标

1. 服务端 emotion 不打断正在说话的动画，但能在回答结束后被用户看到。
2. idle 不再长期只显示同一个 `idle.gif`，而是低频出现自然的小动作。
3. 动画选择避免连续重复，让宠物显得有记忆。
4. 强烈动画只在明确事件下出现，避免宠物无缘无故哭、失败或生气。
5. 第一版保持小而可控，不引入复杂 AI 行为树或长期人格系统。

## 非目标

1. 不改变语音连接、TTS/STT、协议或音频状态机。
2. 不让服务端 emotion 立即打断 `speaking_fixed.gif`。
3. 不新增 GIF 资源。
4. 不重写整个 Paopao 显示类。
5. 不把随机动画做成完全不可预测；随机必须受冷却、场景和强度约束。

## 核心方案

新增一个轻量的“宠物行为导演”层，建议命名为 `paopao_pet_behavior` 或等价模块。它不直接渲染 GIF，只负责在合适时机给现有 `paopao_pet_trigger` 派发触发事件。

现有链路：

```text
事件 -> paopao_pet_trigger -> pet state -> GIF
```

调整后链路：

```text
事件 -> behavior director -> paopao_pet_trigger -> pet state -> GIF
```

行为导演负责三件事：

1. 缓存服务端 emotion，在回答结束后播放情绪收尾。
2. idle 时按低频权重选择生活小动作。
3. 维护短期记忆和冷却时间，避免机械重复。

## 状态输入

行为导演接收以下输入：

| 输入 | 来源 | 用途 |
| --- | --- | --- |
| `VOICE_LISTENING` | `SetStatus()` 或设备状态 | 进入等待 / 聆听表现 |
| `VOICE_THINKING` | 用户消息、状态文本 | 进入思考表现 |
| `VOICE_SPEAKING` | TTS start、助手消息 | 进入说话表现 |
| `VOICE_IDLE` | TTS stop 后设备回 idle | 尝试播放情绪收尾 |
| `SERVICE_EMOTION` | `SetEmotion(emotion)` | 缓存或立即播放服务端情绪 |
| `LOCAL_TAP / HOLD / DRAG / SHAKE` | 触摸、IMU | 保留即时反馈 |
| `TICK` | 渲染循环 tick | 驱动 idle 生活小动作和睡眠 |
| `NETWORK / BATTERY / ERROR` | mood 事件 | 触发明确的系统情绪 |

## 情绪收尾

当服务端返回 `emotion` 时：

1. 先通过现有 `paopao_pet_trigger_for_emotion()` 映射为服务端 trigger。
2. 如果结果是 `NONE` 或 `SERVICE_NEUTRAL`，不缓存。
3. 如果当前 base state 是 `SPEAKING / WAITING / THINKING`，缓存为 `pending_service_trigger`。
4. 如果当前 base state 是 `IDLE`，且不在冷却中，可以直接播放对应反应。
5. 如果说话期间连续收到多个 emotion，只保留最后一个。

当进入 idle 时：

1. 如果存在 `pending_service_trigger`，优先播放它。
2. 播放后清空 pending。
3. 播放期间不触发 idle 随机小动作。
4. 播完后回到底层 idle。

示例：

```text
用户说话 -> thinking.gif
助手开始回复 -> speaking_fixed.gif
服务端返回 emotion=happy -> 缓存 SERVICE_HAPPY
TTS stop -> 进入 idle -> 播放 happy.gif
反应结束 -> idle.gif
```

## Idle 生活小动作

idle 不再只依赖固定 `review.gif`。行为导演维护下一次 idle 小动作时间，建议第一版为 12 到 28 秒之间的伪随机间隔。

候选动画池：

| 动画 | 场景 | 权重建议 |
| --- | --- | --- |
| `review.gif` | 打量、观察 | 高 |
| `thinking.gif` | 走神、好奇 | 中 |
| `happy.gif` | 轻微开心 | 中 |
| `waiting.gif` | 看向用户、等待互动 | 中低 |
| `tired.gif` | 活力低或久未互动 | 低到中 |

强烈动画不进入普通 idle 池：

`failed.gif`、`crying.gif`、`anxiety.gif`、`stamp.gif` 只由服务端情绪、错误、网络、电量等明确事件触发。

长时间无互动时：

1. 约 45 秒后提高 `tired.gif` 权重。
2. 约 90 秒后允许进入 `sleeping.gif`。
3. 本地点击、拖动、摇动、语音唤醒都会刷新互动时间并唤醒。

## 短期记忆与冷却

行为导演维护以下字段：

```text
pending_service_trigger
last_animation_state
last_strong_reaction_ms
last_idle_variant_ms
next_idle_variant_ms
last_interaction_ms
mood_score
energy_score
```

第一版只需要轻量使用：

- `last_animation_state`：避免连续两次选同一个 idle 小动作。
- `last_strong_reaction_ms`：限制哭、焦虑、生气、失败等强反应频率。
- `next_idle_variant_ms`：控制 idle 小动作节奏。
- `last_interaction_ms`：决定 tired / sleeping 倾向。
- `mood_score` 和 `energy_score` 可以复用现有 `paopao_pet_mood_context_t`，不必另起复杂系统。

## 动画优先级

从高到低：

1. 系统锁定态：`FAILING`、显式 `SLEEPING`。
2. 本地直接交互：tap、hold、drag、shake。
3. 主语音状态：listening、thinking、speaking。
4. 回答结束后的 pending service emotion。
5. 系统 mood 建议：网络、电量、语音错误。
6. idle 生活小动作。
7. 默认 idle。

这保证宠物不会在说话时突然切走，但说完后仍然能表达服务端情绪。

## 接入点

建议改动集中在 1.46 Paopao 相关模块：

1. `paopao_pet_behavior.h/.c`
   - 新增行为导演上下文、事件输入、tick 和选择逻辑。
2. `esp32-s3-touch-lcd-1.46.cc`
   - `SetEmotion()` 改为把服务端 emotion 交给行为导演。
   - `SetStatus()` / `SetChatMessage()` 在语音阶段变化时通知行为导演。
   - `RunRenderLoop()` 每次 tick 调用行为导演，必要时派发 trigger。
3. `paopao_pet_trigger.c`
   - 保留现有职责；只在必要时扩展 helper，不把全部导演逻辑塞进去。
4. `paopao_pet_mood.c`
   - 保留分数和系统事件建议；强反应冷却可以由行为导演统一收口。

## 测试计划

新增或扩展 C 单元测试：

1. `emotion=happy` 在 speaking 期间被缓存，不立即覆盖 speaking。
2. TTS stop / idle 后播放缓存的 `SERVICE_HAPPY`。
3. 连续多个服务端 emotion 只保留最后一个。
4. `neutral` 不进入 pending。
5. idle 小动作不会连续选择同一个状态。
6. 强烈动画不会由普通 idle tick 触发。
7. 长时间无互动后允许 tired / sleeping。
8. 本地 tap / drag / shake 仍然即时反馈，并刷新 idle 计时。

保留现有路径测试：

- `paopao_pet_trigger_test.c`
- `paopao_pet_mood_test.c`
- `paopao_pet_emotion_test.c`
- `xiaoxin_pet_mood_integration_path_test.py`

## 第一版验收标准

1. 对话中收到 `{"type":"llm","emotion":"happy"}` 后，回答结束能看到 `happy.gif`，随后回到 `idle.gif`。
2. 普通 idle 等待 1 分钟内能看到至少 2 种不同的轻量小动作。
3. 说话期间不被 `happy.gif` 或其他服务端 emotion 打断。
4. 强烈动画不会在无明确事件时随机出现。
5. 现有本地点击、拖动、摇动反馈仍然可见。

## 风险

1. 随机太频繁会让宠物显得吵；第一版要保守。
2. 如果行为导演和现有 mood/trigger 职责混在一起，会让状态更难理解；必须保持导演只做调度，不做渲染。
3. 如果 pending emotion 只在 idle 时消费，需要确保所有 TTS stop 路径都会通知 idle。
4. 如果强反应冷却过长，用户可能觉得服务端 emotion 又没响应；冷却只应限制重复强反应，不限制回答结束后的单次收尾。

## 推荐实施顺序

1. 增加行为导演上下文和纯 C 单元测试。
2. 接入 `SetEmotion()` 的 pending service emotion。
3. 在 idle 入口消费 pending emotion。
4. 增加 idle 小动作选择和冷却。
5. 调整权重和时间常量，实机观察后再微调。
