#ifndef XIAOXIN_OVERVIEW_PAYLOAD_CONTRACT_H
#define XIAOXIN_OVERVIEW_PAYLOAD_CONTRACT_H

#include <string>

struct XiaoxinContractString {
    bool valid = false;
    std::string value;
};

struct XiaoxinContractBool {
    bool valid = false;
    bool value = false;
};

struct XiaoxinContractInt {
    bool valid = false;
    int value = 0;
    bool is_null = false;
};

struct XiaoxinOverviewWeatherContract {
    bool object_valid = false;
    XiaoxinContractBool configured;
    XiaoxinContractBool available;
    XiaoxinContractString province;
    XiaoxinContractString city;
    XiaoxinContractString date;
    XiaoxinContractString summary;
    XiaoxinContractString detail;
    XiaoxinContractString fetched_at;
};

struct XiaoxinOverviewCourseContract {
    bool object_valid = false;
    XiaoxinContractBool configured;
    XiaoxinContractBool available_today;
    XiaoxinContractString title;
    XiaoxinContractString detail;
};

struct XiaoxinOverviewTodoContract {
    bool object_valid = false;
    XiaoxinContractBool configured;
    XiaoxinContractInt count;
    XiaoxinContractString detail;
};

struct XiaoxinOverviewCompanionContract {
    bool object_present = false;
    bool object_valid = false;
    XiaoxinContractInt xiaoxin_age;
    XiaoxinContractString academic_stage;
    XiaoxinContractString growth_moment_id;
    XiaoxinContractString growth_summary;
    XiaoxinContractString expression;
};

struct XiaoxinOverviewPayloadContract {
    bool root_object_valid = false;
    XiaoxinContractString type;
    XiaoxinContractInt version;
    XiaoxinContractString device_id;
    XiaoxinContractInt revision;
    XiaoxinContractString generated_at;
    XiaoxinContractBool bound;
    bool notifications_absent = false;
    XiaoxinOverviewWeatherContract weather;
    XiaoxinOverviewCourseContract course;
    XiaoxinOverviewTodoContract todo;
    XiaoxinOverviewCompanionContract companion;
};

bool ValidateXiaoxinOverviewPayloadContract(
    const XiaoxinOverviewPayloadContract& payload,
    const std::string& expected_device,
    int last_revision);

#endif  // XIAOXIN_OVERVIEW_PAYLOAD_CONTRACT_H
