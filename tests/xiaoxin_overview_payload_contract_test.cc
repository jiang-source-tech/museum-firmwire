#include "xiaoxin_overview_payload_contract.h"

#include <cassert>
#include <iostream>

namespace {
XiaoxinOverviewPayloadContract ValidPayload() {
    XiaoxinOverviewPayloadContract payload;
    payload.root_object_valid = true;
    payload.type = {true, "xiaoxin_overview_update"};
    payload.version = {true, 1};
    payload.device_id = {true, "aabbcc"};
    payload.revision = {true, 2};
    payload.generated_at = {true, "2026-07-11T00:00:00Z"};
    payload.bound = {true, true};
    payload.notifications_absent = true;
    payload.weather.object_valid = true;
    payload.weather.configured = {true, true};
    payload.weather.available = {true, true};
    payload.weather.province = {true, "Zhejiang"};
    payload.weather.city = {true, "Hangzhou"};
    payload.weather.date = {true, "2026-07-11"};
    payload.weather.summary = {true, "Sunny"};
    payload.weather.detail = {true, "30 C"};
    payload.weather.fetched_at = {true, "2026-07-11T00:00:00Z"};
    payload.course.object_valid = true;
    payload.course.configured = {true, true};
    payload.course.available_today = {true, false};
    payload.course.title = {true, "No course"};
    payload.course.detail = {true, ""};
    payload.todo.object_valid = true;
    payload.todo.configured = {true, true};
    payload.todo.count = {true, 1};
    payload.todo.detail = {true, "Read"};
    return payload;
}
}  // namespace

int main() {
    assert(ValidateXiaoxinOverviewPayloadContract(ValidPayload(), "aabbcc", 1));

    XiaoxinOverviewPayloadContract with_growth = ValidPayload();
    with_growth.companion.object_present = true;
    with_growth.companion.object_valid = true;
    with_growth.companion.xiaoxin_age = {true, 2};
    with_growth.companion.academic_stage = {true, "sophomore"};
    with_growth.companion.growth_moment_id = {true, "growth-1"};
    with_growth.companion.growth_summary = {true, "We entered year two"};
    with_growth.companion.expression = {true, "growth"};
    assert(ValidateXiaoxinOverviewPayloadContract(with_growth, "aabbcc", 1));

    XiaoxinOverviewPayloadContract unknown_age = ValidPayload();
    unknown_age.companion.object_present = true;
    unknown_age.companion.object_valid = true;
    unknown_age.companion.xiaoxin_age = {true, 0, true};
    unknown_age.companion.academic_stage = {true, "unknown"};
    unknown_age.companion.growth_moment_id = {true, ""};
    unknown_age.companion.growth_summary = {true, ""};
    unknown_age.companion.expression = {true, "idle"};
    assert(ValidateXiaoxinOverviewPayloadContract(unknown_age, "aabbcc", 1));

    XiaoxinOverviewPayloadContract stale = ValidPayload();
    stale.revision.value = 1;
    XiaoxinOverviewPayloadContract wrong_device = ValidPayload();
    wrong_device.device_id.value = "other";
    XiaoxinOverviewPayloadContract notifications = ValidPayload();
    notifications.notifications_absent = false;
    XiaoxinOverviewPayloadContract invalid_utf8 = ValidPayload();
    invalid_utf8.weather.summary.value = std::string("\xC0\xAF", 2);
    XiaoxinOverviewPayloadContract long_title = ValidPayload();
    long_title.course.title.value.assign(40, 'x');
    XiaoxinOverviewPayloadContract fractional_revision = ValidPayload();
    fractional_revision.revision.valid = false;
    XiaoxinOverviewPayloadContract malformed_companion = with_growth;
    malformed_companion.companion.object_valid = false;
    XiaoxinOverviewPayloadContract invalid_age = with_growth;
    invalid_age.companion.xiaoxin_age.value = 5;
    XiaoxinOverviewPayloadContract invalid_expression = with_growth;
    invalid_expression.companion.expression.value = "celebrate";
    XiaoxinOverviewPayloadContract missing_growth_summary = with_growth;
    missing_growth_summary.companion.growth_summary.value.clear();
    XiaoxinOverviewPayloadContract null_version = ValidPayload();
    null_version.version = {true, 0, true};
    XiaoxinOverviewPayloadContract null_todo_count = ValidPayload();
    null_todo_count.todo.count = {true, 0, true};
    XiaoxinOverviewPayloadContract unknown_with_numeric_age = unknown_age;
    unknown_with_numeric_age.companion.xiaoxin_age = {true, 2};

    const XiaoxinOverviewPayloadContract invalid_cases[] = {
        stale, wrong_device, notifications, invalid_utf8, long_title,
        fractional_revision, malformed_companion, invalid_age,
        invalid_expression, missing_growth_summary, null_version,
        null_todo_count, unknown_with_numeric_age,
    };
    for (const auto& payload : invalid_cases) {
        assert(!ValidateXiaoxinOverviewPayloadContract(payload, "aabbcc", 1));
    }

    XiaoxinOverviewPayloadContract unbound = ValidPayload();
    unbound.bound.value = false;
    unbound.weather.configured.value = false;
    unbound.weather.available.value = false;
    unbound.weather.province.value.clear();
    unbound.weather.city.value.clear();
    unbound.weather.summary.value = "设备未绑定";
    unbound.weather.detail.value = "绑定后显示天气";
    unbound.weather.fetched_at.value.clear();
    unbound.course.configured.value = false;
    unbound.course.available_today.value = false;
    unbound.course.title.value = "设备未绑定";
    unbound.course.detail.value = "绑定后显示课程";
    unbound.todo.configured.value = false;
    unbound.todo.count.value = 0;
    unbound.todo.detail.value = "绑定后显示待办";
    assert(ValidateXiaoxinOverviewPayloadContract(unbound, "aabbcc", 1));
    unbound.weather.date.value = "2026-02-30";
    assert(!ValidateXiaoxinOverviewPayloadContract(unbound, "aabbcc", 1));

    std::cout << "xiaoxin overview payload contract tests passed\n";
    return 0;
}
