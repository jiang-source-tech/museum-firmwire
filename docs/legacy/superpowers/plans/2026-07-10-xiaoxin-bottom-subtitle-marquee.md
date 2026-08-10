# Xiaoxin Bottom Subtitle Marquee Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将小新简单界面的所有底部字幕统一改为无条件、像素级、从右向左的无限循环动画，并在新字幕到达时立即从右侧重新开始。

**Architecture:** 保留现有 `Display::SetChatMessage()` 调用边界，在 `LcdDisplay` 内部用一个裁剪视口和一个按内容宽度布局的标签实现跑马灯。完整字符串一次性写入标签，LVGL 动画只改变标签 X 坐标；旧的 UTF-8 前缀定时器和 `LV_LABEL_LONG_SCROLL_CIRCULAR` 路径全部删除。

**Tech Stack:** C++17、ESP-IDF、LVGL 9、pytest 源码路径回归测试、ESP-IDF 固件构建。

## Global Constraints

- 所有非空底部字幕都必须移动，不能以文字是否超宽作为条件。
- 动画方向固定为从右向左，速度固定为 60 px/s，路径为线性。
- 动画无限循环，不设置启动延迟或循环延迟。
- 新字幕立即删除旧动画，并从字幕视口右侧重新开始。
- 空字幕停止动画、清空标签、重置位置并隐藏底栏。
- 微信气泡式聊天界面、通知卡片、设置页面和顶部状态栏不变。
- 所有 LVGL 对象和动画操作继续位于现有显示锁保护范围内。
- 不新增依赖，不修改服务器消息格式，不要求调用方区分消息角色或长度。

## File Map

- `tests/xiaoxin_bottom_subtitle_stream_test.py`：把旧“逐字流式”断言替换为右向左跑马灯行为断言，并锁定激活消息和小新显示覆写仍走统一路径。
- `main/display/lcd_display.h`：删除字符流定时器状态，声明跑马灯启动、停止和 X 坐标动画回调。
- `main/display/lcd_display.cc`：配置字幕裁剪视口，创建固定速度的无限 X 位移动画，接管设置、清空和析构生命周期。
- `main/application.cc`：不修改；由回归测试确认激活验证码继续通过 `Alert()` 进入 `SetChatMessage("system", message)`。
- `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`：不修改；由回归测试确认 `PaopaoPetDisplay` 继续委托 `LcdDisplay::SetChatMessage()`。

---

### Task 1: 用失败测试定义统一跑马灯行为

**Files:**
- Modify: `tests/xiaoxin_bottom_subtitle_stream_test.py`
- Read: `main/application.cc:941-980`
- Read: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc:785-827`

**Interfaces:**
- Consumes: 当前 `LcdDisplay::SetChatMessage(const char*, const char*)`、`Application::Alert()` 和 `PaopaoPetDisplay::SetChatMessage()` 调用链。
- Produces: 对 `StartChatMessageMarqueeLocked(const char*)`、`StopChatMessageMarqueeLocked()` 和 `SetChatMessageMarqueeX(void*, int32_t)` 的源码级行为契约。

- [ ] **Step 1: 替换旧的逐字流式测试**

保留 `read_source()`、`function_body()` 和 `last_function_body()` 三个辅助函数，把测试主体替换为以下内容：

```python
LCD_HEADER = Path("main/display/lcd_display.h")
LCD_SOURCE = Path("main/display/lcd_display.cc")
APPLICATION_SOURCE = Path("main/application.cc")
PAOPAO_DISPLAY_SOURCE = Path(
    "main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc"
)


def test_all_bottom_subtitle_roles_start_the_same_marquee():
    source = read_source(LCD_SOURCE)
    set_message = last_function_body(source, "void LcdDisplay::SetChatMessage")
    start_marquee = function_body(
        source, "void LcdDisplay::StartChatMessageMarqueeLocked"
    )

    assert "StartChatMessageMarqueeLocked(content);" in set_message
    assert 'strcmp(role, "assistant")' not in set_message
    assert 'strcmp(role, "system")' not in set_message
    assert "lv_label_set_text(chat_message_label_, content);" in start_marquee


def test_marquee_moves_every_text_right_to_left_at_fixed_speed_forever():
    source = read_source(LCD_SOURCE)
    start_marquee = function_body(
        source, "void LcdDisplay::StartChatMessageMarqueeLocked"
    )

    assert "k_chat_message_marquee_speed_px_per_second = 60" in source
    assert "lv_obj_get_content_width(bottom_bar_)" in start_marquee
    assert "lv_obj_get_width(chat_message_label_)" in start_marquee
    assert "const int32_t start_x = viewport_width;" in start_marquee
    assert "const int32_t end_x = -label_width;" in start_marquee
    assert "lv_anim_speed_to_time(" in start_marquee
    assert "lv_anim_set_var(&animation, chat_message_label_);" in start_marquee
    assert "lv_anim_set_exec_cb(&animation, SetChatMessageMarqueeX);" in start_marquee
    assert "lv_anim_set_values(&animation, start_x, end_x);" in start_marquee
    assert "lv_anim_set_path_cb(&animation, lv_anim_path_linear);" in start_marquee
    assert "lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);" in start_marquee
    assert "lv_anim_start(&animation)" in start_marquee
    assert "label_width > viewport_width" not in start_marquee
    assert "LV_LABEL_LONG_SCROLL_CIRCULAR" not in start_marquee


def test_new_and_empty_messages_restart_or_stop_the_marquee():
    source = read_source(LCD_SOURCE)
    set_message = last_function_body(source, "void LcdDisplay::SetChatMessage")
    start_marquee = function_body(
        source, "void LcdDisplay::StartChatMessageMarqueeLocked"
    )
    stop_marquee = function_body(
        source, "void LcdDisplay::StopChatMessageMarqueeLocked"
    )
    clear_messages = function_body(source, "void LcdDisplay::ClearChatMessages")

    assert start_marquee.index("StopChatMessageMarqueeLocked();") < start_marquee.index(
        "lv_label_set_text(chat_message_label_, content);"
    )
    assert "lv_anim_delete(chat_message_label_, SetChatMessageMarqueeX);" in stop_marquee
    assert "lv_obj_set_x(chat_message_label_, 0);" in stop_marquee
    assert "StopChatMessageMarqueeLocked();" in set_message
    assert 'lv_label_set_text(chat_message_label_, "");' in set_message
    assert "lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);" in set_message
    assert "StopChatMessageMarqueeLocked();" in clear_messages


def test_simple_bottom_bar_is_a_clipped_single_line_marquee_viewport():
    source = read_source(LCD_SOURCE)
    setup_ui = last_function_body(source, "void LcdDisplay::SetupUI")
    subtitle_setup = setup_ui[
        setup_ui.index("#if CONFIG_USE_MULTILINE_CHAT_MESSAGE") :
        setup_ui.index("low_battery_popup_ = lv_obj_create")
    ]

    assert "lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);" in subtitle_setup
    assert "lv_obj_set_width(chat_message_label_, LV_SIZE_CONTENT);" in subtitle_setup
    assert "lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_CLIP);" in subtitle_setup
    assert "LV_LABEL_LONG_SCROLL_CIRCULAR" not in subtitle_setup
    assert "lv_obj_set_style_anim(chat_message_label_" not in subtitle_setup
    assert "lv_obj_set_style_anim_duration(chat_message_label_" not in subtitle_setup


def test_legacy_character_stream_state_is_removed():
    header = read_source(LCD_HEADER)
    source = read_source(LCD_SOURCE)

    assert "chat_message_stream_timer_" not in header
    assert "chat_message_stream_text_" not in header
    assert "chat_message_stream_offset_" not in header
    assert "NextUtf8CharacterEnd" not in header
    assert "ChatMessageStreamTimerCallback" not in header
    assert "substr(0, end)" not in source
    assert "k_chat_message_stream_frame_ms" not in source


def test_activation_and_paopao_display_keep_using_the_unified_subtitle_path():
    application = read_source(APPLICATION_SOURCE)
    paopao = read_source(PAOPAO_DISPLAY_SOURCE)
    show_activation = function_body(
        application, "void Application::ShowActivationCode"
    )
    alert = function_body(application, "void Application::Alert")
    paopao_set_message = function_body(
        paopao,
        "virtual void SetChatMessage(const char* role, const char* content) override",
    )
    lcd_set_message = last_function_body(
        read_source(LCD_SOURCE), "void LcdDisplay::SetChatMessage"
    )

    assert "Alert(Lang::Strings::ACTIVATION, message.c_str()" in show_activation
    assert 'display->SetChatMessage("system", message);' in alert
    assert "LcdDisplay::SetChatMessage(role, content);" in paopao_set_message
    assert "StartChatMessageMarqueeLocked(content);" in lcd_set_message
```

- [ ] **Step 2: 运行测试并确认按预期失败**

Run:

```powershell
python -m pytest tests/xiaoxin_bottom_subtitle_stream_test.py -q
```

Expected: FAIL；首个失败应指出 `StartChatMessageMarqueeLocked` 不存在，或断言仍找到旧的字符流定时器。不得因为 Python 语法错误、路径错误或辅助函数错误而失败。

- [ ] **Step 3: 提交失败测试**

```powershell
git add -- tests/xiaoxin_bottom_subtitle_stream_test.py
git commit -m "test: specify looping bottom subtitle marquee"
```

---

### Task 2: 实现像素级右向左无限循环动画

**Files:**
- Modify: `main/display/lcd_display.h:10-49`
- Modify: `main/display/lcd_display.cc:19-433`
- Modify: `main/display/lcd_display.cc:1020-1070`
- Modify: `main/display/lcd_display.cc:1123-1164`
- Test: `tests/xiaoxin_bottom_subtitle_stream_test.py`

**Interfaces:**
- Consumes: `bottom_bar_` 裁剪视口、`chat_message_label_` 标签、LVGL `lv_anim_*` API 和现有显示锁。
- Produces: `void StartChatMessageMarqueeLocked(const char* content)`、`void StopChatMessageMarqueeLocked()`、`static void SetChatMessageMarqueeX(void* object, int32_t x)`。

- [ ] **Step 1: 用跑马灯接口替换头文件中的字符流状态**

在 `main/display/lcd_display.h` 中删除 `<string>`、`chat_message_stream_timer_`、`chat_message_stream_text_`、`chat_message_stream_offset_` 和四个字符流方法，改为：

```cpp
    void StartChatMessageMarqueeLocked(const char* content);
    void StopChatMessageMarqueeLocked();
    static void SetChatMessageMarqueeX(void* object, int32_t x);
```

这些声明继续放在 `InitializeLcdThemes()`、`Lock()` 和 `Unlock()` 附近的受保护区域，不新增公开 API。

- [ ] **Step 2: 删除旧字符流实现并增加跑马灯实现**

在 `main/display/lcd_display.cc` 顶部把旧帧间隔常量替换为：

```cpp
static constexpr uint32_t k_chat_message_marquee_speed_px_per_second = 60;
```

删除 `NextUtf8CharacterEnd()`、`StopChatMessageStreamLocked()`、`StartChatMessageStreamLocked()` 和 `ChatMessageStreamTimerCallback()`，在原位置实现：

```cpp
void LcdDisplay::SetChatMessageMarqueeX(void* object, int32_t x) {
    if (object == nullptr) {
        return;
    }
    lv_obj_set_x(static_cast<lv_obj_t*>(object), static_cast<lv_coord_t>(x));
}

void LcdDisplay::StopChatMessageMarqueeLocked() {
    if (chat_message_label_ == nullptr) {
        return;
    }
    lv_anim_delete(chat_message_label_, SetChatMessageMarqueeX);
    lv_obj_set_x(chat_message_label_, 0);
}

void LcdDisplay::StartChatMessageMarqueeLocked(const char* content) {
    if (chat_message_label_ == nullptr || bottom_bar_ == nullptr ||
        content == nullptr || content[0] == '\0') {
        return;
    }

    StopChatMessageMarqueeLocked();
    lv_label_set_text(chat_message_label_, content);
    lv_obj_set_width(chat_message_label_, LV_SIZE_CONTENT);
    lv_obj_update_layout(chat_message_label_);

    const int32_t viewport_width = lv_obj_get_content_width(bottom_bar_);
    const int32_t label_width = lv_obj_get_width(chat_message_label_);
    const int32_t start_x = viewport_width;
    const int32_t end_x = -label_width;
    const uint32_t duration_ms = lv_anim_speed_to_time(
        k_chat_message_marquee_speed_px_per_second,
        start_x,
        end_x
    );

    lv_obj_set_x(chat_message_label_, static_cast<lv_coord_t>(start_x));

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, chat_message_label_);
    lv_anim_set_exec_cb(&animation, SetChatMessageMarqueeX);
    lv_anim_set_values(&animation, start_x, end_x);
    lv_anim_set_duration(&animation, duration_ms);
    lv_anim_set_path_cb(&animation, lv_anim_path_linear);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);

    if (lv_anim_start(&animation) == nullptr) {
        lv_obj_set_x(chat_message_label_, 0);
    }
}
```

该实现不得增加 `if (label_width > viewport_width)` 一类条件。短文字必须与长文字一样创建动画。

- [ ] **Step 3: 简化析构生命周期**

把析构函数开头的字符流清理：

```cpp
    StopChatMessageStreamLocked();
    if (chat_message_stream_timer_ != nullptr) {
        lv_timer_delete(chat_message_stream_timer_);
        chat_message_stream_timer_ = nullptr;
    }
```

替换为：

```cpp
    StopChatMessageMarqueeLocked();
```

确保该调用位于删除 `chat_message_label_` 之前。

- [ ] **Step 4: 把简单底栏配置成裁剪视口和内容宽度标签**

在非 `CONFIG_USE_MULTILINE_CHAT_MESSAGE` 的简单底栏分支中：

```cpp
    lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    chat_message_label_ = lv_label_create(bottom_bar_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_SIZE_CONTENT);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
    lv_obj_align(chat_message_label_, LV_ALIGN_LEFT_MID, 0, 0);
```

删除以下旧配置：

```cpp
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_delay(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(
        chat_message_label_,
        lv_anim_speed_clamped(60, 300, 60000),
        LV_PART_MAIN
    );
```

保留现有底栏尺寸、背景、边距、主题颜色、底部对齐和初始隐藏行为。

- [ ] **Step 5: 让设置和清空消息统一管理动画**

在 `LcdDisplay::SetChatMessage()` 的空消息分支中使用：

```cpp
    if (content == nullptr || content[0] == '\0') {
        StopChatMessageMarqueeLocked();
        lv_label_set_text(chat_message_label_, "");
        if (bottom_bar_ != nullptr) {
            lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (bottom_bar_ != nullptr && !hide_subtitle_) {
        lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    }
    StartChatMessageMarqueeLocked(content);
```

删除 `StartChatMessageStreamLocked(content)`。在 `ClearChatMessages()` 中把 `StopChatMessageStreamLocked()` 替换为 `StopChatMessageMarqueeLocked()`，其他清空和隐藏逻辑保持不变。

- [ ] **Step 6: 运行聚焦测试并确认通过**

Run:

```powershell
python -m pytest tests/xiaoxin_bottom_subtitle_stream_test.py -q
```

Expected: `6 passed`，退出码为 0。

- [ ] **Step 7: 检查旧实现已经完全删除**

Run:

```powershell
rg -n "chat_message_stream|NextUtf8CharacterEnd|ChatMessageStreamTimerCallback" main/display/lcd_display.h main/display/lcd_display.cc
```

Expected: 无输出，退出码为 1。不得残留旧字段、旧方法或旧字符流回调。字幕标签不再使用 `LV_LABEL_LONG_SCROLL_CIRCULAR` 由聚焦测试单独保证；状态栏仍可继续使用该模式。

- [ ] **Step 8: 提交实现**

```powershell
git add -- main/display/lcd_display.h main/display/lcd_display.cc
git commit -m "fix: loop all bottom subtitles right to left"
```

---

### Task 3: 回归验证调用链、其他界面行为和固件构建

**Files:**
- Verify: `tests/xiaoxin_bottom_subtitle_stream_test.py`
- Verify: `tests/xiaoxin_error_display_path_test.py`
- Verify: `tests/xiaoxin_voice_state_flow_path_test.py`
- Verify: `tests/xiaoxin_boot_diagnostics_path_test.py`
- Verify: `tests/wifi_config_status_path_test.py`
- Verify: full `tests/` suite
- Verify: ESP-IDF firmware build

**Interfaces:**
- Consumes: Task 2 提供的跑马灯实现和未修改的激活、错误、语音消息调用链。
- Produces: 可复现的自动化测试结果、固件编译结果和实机检查清单。

- [ ] **Step 1: 运行直接相关回归测试**

Run:

```powershell
python -m pytest `
  tests/xiaoxin_bottom_subtitle_stream_test.py `
  tests/xiaoxin_error_display_path_test.py `
  tests/xiaoxin_voice_state_flow_path_test.py `
  tests/xiaoxin_boot_diagnostics_path_test.py `
  tests/wifi_config_status_path_test.py `
  -q
```

Expected: 所有测试通过，退出码为 0；不得出现错误提示被提前清除、语音角色路径改变或激活任务路径改变。

- [ ] **Step 2: 运行完整 Python 回归测试**

Run:

```powershell
python -m pytest tests -q
```

Expected: 所有测试通过，退出码为 0。

- [ ] **Step 3: 检查代码格式和未提交差异**

Run:

```powershell
git diff --check
git status --short
```

Expected: `git diff --check` 无输出；`git status --short` 无未提交文件。若执行过程中产生预期外文件，先确认来源，不得删除用户已有内容。

- [ ] **Step 4: 构建固件**

Run:

```powershell
idf.py build
```

Expected: 命令退出码为 0，并输出 ESP-IDF 构建完成信息；`main/display/lcd_display.cc` 不得出现 LVGL 回调类型、动画 API 或坐标类型编译错误。

- [ ] **Step 5: 在可用硬件上执行视觉验收**

依次触发并观察：

```text
1. 绑定设备验证码：“在控制台输入验证码:xxxxxx”
2. 短系统提示：“错误”
3. 用户语音识别结果
4. 助手长回复
5. 在旧字幕仍滚动时快速触发一条新字幕
6. 清空字幕或结束会话
```

Expected:

```text
- 每条非空字幕都从右边界之外进入；
- 所有字幕以相同像素速度向左移动；
- 完全离开左侧后立即无限循环；
- 新字幕立即替换旧字幕并从右侧重新开始；
- 清空后底栏隐藏且不再有残余动画；
- 字幕不越过底栏裁剪区域，不出现残影、字符切割或跳速。
```

如果当前没有硬件，将该步骤明确记录为“未执行：缺少可用设备”，不能把自动化测试等同于视觉验收。

---

## Completion Checklist

- [ ] 失败测试在生产代码修改前运行，并因缺少跑马灯实现而失败。
- [ ] 聚焦测试显示 `6 passed`。
- [ ] 旧字符流状态和 `LV_LABEL_LONG_SCROLL_CIRCULAR` 已从 LCD 底栏路径删除。
- [ ] 激活、错误和语音调用链仍进入统一 `SetChatMessage()` 路径。
- [ ] 完整 Python 测试套件通过。
- [ ] ESP-IDF 固件构建通过。
- [ ] 实机视觉验收已通过，或明确记录因缺少硬件未执行。
- [ ] 工作区无意外未提交修改。
