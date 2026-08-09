#include "xiaoxin_overview_model.h"

#include <stdio.h>
#include <string.h>

enum {
  XIAOXIN_OVERVIEW_WEATHER_INDEX = 0,
  XIAOXIN_OVERVIEW_COURSE_INDEX = 1,
  XIAOXIN_OVERVIEW_TODO_INDEX = 2,
  XIAOXIN_OVERVIEW_DEVICE_INDEX = 3,
  XIAOXIN_OVERVIEW_GROWTH_INDEX = 4,
};

static bool non_empty(const char* text) {
  return text != NULL && text[0] != '\0';
}

static void copy_body(xiaoxin_overview_snapshot_t* snapshot, uint8_t index, const char* text) {
  snprintf(snapshot->body_storage[index], XIAOXIN_OVERVIEW_BODY_MAX, "%s", text);
}

static void copy_detail(xiaoxin_overview_snapshot_t* snapshot, uint8_t index, const char* text) {
  snprintf(snapshot->detail_storage[index], XIAOXIN_OVERVIEW_DETAIL_MAX, "%s", text);
}

static int clamp_two_digit(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 99) {
    return 99;
  }
  return value;
}

static void set_item(
  xiaoxin_overview_snapshot_t* snapshot,
  uint8_t index,
  const char* title,
  const char* tag,
  uint32_t priority
) {
  snapshot->items[index].title = title;
  snapshot->items[index].body = snapshot->body_storage[index];
  snapshot->items[index].detail = snapshot->detail_storage[index];
  snapshot->items[index].tag = tag;
  snapshot->items[index].priority = priority;
  snapshot->items[index].ttl_ms = 0;
}

static void build_time_text(
  const xiaoxin_overview_state_t* state,
  xiaoxin_overview_snapshot_t* snapshot
) {
  static const char* weekdays[] = {
    "周日",
    "周一",
    "周二",
    "周三",
    "周四",
    "周五",
    "周六",
  };

  if (state != NULL && state->time_valid) {
    const uint8_t weekday = state->weekday <= 6 ? state->weekday : 0;
    const int hour = clamp_two_digit(state->hour);
    const int minute = clamp_two_digit(state->minute);
    const int month = clamp_two_digit(state->month);
    const int day = clamp_two_digit(state->day);
    snprintf(snapshot->time_text, sizeof(snapshot->time_text), "%02d:%02d", hour, minute);
    snprintf(
      snapshot->date_text,
      sizeof(snapshot->date_text),
      "%d月%d日 %s",
      month,
      day,
      weekdays[weekday]
    );
    return;
  }

  snprintf(snapshot->time_text, sizeof(snapshot->time_text), "%s", "--:--");
  snprintf(snapshot->date_text, sizeof(snapshot->date_text), "%s", "时间未同步");
}

static void build_weather_item(
  const xiaoxin_overview_state_t* state,
  xiaoxin_overview_snapshot_t* snapshot
) {
  if (state == NULL || !state->network_connected) {
    copy_body(snapshot, XIAOXIN_OVERVIEW_WEATHER_INDEX, "天气未同步");
    copy_detail(snapshot, XIAOXIN_OVERVIEW_WEATHER_INDEX, "连接网络后更新");
    return;
  }

  if (!state->weather_configured) {
    copy_body(snapshot, XIAOXIN_OVERVIEW_WEATHER_INDEX, "未配置位置");
    copy_detail(snapshot, XIAOXIN_OVERVIEW_WEATHER_INDEX, "设置位置后显示");
    return;
  }

  if (state->weather_available && non_empty(state->weather_summary)) {
    copy_body(snapshot, XIAOXIN_OVERVIEW_WEATHER_INDEX, state->weather_summary);
    copy_detail(
      snapshot,
      XIAOXIN_OVERVIEW_WEATHER_INDEX,
      non_empty(state->weather_detail) ? state->weather_detail : "天气详情待同步"
    );
    return;
  }

  copy_body(snapshot, XIAOXIN_OVERVIEW_WEATHER_INDEX, "天气未同步");
  copy_detail(snapshot, XIAOXIN_OVERVIEW_WEATHER_INDEX, "连接网络后更新");
}

static void build_course_item(
  const xiaoxin_overview_state_t* state,
  xiaoxin_overview_snapshot_t* snapshot
) {
  if (state == NULL || !state->course_configured) {
    copy_body(snapshot, XIAOXIN_OVERVIEW_COURSE_INDEX, "暂无课程");
    copy_detail(snapshot, XIAOXIN_OVERVIEW_COURSE_INDEX, "在配置中添加课表");
    return;
  }

  if (!state->course_available_today) {
    copy_body(snapshot, XIAOXIN_OVERVIEW_COURSE_INDEX, "今日无课");
    copy_detail(snapshot, XIAOXIN_OVERVIEW_COURSE_INDEX, "可以安排自习");
    return;
  }

  copy_body(
    snapshot,
    XIAOXIN_OVERVIEW_COURSE_INDEX,
    non_empty(state->course_title) ? state->course_title : "课程待确认"
  );
  copy_detail(
    snapshot,
    XIAOXIN_OVERVIEW_COURSE_INDEX,
    non_empty(state->course_detail) ? state->course_detail : "查看课表详情"
  );
}

static void build_todo_item(
  const xiaoxin_overview_state_t* state,
  xiaoxin_overview_snapshot_t* snapshot
) {
  if (state == NULL || !state->todo_configured || state->todo_count == 0) {
    copy_body(snapshot, XIAOXIN_OVERVIEW_TODO_INDEX, "暂无待办");
    copy_detail(snapshot, XIAOXIN_OVERVIEW_TODO_INDEX, "添加提醒后显示");
    return;
  }

  snprintf(
    snapshot->body_storage[XIAOXIN_OVERVIEW_TODO_INDEX],
    XIAOXIN_OVERVIEW_BODY_MAX,
    "%u 项待办",
    (unsigned int)state->todo_count
  );
  copy_detail(
    snapshot,
    XIAOXIN_OVERVIEW_TODO_INDEX,
    non_empty(state->todo_detail) ? state->todo_detail : "打开待办查看"
  );
}

static uint8_t clamp_battery_level(uint8_t level) {
  return level > 4 ? 4 : level;
}

static void format_battery_blocks(char* buffer, size_t buffer_size, uint8_t level) {
  static const char* levels[] = {
    "[□□□□]",
    "[■□□□]",
    "[■■□□]",
    "[■■■□]",
    "[■■■■]",
  };
  snprintf(buffer, buffer_size, "%s", levels[clamp_battery_level(level)]);
}

static void build_device_item(
  const xiaoxin_overview_state_t* state,
  xiaoxin_overview_snapshot_t* snapshot
) {
  const bool network_connected = state != NULL && state->network_connected;

  copy_body(
    snapshot,
    XIAOXIN_OVERVIEW_DEVICE_INDEX,
    network_connected ? "WiFi 已连接" : "离线模式"
  );

  if (state == NULL || state->battery_power_source == XIAOXIN_BATTERY_POWER_UNKNOWN) {
    copy_detail(snapshot, XIAOXIN_OVERVIEW_DEVICE_INDEX, "电量检测中");
    return;
  }

  if (state->battery_power_source == XIAOXIN_BATTERY_POWER_EXTERNAL) {
    copy_detail(snapshot, XIAOXIN_OVERVIEW_DEVICE_INDEX, "外部供电");
    return;
  }

  if (!state->battery_known) {
    copy_detail(snapshot, XIAOXIN_OVERVIEW_DEVICE_INDEX, "电量检测中");
    return;
  }

  char blocks[24] = {};
  format_battery_blocks(blocks, sizeof(blocks), state->battery_level);

  const char* label = "电量";
  if (state->battery_state == XIAOXIN_BATTERY_STATE_LOW) {
    label = "低电";
  } else if (state->battery_state == XIAOXIN_BATTERY_STATE_CRITICAL) {
    label = "即将关机";
  }

  snprintf(
    snapshot->detail_storage[XIAOXIN_OVERVIEW_DEVICE_INDEX],
    XIAOXIN_OVERVIEW_DETAIL_MAX,
    "%s %s",
    label,
    blocks
  );
}

static bool build_growth_item(
  const xiaoxin_overview_state_t* state,
  xiaoxin_overview_snapshot_t* snapshot
) {
  if (state == NULL || !state->companion_available ||
      state->xiaoxin_age < 1 || state->xiaoxin_age > 4) {
    return false;
  }

  snprintf(
    snapshot->body_storage[XIAOXIN_OVERVIEW_GROWTH_INDEX],
    XIAOXIN_OVERVIEW_BODY_MAX,
    "小芯 %u 岁",
    (unsigned int)state->xiaoxin_age
  );
  copy_detail(
    snapshot,
    XIAOXIN_OVERVIEW_GROWTH_INDEX,
    non_empty(state->growth_summary) ? state->growth_summary : "成长记录持续更新"
  );
  return true;
}

void xiaoxin_overview_model_build(
  const xiaoxin_overview_state_t* state,
  xiaoxin_overview_snapshot_t* snapshot
) {
  if (snapshot == NULL) {
    return;
  }

  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->item_count = 4;

  set_item(snapshot, XIAOXIN_OVERVIEW_WEATHER_INDEX, "天气", "天气", 1);
  set_item(snapshot, XIAOXIN_OVERVIEW_COURSE_INDEX, "下一节课", "课程", 2);
  set_item(snapshot, XIAOXIN_OVERVIEW_TODO_INDEX, "今日待办", "待办", 3);
  set_item(snapshot, XIAOXIN_OVERVIEW_DEVICE_INDEX, "设备状态", "设备", 4);
  set_item(snapshot, XIAOXIN_OVERVIEW_GROWTH_INDEX, "小芯成长", "成长", 5);

  build_time_text(state, snapshot);
  build_weather_item(state, snapshot);
  build_course_item(state, snapshot);
  build_todo_item(state, snapshot);
  build_device_item(state, snapshot);
  if (build_growth_item(state, snapshot)) {
    snapshot->item_count = XIAOXIN_OVERVIEW_ITEM_COUNT;
  }
}
