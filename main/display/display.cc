#include <esp_log.h>
#include <esp_err.h>
#include <string>
#include <cstdlib>
#include <cstring>
#include <font_awesome.h>

#include "display.h"
#include "board.h"
#include "application.h"
#include "audio_codec.h"
#include "settings.h"
#include "assets/lang_config.h"

#define TAG "Display"

Display::Display() {
}

Display::~Display() {
}

void Display::SetStatus(const char* status) {
    ESP_LOGW(TAG, "SetStatus: %s", status);
}

void Display::ShowNotification(const std::string &notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void Display::ShowNotification(const char* notification, int duration_ms) {
    ESP_LOGW(TAG, "ShowNotification: %s", notification);
}

bool Display::UpsertNotification(
    const char* id,
    const char* title,
    const char* body,
    const char* tag,
    uint32_t priority,
    uint32_t ttl_ms,
    const char* event_type
) {
    ESP_LOGW(
        TAG,
        "UpsertNotification ignored: id=%s event_type=%s title=%s body=%s tag=%s priority=%lu ttl=%lu",
        id != nullptr ? id : "",
        event_type != nullptr ? event_type : "",
        title != nullptr ? title : "",
        body != nullptr ? body : "",
        tag != nullptr ? tag : "",
        (unsigned long)priority,
        (unsigned long)ttl_ms
    );
    return false;
}

bool Display::RemoveNotification(const char* id) {
    ESP_LOGW(TAG, "RemoveNotification ignored: id=%s", id != nullptr ? id : "");
    return false;
}

void Display::UpdateStatusBar(bool update_all) {
}


void Display::SetEmotion(const char* emotion) {
    ESP_LOGW(TAG, "SetEmotion: %s", emotion);
}

void Display::SetMuseumState(const char* text) {
    SetChatMessage("system", text != nullptr ? text : "");
}

void Display::SetChatMessage(const char* role, const char* content) {
    ESP_LOGW(TAG, "Role:%s", role);
    ESP_LOGW(TAG, "     %s", content);
}

void Display::UpdateChatMessage(const char* role, const char* content) {
    SetChatMessage(role, content);
}

void Display::ClearChatMessages() {
    // Default empty implementation, override in subclasses if needed
}

void Display::SetTheme(Theme* theme) {
    current_theme_ = theme;
    Settings settings("display", true);
    settings.SetString("theme", theme->name());
}

void Display::SetPowerSaveMode(bool on) {
    ESP_LOGW(TAG, "SetPowerSaveMode: %d", on);
}
