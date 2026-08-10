#ifndef MUSEUM_STATE_H
#define MUSEUM_STATE_H

#include <string>

struct cJSON;

struct MuseumState {
    int version = 0;
    std::string request_id;
    std::string session_id;
    std::string museum_id;
    std::string zone_id;
    std::string exhibit_id;
    std::string exhibit_name;
    std::string context_source;
    std::string visitor_mode;
    std::string route_id;
    int current_stop = 0;
    int total_stops = 0;
    std::string next_exhibit_name;
    std::string prompt_title;
    std::string prompt_body;
    std::string grounding_status;
    int source_count = 0;
    int content_version = 0;
    bool can_previous = false;
    bool can_next = false;
    bool can_end = false;
};

bool ParseMuseumState(const cJSON* root, MuseumState* out, std::string* error);
std::string BuildMuseumStateDisplayText(const MuseumState& state);

#endif
