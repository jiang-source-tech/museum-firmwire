# 小芯系统通知大卡分页 UI 进度记录

> 日期：2026-06-18
> 范围：Waveshare ESP32-S3 Touch LCD 1.46 圆屏通知页
> 状态：已实现并通过构建验证，仍需实机继续调视觉细节

## 1. 本轮目标

把通知页从“三条小列表/蓝框调试感”的样式，改成更像系统通知中心的单张大卡：

- 通知页只展示当前一条系统通知。
- 左上角使用统一系统通知 icon。
- 多条通知通过左右滑切换。
- 上下滑保留给页面进入/收回逻辑，避免和通知分页冲突。
- 右上角 WiFi 和电量作为全局安全区浮层显示，不跟随通知卡片滑动。
- 底部不再显示 `1 / 4` 这种数字页码，改为更轻的小圆点指示。

## 2. 已完成的代码修改

### 2.1 通知分页状态

修改文件：

- `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_card_pager.h`
- `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_card_pager.c`
- `tests/xiaoxin_card_pager_test.c`

## 2026-06-18 透明主页背景与重启问题修复

### 目标

通知卡片不再使用 snapshot 或复制出来的背景图。分页层保持透明，主页真实停留在底层作为背景，通知卡片以半透明玻璃层浮在上方。

### 修改记录

- `paopao_pet` 任务栈从 `4096` 提升到 `12 KiB`，修复上下滑分页时的栈溢出重启。
- 移除分页背景图状态和渲染逻辑，包括 `card_background_buffer_`、`card_background_image_` 以及相关 snapshot/fallback 处理。
- `card_layer_` 保持透明，不再铺满屏暗色或背景图片。
- 新增分页可见时的桌宠动画控制：
  - 进入通知页/总览页时暂停当前 GIF，主页停在当前帧。
  - 返回 Home 后恢复 GIF 播放。
  - 分页层可见期间暂缓应用新的桌宠状态，避免底层背景突然跳帧或切换动画。
- 下调通知卡和总览行背景透明度，让主页静止画面可见，同时保留文字可读性。

### 验证

- `xiaoxin_card_pager_test`：通过。
- `idf.py build`：通过，固件输出为 `build/ai_pet.bin`。

新增状态：

- `notification_index`
- `notification_count`

新增接口：

```c
uint8_t xiaoxin_card_pager_notification_index(const xiaoxin_card_pager_t* pager);
uint8_t xiaoxin_card_pager_notification_count(const xiaoxin_card_pager_t* pager);
bool xiaoxin_card_pager_notification_next(xiaoxin_card_pager_t* pager);
bool xiaoxin_card_pager_notification_prev(xiaoxin_card_pager_t* pager);
const xiaoxin_card_item_t* xiaoxin_card_pager_current_notification(const xiaoxin_card_pager_t* pager);
```

当前行为：

- 通知 index 默认从 `0` 开始。
- 左滑调用 `notification_next()`。
- 右滑调用 `notification_prev()`。
- 到边界不循环。

### 2.2 通知页视觉结构

修改文件：

- `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`

主要变化：

- 通知页从多张完整小卡改为单张大玻璃通知卡。
- 卡片尺寸调整为更大的圆角玻璃卡：
  - 宽度约 `252`
  - 高度约 `126`
  - 圆角约 `24`
- 卡片背景从亮蓝边框改成深色玻璃：
  - 背景 `#1b1b1f`
  - 低透明白色描边
  - 黑色柔和阴影
- 左上角新增统一通知 icon：
  - 当前使用 `!` 作为轻量符号
  - 背景色根据通知优先级变化
- 卡片内部显示：
  - 标题
  - 正文
  - 右上角“现在”

### 2.3 全局 WiFi / 电量安全区浮层

修改文件：

- `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`

主要变化：

- 保留全局电量显示，不恢复通知卡片内部四格电量。
- 新增 `system_overlay_`，挂在 `lv_screen_active()`，不是 `card_layer_` 子对象。
- `system_overlay_` 统一承载 WiFi 标志和全局电量：
  - 浮层尺寸为 `76 x 24`。
  - 浮层位置为 `LV_ALIGN_TOP_RIGHT, -76, 50`。
  - 该位置位于圆屏右上安全区内，避免图标被圆形屏幕边缘裁切。
- 将 WiFi `network_label_` 迁移到 `system_overlay_` 内：
  - 继续复用原 `UpdateStatusBar()` 网络图标更新逻辑。
  - 原顶部栏中的旧 WiFi label 被隐藏，避免重复显示。
- 将 `battery_overlay_`、`battery_overlay_box_`、`battery_overlay_fill_`、`battery_overlay_cap_` 放入 `system_overlay_`。
- 电量填充颜色：
  - 正常：蓝色
  - 低电量：红色
- `RaiseOverlayObjects()` 会把 `system_overlay_` 移动到前景。
- 分页页隐藏 `top_bar_` / `status_bar_` / `bottom_bar_` 时，`system_overlay_` 仍然可见。
- `UpdateStatusBar()` 后会同步刷新全局电量填充宽度。
- 通知卡片滑动切换时，WiFi 和电量浮层不会跟着卡片移动。

### 2.4 通知页手势

修改文件：

- `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_card_pager.c`
- `tests/xiaoxin_card_pager_test.c`

当前手势约定：

| 场景 | 手势 | 行为 |
|---|---|---|
| Home | 下滑 | 进入通知页 |
| Home | 上滑 | 进入总览页 |
| 通知页 | 左滑 | 下一条通知 |
| 通知页 | 右滑 | 上一条通知 |
| 通知页 | 上滑 | 收回通知页 / 返回 Home |
| 通知页 | 长按 | 返回 Home |
| 总览页 | 下滑 | 返回 Home |

修复过的问题：

- 早期版本把通知分页做成上下滑，和页面收回逻辑冲突，已改为左右滑。
- 左右滑时曾复用 `card_layer_` 的 Y 轴动画，导致底部边界跟着上下移动，已改为只对当前通知卡做淡入动画，不再移动整个 `card_layer_`。

### 2.5 底部指示器

修改文件：

- `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`

主要变化：

- 移除 `1 / 4` 数字分页。
- 改为底部小圆点指示：
  - 当前通知点更亮
  - 其它通知点更暗
  - 只有一条通知时隐藏
  - 当前最多显示 4 个点

## 3. 已完成验证

### 3.1 单元测试

命令：

```powershell
gcc -std=c11 -Wall -Wextra -I. tests/xiaoxin_card_pager_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_card_pager.c -o build/xiaoxin_card_pager_test.exe
./build/xiaoxin_card_pager_test.exe
```

结果：

```text
xiaoxin_card_pager tests passed
```

### 3.2 ESP-IDF 构建

命令：

```powershell
& 'D:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1'
idf.py build
```

结果：

```text
Project build complete.
Generated D:/Learn/hzcu-xiaoxin-firmwire/build/ai_pet.bin
```

构建中仍有非阻塞 warning：

- `esp_lcd_touch_get_coordinates` deprecated warning

## 4. 当前遗留问题 / 下一步建议

视觉上用户反馈仍然“不够舒服大气”，下一步建议继续调通知页视觉，而不是再改手势：

1. **卡片位置再下移/居中**
   当前卡片视觉仍略偏上，圆屏中可考虑更接近 watchOS 通知中心的垂直居中。

2. **通知 icon 需要更像系统 icon**
   当前 `!` 比较临时，后续可改成更柔和的铃铛/小芯系统符号，或者用 LVGL 简单线条组合。

3. **卡片文字层级还要放松**
   标题和正文可以加大间距，正文可考虑两行换行显示，减少“卡片里东西挤”的感觉。

4. **底部指示器位置可继续微调**
   当前小圆点替代了 `1 / 4`，但实机上还需看是否和底部 home indicator 过近。

5. **WiFi / 电量安全区位置可实机微调**
   当前 `system_overlay_` 使用偏保守的位置，优先保证可见。实机确认无遮挡后，可略微右移或上移。

6. **后续可增加真实通知数据源**
   当前通知仍来自静态 `k_notification_items`，后续再接小程序/本地事件。

## 5. 当前代码涉及文件

- `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_card_pager.h`
- `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_card_pager.c`
- `tests/xiaoxin_card_pager_test.c`
## 2026-06-18 最新实现补充：通知卡连续竖向跟手滚动

> 说明：本节覆盖前文中“左右滑分页”“竖向切换分页”等早期方案。当前最终方向是不做分页切换感，而是做类似手机通知中心的连续竖向跟手滚动。

### 交互目标

- 多条通知放在同一个通知页里，通过竖向滑动查看。
- 拖动过程必须跟手：手指拖多少，通知卡组就移动多少。
- 不要“松手后切到下一张”的突兀分页感。
- 只有通知卡组已经滑到最底部后，再继续自下而上滑动，才收回通知页并返回 Home。

### 当前视觉结构

- 通知卡对象数量 `k_card_glass_count` 调整为 4，对应当前 4 条静态通知。
- 每张通知卡使用统一的大玻璃卡样式：
  - 宽度 `k_glass_width = 268`
  - 高度 `k_glass_height = 150`
  - 圆角 `k_glass_radius = 34`
  - 图标尺寸 `k_notification_icon_size = 46`
- 通知卡竖向排布，间距为 `k_notification_slide_pitch = 116`。
- 当前视觉中心附近的卡片更亮、阴影更强；离中心越远透明度越低。
- 底部圆点指示器保留，当前点根据连续滚动位置推导。

### 新增滚动状态

新增于 `PaopaoPetDisplay`：

```cpp
int16_t notification_scroll_y_;
int16_t notification_drag_start_scroll_y_;
```

含义：

- `notification_scroll_y_`：通知卡组当前连续滚动偏移。
- `notification_drag_start_scroll_y_`：本次触摸开始时的滚动偏移，用来计算跟手移动量。

### 新增滚动辅助函数

```cpp
static int16_t NotificationMinScrollY(uint8_t total);
static int16_t ClampNotificationScrollY(int16_t scroll_y, uint8_t total);
static int16_t NotificationScrollDisplayY(int16_t raw_scroll_y, uint8_t total);
static uint8_t NotificationIndexForScroll(int16_t scroll_y, uint8_t total);
void ApplyNotificationScrollVisual(int16_t scroll_y, bool prepare_entry_animation = false);
void AnimateNotificationScroll(int16_t start_scroll_y, int16_t target_scroll_y);
```

职责：

- `NotificationMinScrollY()`：计算最后一条通知对齐时允许的最小滚动值。
- `ClampNotificationScrollY()`：把真实滚动位置限制在顶部和底部之间。
- `NotificationScrollDisplayY()`：越界拖动时增加 1/3 阻尼，形成回弹手感。
- `NotificationIndexForScroll()`：根据连续滚动位置推导当前最接近中心的通知 index。
- `ApplyNotificationScrollVisual()`：按像素更新所有通知卡位置、透明度、阴影和圆点状态。
- `AnimateNotificationScroll()`：松手后只做边界回弹或停靠，不做强制分页切换。

### 当前手势规则

| 场景 | 手势 | 行为 |
|---|---|---|
| Home | 下滑 | 通知页从顶部进入 |
| 通知页 | 上/下滑，未到底 | 通知卡组连续跟随手指滚动 |
| 通知页 | 顶部/底部越界 | 阻尼跟手，松手回弹 |
| 通知页 | 已在最底部后继续上滑 | 外层通知页收回，返回 Home |
| 通知页 | 横向滑动 | 不切换通知，不返回 Home |
| 通知页 | 长按 | 不再返回 Home |

### 和旧方案的差异

- 旧方案：`notification_next()` / `notification_prev()` 驱动一条条通知分页切换。
- 当前方案：视觉交互由 `notification_scroll_y_` 驱动，通知卡组连续滚动。
- 旧方案：松手后切换到上一条/下一条，有明显分页感。
- 当前方案：松手后保留滚动位置，只在越界时回弹。
- 旧方案：通知页上滑容易和返回 Home 冲突。
- 当前方案：只有滑到最后一条通知底部后继续上滑，才允许返回 Home。

### 保留的状态机能力

`xiaoxin_card_pager` 中仍保留：

```c
uint8_t notification_index;
uint8_t notification_count;
bool xiaoxin_card_pager_notification_next(xiaoxin_card_pager_t* pager);
bool xiaoxin_card_pager_notification_prev(xiaoxin_card_pager_t* pager);
const xiaoxin_card_item_t* xiaoxin_card_pager_current_notification(const xiaoxin_card_pager_t* pager);
```

这些接口当前主要用于：

- 单元测试覆盖通知数量和边界行为。
- 后续真实通知数据源接入时复用状态字段。
- 圆点或当前通知状态同步。

当前视觉主路径不再依赖这些接口做分页切换动画。

### 验证

已执行：

```powershell
gcc -std=c11 -Wall -Wextra -I. tests/xiaoxin_card_pager_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_card_pager.c -o build/xiaoxin_card_pager_test.exe
.\build\xiaoxin_card_pager_test.exe

gcc -std=c11 -Wall -Wextra -I. tests/xiaoxin_battery_level_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_battery_level.c -o build/xiaoxin_battery_level_test.exe
.\build\xiaoxin_battery_level_test.exe
```

结果：

- `xiaoxin_card_pager tests passed`
- `xiaoxin_battery_level tests passed`

未执行完整固件构建：当前 PowerShell 中未找到 `idf.py`。
