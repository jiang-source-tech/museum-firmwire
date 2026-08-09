# 小新 Boot: Audio 阶段开机动画保持设计

## 背景

小新 1.46 固件在 `Application::Initialize()` 中按同步顺序完成 UI、音频和网络启动。当前音频阶段会把屏幕状态设置为 `Boot: Audio`：

```cpp
display->SetStatus("Boot: Audio");
display->UpdateStatusBar(true);
auto codec = board.GetAudioCodec();
audio_service_.Initialize(codec);
audio_service_.Start();
```

实机现象是：USB 供电时 `Boot: Audio` 很快被后续 Wi-Fi 状态覆盖，用户通常看不到；外接锂电池供电时音频初始化阶段更慢，`Boot: Audio` 会短暂可见，然后系统仍能正常工作。

这不是故障码，而是内部启动进度直接暴露到了用户界面。产品体验上，外接电池慢启动时应继续显示开机动画，而不是显示工程状态文本。

## 目标

1. UI ready 后继续显示开机动画，覆盖 `Boot: Audio` 阶段。
2. 音频初始化完成后、Wi-Fi 启动状态显示前结束开机动画。
3. 不再把 `Boot: Audio` 作为普通用户可见状态显示。
4. 保留音频启动诊断信息，便于串口日志和 BootDiagnostics 排查问题。
5. 不改变音频初始化、Wi-Fi 启动、协议加载和状态机的业务顺序。

## 非目标

1. 不把 Wi-Fi 连接成功作为开机动画结束条件。
2. 不重构音频服务为异步初始化。
3. 不改变 `audio_service_.Initialize()` 或 `audio_service_.Start()` 的语义。
4. 不重新设计整套启动页视觉。
5. 不处理低电、电源检测或电池供电稳定性问题。

## 当前启动顺序

当前 `Application::Initialize()` 的关键顺序是：

1. `display->SetupUI()`
2. `display->SetStatus("Boot: UI")`
3. `display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str())`
4. `BootDiagnosticsMark("app_audio_start")`
5. `display->SetStatus("Boot: Audio")`
6. `audio_service_.Initialize(codec)`
7. `audio_service_.Start()`
8. 注册音频回调和状态机监听
9. `BootDiagnosticsMark("app_network_start")`
10. `display->SetStatus("Boot: Wi-Fi")`
11. `display->SetStatus(Lang::Strings::SCANNING_WIFI)`
12. `board.StartNetwork()`

因此 `Boot: Audio` 阶段有明确结束点：`audio_service_.Start()` 返回之后。用户期望的动画结束点就是这个点之后、Wi-Fi 状态显示之前。

## 推荐方案

采用“启动动画覆盖 Audio 阶段”的显式生命周期：

1. `SetupUI()` 完成后，开机动画保持可见。
2. 进入音频初始化时，不调用用户可见的 `SetStatus("Boot: Audio")`。
3. `BootDiagnosticsMark("app_audio_start")` 继续记录音频启动阶段。
4. `audio_service_.Initialize(codec)` 和 `audio_service_.Start()` 同步完成后，调用显示层接口结束开机动画。
5. 结束动画后再显示 `Boot: Wi-Fi` 和扫描 Wi-Fi 状态。

目标时序：

```text
SetupUI
↓
开机动画保持显示
↓
Audio 初始化开始
↓
Audio 初始化完成
↓
结束开机动画
↓
Boot: Wi-Fi / 扫描 Wi-Fi
↓
StartNetwork
```

## 接口设计

项目已经有显示层启动动画退场接口：

```cpp
virtual void CompleteBootSplash();
```

`Display` 默认实现为空操作，小新 1.46 的 `PaopaoPetDisplay` 已经覆写该接口。因此本设计不新增接口，直接复用 `CompleteBootSplash()` 表达“启动动画到这里结束”。

应用层只需要表达“启动动画到这里结束”，不应知道具体是 GIF 层、LVGL 层还是宠物动画层。

## Application 改动

音频阶段从用户可见状态改为诊断阶段：

```cpp
BootDiagnosticsMark("app_audio_start");
ESP_LOGI(TAG, "Boot: Audio");
auto codec = board.GetAudioCodec();
audio_service_.Initialize(codec);
audio_service_.Start();
display->CompleteBootSplash();
```

随后保持现有 Wi-Fi 逻辑：

```cpp
BootDiagnosticsMark("app_network_start");
display->SetStatus("Boot: Wi-Fi");
display->UpdateStatusBar(true);
display->SetStatus(Lang::Strings::SCANNING_WIFI);
display->UpdateStatusBar(true);
board.StartNetwork();
```

这样 USB 供电和电池供电都走同一条逻辑。差异只体现在 Audio 阶段持续时间，而不是用户看到不同的工程状态文本。

## 显示层行为

小新 1.46 显示层需要保证：

1. 开机动画在 `SetupUI()` 后处于可见状态。
2. `CompleteBootSplash()` 可重复调用，多次调用不产生副作用。
3. 结束动画后主 UI 已经可见，不能出现黑屏、白屏或未初始化控件裸露。
4. 如果开机动画资源加载失败，`CompleteBootSplash()` 仍安全返回。
5. Wi-Fi 状态显示不应被已结束的开机动画遮挡。

当前小新 1.46 显示层已有 boot splash 退场机制，实现阶段应复用它，而不是新增另一套动画生命周期。

## 诊断与超时

本设计不要求在正常路径显示 `Boot: Audio`。诊断保留在三处：

1. `BootDiagnosticsMark("app_audio_start")`
2. `ESP_LOGI(TAG, "Boot: Audio")`
3. 如果未来需要，可在调试构建中显示隐藏诊断状态

本设计不新增 Audio 阶段超时处理。原因是音频初始化当前仍是同步启动流程；如果它卡死，原有系统也无法继续进入 Wi-Fi。显示动画可以改善体验，但不应伪装成启动成功。

后续如果要处理卡死问题，应另开设计：为音频初始化增加超时、错误页或降级启动模式。

## 测试与验证

1. 静态测试：确认 `Application::Initialize()` 不再包含用户可见的 `display->SetStatus("Boot: Audio")`。
2. 静态测试：确认 `BootDiagnosticsMark("app_audio_start")` 仍保留。
3. 静态测试：确认 `CompleteBootSplash()` 出现在 `audio_service_.Start()` 之后、`Boot: Wi-Fi` 之前。
4. 单元/路径测试：覆盖启动顺序，防止 Wi-Fi 状态显示早于动画结束。
5. 实机 USB 验证：开机动画正常结束，进入 Wi-Fi/主界面无明显回退。
6. 实机电池验证：原本显示 `Boot: Audio` 的时间段继续显示开机动画；Audio 完成后动画结束并进入 Wi-Fi 状态。

## 风险

1. 如果开机动画层本身依赖音频初始化后的某些资源，动画可能不能覆盖 Audio 阶段。当前观察更像是 UI 已经 ready，风险较低。
2. 如果 `CompleteBootSplash()` 调用过早，可能短暂露出未准备好的主 UI。结束点必须放在 `audio_service_.Start()` 之后。
3. 如果显示层退场动画耗时较长，可能遮挡 `Boot: Wi-Fi` 或扫描 Wi-Fi 状态。退场应短且可中断。
4. 如果后续有人重新添加 `Boot: Audio` 状态显示，电池慢启动时问题会复发。测试需要锁住这个路径。

## 推荐实现顺序

1. 复用现有 `display->CompleteBootSplash()` 作为 Audio 阶段结束后的退场调用。
2. 移除用户可见的 `display->SetStatus("Boot: Audio")`，改为日志。
3. 在 `audio_service_.Start()` 之后、`Boot: Wi-Fi` 之前结束动画。
4. 补充路径测试，锁定顺序和诊断保留。
5. 烧录实机验证 USB 和外接锂电池两种路径。
