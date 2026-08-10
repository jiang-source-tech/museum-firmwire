#include "museum_state.h"

#include <cJSON.h>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <utility>

namespace {

bool RequiredString(const cJSON* object, const char* key, std::string* out) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(value) || value->valuestring == nullptr) {
        return false;
    }
    *out = value->valuestring;
    return true;
}

bool OptionalString(const cJSON* object, const char* key, std::string* out) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (value == nullptr) {
        out->clear();
        return true;
    }
    return RequiredString(object, key, out);
}

bool RequiredObject(const cJSON* parent, const char* key, const cJSON** out) {
    *out = cJSON_GetObjectItemCaseSensitive(parent, key);
    return cJSON_IsObject(*out);
}

bool RequiredNumber(const cJSON* object, const char* key, int* out) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(value)) {
        return false;
    }
    const double number = value->valuedouble;
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < std::numeric_limits<int>::min() ||
        number > std::numeric_limits<int>::max()) {
        return false;
    }
    *out = static_cast<int>(number);
    return true;
}

bool NullableNumber(const cJSON* object, const char* key, int* out) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNull(value)) {
        *out = 0;
        return true;
    }
    return RequiredNumber(object, key, out);
}

bool RequiredBool(const cJSON* object, const char* key, bool* out) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsBool(value)) {
        return false;
    }
    *out = cJSON_IsTrue(value);
    return true;
}

bool Fail(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

bool IsAllowedValue(
    const std::string& value,
    std::initializer_list<const char*> allowed) {
    for (const char* candidate : allowed) {
        if (value == candidate) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool ParseMuseumState(const cJSON* root, MuseumState* out, std::string* error) {
    if (out == nullptr || !cJSON_IsObject(root)) {
        return Fail(error, "root_not_object");
    }
    const cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || std::string(type->valuestring) != "museum_state") {
        return Fail(error, "invalid_type");
    }

    MuseumState parsed;
    if (!RequiredNumber(root, "version", &parsed.version) || parsed.version != 1) {
        return Fail(error, "unsupported_version");
    }
    if (!RequiredString(root, "request_id", &parsed.request_id) ||
        !RequiredString(root, "session_id", &parsed.session_id)) {
        return Fail(error, "missing_request_identity");
    }

    const cJSON* context = nullptr;
    const cJSON* journey = nullptr;
    const cJSON* prompt = nullptr;
    const cJSON* grounding = nullptr;
    const cJSON* navigation = nullptr;
    if (!RequiredObject(root, "context", &context) ||
        !RequiredObject(root, "journey", &journey) ||
        !RequiredObject(root, "prompt", &prompt) ||
        !RequiredObject(root, "grounding", &grounding) ||
        !RequiredObject(root, "navigation", &navigation)) {
        return Fail(error, "missing_state_section");
    }
    if (!RequiredString(context, "museum_id", &parsed.museum_id) ||
        !RequiredString(context, "zone_id", &parsed.zone_id) ||
        !RequiredString(context, "exhibit_id", &parsed.exhibit_id) ||
        !RequiredString(context, "exhibit_name", &parsed.exhibit_name) ||
        !RequiredString(context, "source", &parsed.context_source) ||
        !RequiredString(root, "visitor_mode", &parsed.visitor_mode) ||
        !RequiredString(journey, "route_id", &parsed.route_id) ||
        !RequiredNumber(journey, "current_stop", &parsed.current_stop) ||
        !RequiredNumber(journey, "total_stops", &parsed.total_stops) ||
        !OptionalString(journey, "next_exhibit_name", &parsed.next_exhibit_name) ||
        !RequiredString(prompt, "title", &parsed.prompt_title) ||
        !RequiredString(prompt, "body", &parsed.prompt_body) ||
        !RequiredString(grounding, "status", &parsed.grounding_status) ||
        !RequiredNumber(grounding, "source_count", &parsed.source_count) ||
        !NullableNumber(grounding, "content_version", &parsed.content_version) ||
        !RequiredBool(navigation, "can_previous", &parsed.can_previous) ||
        !RequiredBool(navigation, "can_next", &parsed.can_next) ||
        !RequiredBool(navigation, "can_end", &parsed.can_end)) {
        return Fail(error, "invalid_state_field");
    }
    if (parsed.current_stop < 0 || parsed.total_stops < 0 ||
        parsed.current_stop > parsed.total_stops || parsed.source_count < 0 ||
        parsed.content_version < 0) {
        return Fail(error, "invalid_state_range");
    }
    if (!IsAllowedValue(parsed.visitor_mode, {"general", "family", "deep"})) {
        return Fail(error, "invalid_visitor_mode");
    }
    if (!IsAllowedValue(
            parsed.grounding_status,
            {"ready", "retrieving", "grounded", "unsupported", "missing_context"})) {
        return Fail(error, "invalid_grounding_status");
    }
    *out = std::move(parsed);
    return true;
}

std::string BuildMuseumStateDisplayText(const MuseumState& state) {
    if (state.grounding_status == "missing_context") {
        return "请先选择展品\n" + state.prompt_body;
    }

    std::string text = state.exhibit_name;
    if (state.grounding_status == "ready") {
        text += "\n可以开始提问";
    } else if (state.grounding_status == "retrieving") {
        text += "\n正在查阅馆方资料...";
    } else if (state.grounding_status == "grounded") {
        text += "\n有依据 · " + std::to_string(state.source_count) + " 个来源";
    } else if (state.grounding_status == "unsupported") {
        text += "\n馆方资料暂未覆盖";
    }

    if (!state.prompt_body.empty() && state.grounding_status != "retrieving") {
        text += "\n" + state.prompt_body;
    }
    return text;
}
