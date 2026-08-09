#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_overview_model.h"

enum {
    WEATHER_INDEX = 0,
    COURSE_INDEX = 1,
    TODO_INDEX = 2,
    DEVICE_INDEX = 3,
    GROWTH_INDEX = 4,
};

static void build_device_snapshot(
    xiaoxin_battery_state_t battery_state,
    uint8_t battery_level,
    bool battery_known,
    xiaoxin_overview_snapshot_t* snapshot
) {
    xiaoxin_overview_state_t state = {
        .network_connected = true,
        .battery_state = battery_state,
        .battery_power_source = XIAOXIN_BATTERY_POWER_BATTERY,
        .battery_level = battery_level,
        .battery_known = battery_known,
    };

    xiaoxin_overview_model_build(&state, snapshot);
}

static void assert_card(
    const xiaoxin_overview_snapshot_t* snapshot,
    uint8_t index,
    const char* title,
    const char* tag,
    uint32_t priority,
    const char* body,
    const char* detail
) {
    assert(index < snapshot->item_count);
    assert(strcmp(snapshot->items[index].title, title) == 0);
    assert(strcmp(snapshot->items[index].tag, tag) == 0);
    assert(snapshot->items[index].priority == priority);
    assert(snapshot->items[index].ttl_ms == 0);
    assert(snapshot->items[index].body == snapshot->body_storage[index]);
    assert(snapshot->items[index].detail == snapshot->detail_storage[index]);
    assert(strcmp(snapshot->items[index].body, body) == 0);
    assert(strcmp(snapshot->items[index].detail, detail) == 0);
}

static void assert_standard_card_headers(const xiaoxin_overview_snapshot_t* snapshot) {
    assert(snapshot->item_count == 4);
    assert(strcmp(snapshot->items[WEATHER_INDEX].title, "天气") == 0);
    assert(strcmp(snapshot->items[WEATHER_INDEX].tag, "天气") == 0);
    assert(snapshot->items[WEATHER_INDEX].priority == 1);
    assert(strcmp(snapshot->items[COURSE_INDEX].title, "下一节课") == 0);
    assert(strcmp(snapshot->items[COURSE_INDEX].tag, "课程") == 0);
    assert(snapshot->items[COURSE_INDEX].priority == 2);
    assert(strcmp(snapshot->items[TODO_INDEX].title, "今日待办") == 0);
    assert(strcmp(snapshot->items[TODO_INDEX].tag, "待办") == 0);
    assert(snapshot->items[TODO_INDEX].priority == 3);
    assert(strcmp(snapshot->items[DEVICE_INDEX].title, "设备状态") == 0);
    assert(strcmp(snapshot->items[DEVICE_INDEX].tag, "设备") == 0);
    assert(snapshot->items[DEVICE_INDEX].priority == 4);
}

static void offline_defaults(void) {
    const xiaoxin_overview_state_t state = {
        .battery_state = XIAOXIN_BATTERY_STATE_NORMAL,
        .battery_power_source = XIAOXIN_BATTERY_POWER_BATTERY,
    };
    xiaoxin_overview_snapshot_t snapshot;

    xiaoxin_overview_model_build(&state, &snapshot);

    assert(strcmp(snapshot.time_text, "--:--") == 0);
    assert(strcmp(snapshot.date_text, "时间未同步") == 0);
    assert_standard_card_headers(&snapshot);
    assert_card(&snapshot, WEATHER_INDEX, "天气", "天气", 1, "天气未同步", "连接网络后更新");
    assert_card(&snapshot, COURSE_INDEX, "下一节课", "课程", 2, "暂无课程", "在配置中添加课表");
    assert_card(&snapshot, TODO_INDEX, "今日待办", "待办", 3, "暂无待办", "添加提醒后显示");
    assert_card(&snapshot, DEVICE_INDEX, "设备状态", "设备", 4, "离线模式", "电量检测中");
}

static void connected_without_weather_location(void) {
    const xiaoxin_overview_state_t state = {
        .network_connected = true,
        .battery_state = XIAOXIN_BATTERY_STATE_NORMAL,
        .battery_power_source = XIAOXIN_BATTERY_POWER_BATTERY,
    };
    xiaoxin_overview_snapshot_t snapshot;

    xiaoxin_overview_model_build(&state, &snapshot);

    assert_card(&snapshot, WEATHER_INDEX, "天气", "天气", 1, "未配置位置", "设置位置后显示");
    assert_card(&snapshot, DEVICE_INDEX, "设备状态", "设备", 4, "WiFi 已连接", "电量检测中");
}

static void rich_injected_sources(void) {
    const xiaoxin_overview_state_t state = {
        .time_valid = true,
        .hour = 14,
        .minute = 32,
        .month = 6,
        .day = 19,
        .weekday = 5,
        .network_connected = true,
        .battery_state = XIAOXIN_BATTERY_STATE_NORMAL,
        .battery_power_source = XIAOXIN_BATTERY_POWER_BATTERY,
        .battery_level = 3,
        .battery_known = true,
        .weather_available = true,
        .weather_configured = true,
        .weather_summary = "多云 26C",
        .weather_detail = "湿度72% · 东风2级",
        .course_configured = true,
        .course_available_today = true,
        .course_title = "高数 10:10",
        .course_detail = "教2-301 · 还有24分",
        .todo_configured = true,
        .todo_count = 2,
        .todo_detail = "实验报告 · 晚自习",
    };
    xiaoxin_overview_snapshot_t snapshot;

    xiaoxin_overview_model_build(&state, &snapshot);

    assert(strcmp(snapshot.time_text, "14:32") == 0);
    assert(strcmp(snapshot.date_text, "6月19日 周五") == 0);
    assert_card(&snapshot, WEATHER_INDEX, "天气", "天气", 1, "多云 26C", "湿度72% · 东风2级");
    assert_card(&snapshot, COURSE_INDEX, "下一节课", "课程", 2, "高数 10:10", "教2-301 · 还有24分");
    assert_card(&snapshot, TODO_INDEX, "今日待办", "待办", 3, "2 项待办", "实验报告 · 晚自习");
    assert_card(&snapshot, DEVICE_INDEX, "设备状态", "设备", 4, "WiFi 已连接", "电量 [■■■□]");
}

static void growth_projection_adds_a_fifth_overview_item(void) {
    const xiaoxin_overview_state_t state = {
        .network_connected = true,
        .battery_power_source = XIAOXIN_BATTERY_POWER_EXTERNAL,
        .companion_available = true,
        .xiaoxin_age = 2,
        .growth_summary = "我们一起走进大二了。",
    };
    xiaoxin_overview_snapshot_t snapshot;

    xiaoxin_overview_model_build(&state, &snapshot);

    assert(snapshot.item_count == 5);
    assert_card(
        &snapshot,
        GROWTH_INDEX,
        "小芯成长",
        "成长",
        5,
        "小芯 2 岁",
        "我们一起走进大二了。"
    );
}

static void battery_level_drives_four_segment_device_detail(void) {
    xiaoxin_overview_snapshot_t snapshot;

    build_device_snapshot(XIAOXIN_BATTERY_STATE_NORMAL, 0, true, &snapshot);
    assert_card(&snapshot, DEVICE_INDEX, "设备状态", "设备", 4, "WiFi 已连接", "电量 [□□□□]");

    build_device_snapshot(XIAOXIN_BATTERY_STATE_NORMAL, 1, true, &snapshot);
    assert_card(&snapshot, DEVICE_INDEX, "设备状态", "设备", 4, "WiFi 已连接", "电量 [■□□□]");

    build_device_snapshot(XIAOXIN_BATTERY_STATE_NORMAL, 2, true, &snapshot);
    assert_card(&snapshot, DEVICE_INDEX, "设备状态", "设备", 4, "WiFi 已连接", "电量 [■■□□]");

    build_device_snapshot(XIAOXIN_BATTERY_STATE_NORMAL, 3, true, &snapshot);
    assert_card(&snapshot, DEVICE_INDEX, "设备状态", "设备", 4, "WiFi 已连接", "电量 [■■■□]");

    build_device_snapshot(XIAOXIN_BATTERY_STATE_NORMAL, 4, true, &snapshot);
    assert_card(&snapshot, DEVICE_INDEX, "设备状态", "设备", 4, "WiFi 已连接", "电量 [■■■■]");
}

static void battery_state_changes_device_detail_severity_copy(void) {
    xiaoxin_overview_snapshot_t snapshot;

    build_device_snapshot(XIAOXIN_BATTERY_STATE_LOW, 1, true, &snapshot);
    assert_card(&snapshot, DEVICE_INDEX, "设备状态", "设备", 4, "WiFi 已连接", "低电 [■□□□]");

    build_device_snapshot(XIAOXIN_BATTERY_STATE_CRITICAL, 0, true, &snapshot);
    assert_card(&snapshot, DEVICE_INDEX, "设备状态", "设备", 4, "WiFi 已连接", "即将关机 [□□□□]");
}

static void external_power_source_uses_external_detail(void) {
    const xiaoxin_overview_state_t state = {
        .network_connected = true,
        .battery_state = XIAOXIN_BATTERY_STATE_NORMAL,
        .battery_power_source = XIAOXIN_BATTERY_POWER_EXTERNAL,
    };
    xiaoxin_overview_snapshot_t snapshot;

    xiaoxin_overview_model_build(&state, &snapshot);

    assert_card(&snapshot, DEVICE_INDEX, "设备状态", "设备", 4, "WiFi 已连接", "外部供电");
}

static void unknown_power_source_uses_unknown_detail(void) {
    const xiaoxin_overview_state_t state = {
        .network_connected = true,
        .battery_state = XIAOXIN_BATTERY_STATE_NORMAL,
        .battery_power_source = XIAOXIN_BATTERY_POWER_UNKNOWN,
    };
    xiaoxin_overview_snapshot_t snapshot;

    xiaoxin_overview_model_build(&state, &snapshot);

    assert_card(&snapshot, DEVICE_INDEX, "设备状态", "设备", 4, "WiFi 已连接", "电量检测中");
}

static void battery_percent_still_does_not_drive_device_detail(void) {
    const xiaoxin_overview_state_t state = {
        .network_connected = true,
        .battery_state = XIAOXIN_BATTERY_STATE_NORMAL,
        .battery_power_source = XIAOXIN_BATTERY_POWER_BATTERY,
        .battery_percent = 100,
        .battery_level = 2,
        .battery_known = true,
    };
    xiaoxin_overview_snapshot_t snapshot;

    xiaoxin_overview_model_build(&state, &snapshot);

    assert_card(&snapshot, DEVICE_INDEX, "设备状态", "设备", 4, "WiFi 已连接", "电量 [■■□□]");
}

static void body_and_detail_are_owned_by_snapshot(void) {
    char weather_summary[] = "阵雨 24C";
    char weather_detail[] = "湿度90%";
    char course_title[] = "英语 08:00";
    char course_detail[] = "1号楼";
    char todo_detail[] = "带校卡";
    const xiaoxin_overview_state_t state = {
        .network_connected = true,
        .battery_state = XIAOXIN_BATTERY_STATE_NORMAL,
        .battery_power_source = XIAOXIN_BATTERY_POWER_BATTERY,
        .battery_level = 4,
        .battery_known = true,
        .weather_available = true,
        .weather_configured = true,
        .weather_summary = weather_summary,
        .weather_detail = weather_detail,
        .course_configured = true,
        .course_available_today = true,
        .course_title = course_title,
        .course_detail = course_detail,
        .todo_configured = true,
        .todo_count = 1,
        .todo_detail = todo_detail,
    };
    xiaoxin_overview_snapshot_t snapshot;

    xiaoxin_overview_model_build(&state, &snapshot);

    strcpy(weather_summary, "已变");
    strcpy(weather_detail, "已变");
    strcpy(course_title, "已变");
    strcpy(course_detail, "已变");
    strcpy(todo_detail, "已变");

    assert(snapshot.items[WEATHER_INDEX].body != weather_summary);
    assert(snapshot.items[WEATHER_INDEX].detail != weather_detail);
    assert(snapshot.items[COURSE_INDEX].body != course_title);
    assert(snapshot.items[COURSE_INDEX].detail != course_detail);
    assert(snapshot.items[TODO_INDEX].detail != todo_detail);
    assert_card(&snapshot, WEATHER_INDEX, "天气", "天气", 1, "阵雨 24C", "湿度90%");
    assert_card(&snapshot, COURSE_INDEX, "下一节课", "课程", 2, "英语 08:00", "1号楼");
    assert_card(&snapshot, TODO_INDEX, "今日待办", "待办", 3, "1 项待办", "带校卡");
}

static void null_state_uses_safe_defaults(void) {
    xiaoxin_overview_snapshot_t snapshot;

    xiaoxin_overview_model_build(NULL, &snapshot);
    xiaoxin_overview_model_build(NULL, NULL);

    assert(strcmp(snapshot.time_text, "--:--") == 0);
    assert(strcmp(snapshot.date_text, "时间未同步") == 0);
    assert_card(&snapshot, WEATHER_INDEX, "天气", "天气", 1, "天气未同步", "连接网络后更新");
    assert_card(&snapshot, COURSE_INDEX, "下一节课", "课程", 2, "暂无课程", "在配置中添加课表");
    assert_card(&snapshot, TODO_INDEX, "今日待办", "待办", 3, "暂无待办", "添加提醒后显示");
    assert_card(&snapshot, DEVICE_INDEX, "设备状态", "设备", 4, "离线模式", "电量检测中");
}

int main(void) {
    offline_defaults();
    connected_without_weather_location();
    rich_injected_sources();
    growth_projection_adds_a_fifth_overview_item();
    battery_level_drives_four_segment_device_detail();
    battery_state_changes_device_detail_severity_copy();
    external_power_source_uses_external_detail();
    unknown_power_source_uses_unknown_detail();
    battery_percent_still_does_not_drive_device_detail();
    body_and_detail_are_owned_by_snapshot();
    null_state_uses_safe_defaults();
    puts("xiaoxin_overview_model tests passed");
    return 0;
}
