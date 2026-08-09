#include "xiaoxin_overview_payload_contract.h"

#include <cstddef>
#include <cstdint>

namespace {
constexpr size_t kTextMaxBytes = 192;
constexpr size_t kBodyMaxBytes = 39;
constexpr size_t kDetailMaxBytes = 63;
constexpr size_t kGrowthMomentIdMaxBytes = 64;

bool IsValidUtf8(const std::string& text) {
    size_t index = 0;
    while (index < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        if (lead <= 0x7F) {
            ++index;
            continue;
        }
        size_t continuation_count = 0;
        uint32_t code_point = 0;
        uint32_t minimum_code_point = 0;
        if ((lead & 0xE0) == 0xC0) {
            continuation_count = 1;
            code_point = lead & 0x1F;
            minimum_code_point = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            continuation_count = 2;
            code_point = lead & 0x0F;
            minimum_code_point = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            continuation_count = 3;
            code_point = lead & 0x07;
            minimum_code_point = 0x10000;
        } else {
            return false;
        }
        if (index + continuation_count >= text.size()) {
            return false;
        }
        for (size_t offset = 1; offset <= continuation_count; ++offset) {
            const unsigned char continuation =
                static_cast<unsigned char>(text[index + offset]);
            if ((continuation & 0xC0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (continuation & 0x3F);
        }
        if (code_point < minimum_code_point || code_point > 0x10FFFF ||
            (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            return false;
        }
        index += continuation_count + 1;
    }
    return true;
}

bool ValidString(const XiaoxinContractString& field, size_t max_bytes) {
    return field.valid && field.value.size() <= max_bytes &&
           IsValidUtf8(field.value);
}

bool IsValidIsoDate(const std::string& date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }
    for (size_t index = 0; index < date.size(); ++index) {
        if (index != 4 && index != 7 &&
            (date[index] < '0' || date[index] > '9')) {
            return false;
        }
    }
    const int year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 +
                     (date[2] - '0') * 10 + (date[3] - '0');
    const int month = (date[5] - '0') * 10 + (date[6] - '0');
    const int day = (date[8] - '0') * 10 + (date[9] - '0');
    if (year == 0 || month < 1 || month > 12) {
        return false;
    }
    constexpr int days_by_month[] =
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    const int maximum = days_by_month[month - 1] + (month == 2 && leap ? 1 : 0);
    return day >= 1 && day <= maximum;
}

bool ValidateCards(const XiaoxinOverviewPayloadContract& payload) {
    const auto& weather = payload.weather;
    const auto& course = payload.course;
    const auto& todo = payload.todo;
    return weather.object_valid && course.object_valid && todo.object_valid &&
           weather.configured.valid && weather.available.valid &&
           ValidString(weather.province, kTextMaxBytes) &&
           ValidString(weather.city, kTextMaxBytes) &&
           ValidString(weather.date, kTextMaxBytes) &&
           ValidString(weather.summary, kBodyMaxBytes) &&
           ValidString(weather.detail, kDetailMaxBytes) &&
           ValidString(weather.fetched_at, kTextMaxBytes) &&
           course.configured.valid && course.available_today.valid &&
           ValidString(course.title, kBodyMaxBytes) &&
           ValidString(course.detail, kDetailMaxBytes) &&
           todo.configured.valid && todo.count.valid && !todo.count.is_null &&
           todo.count.value >= 0 &&
           todo.count.value <= 99 &&
           ValidString(todo.detail, kDetailMaxBytes);
}

bool ValidateCompanion(const XiaoxinOverviewCompanionContract& companion) {
    if (!companion.object_present) {
        return true;
    }
    if (!companion.object_valid || !companion.xiaoxin_age.valid ||
        !ValidString(companion.academic_stage, 16) ||
        !ValidString(companion.growth_moment_id, kGrowthMomentIdMaxBytes) ||
        !ValidString(companion.growth_summary, kDetailMaxBytes) ||
        !ValidString(companion.expression, 16)) {
        return false;
    }
    const std::string& stage = companion.academic_stage.value;
    if (stage != "unknown" && stage != "freshman" && stage != "sophomore" &&
        stage != "junior" && stage != "senior") {
        return false;
    }
    const std::string& expression = companion.expression.value;
    if (expression != "idle" && expression != "growth") {
        return false;
    }
    if (companion.xiaoxin_age.is_null) {
        return stage == "unknown" && expression == "idle" &&
               companion.growth_moment_id.value.empty() &&
               companion.growth_summary.value.empty();
    }
    const int expected_age = stage == "freshman" ? 1 :
                             stage == "sophomore" ? 2 :
                             stage == "junior" ? 3 :
                             stage == "senior" ? 4 : 0;
    if (companion.xiaoxin_age.value != expected_age) {
        return false;
    }
    if (expression == "growth") {
        return companion.xiaoxin_age.value >= 1 &&
               !companion.growth_moment_id.value.empty() &&
               !companion.growth_summary.value.empty();
    }
    return companion.growth_moment_id.value.empty() &&
           companion.growth_summary.value.empty();
}

bool ValidateUnbound(const XiaoxinOverviewPayloadContract& payload) {
    const auto& weather = payload.weather;
    const auto& course = payload.course;
    const auto& todo = payload.todo;
    return !weather.configured.value && !weather.available.value &&
           weather.province.value.empty() && weather.city.value.empty() &&
           IsValidIsoDate(weather.date.value) && weather.fetched_at.value.empty() &&
           weather.summary.value == "设备未绑定" &&
           weather.detail.value == "绑定后显示天气" &&
           !course.configured.value && !course.available_today.value &&
           course.title.value == "设备未绑定" &&
           course.detail.value == "绑定后显示课程" &&
           !todo.configured.value && todo.count.value == 0 &&
           todo.detail.value == "绑定后显示待办";
}
}  // namespace

bool ValidateXiaoxinOverviewPayloadContract(
    const XiaoxinOverviewPayloadContract& payload,
    const std::string& expected_device,
    int last_revision) {
    if (!payload.root_object_valid || expected_device.empty() ||
        !ValidString(payload.type, 32) ||
        payload.type.value != "xiaoxin_overview_update" ||
        !payload.version.valid || payload.version.is_null ||
        payload.version.value != 1 ||
        !ValidString(payload.device_id, 128) ||
        payload.device_id.value != expected_device ||
        !payload.revision.valid || payload.revision.is_null ||
        payload.revision.value <= last_revision ||
        payload.revision.value < 1 ||
        !ValidString(payload.generated_at, 64) || !payload.bound.valid ||
        !payload.notifications_absent || !ValidateCards(payload) ||
        !ValidateCompanion(payload.companion)) {
        return false;
    }
    return payload.bound.value || ValidateUnbound(payload);
}
