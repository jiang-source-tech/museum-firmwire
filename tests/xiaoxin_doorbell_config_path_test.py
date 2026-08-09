from pathlib import Path


OTA = Path("main/ota.cc")
OTA_HEADER = Path("main/ota.h")
CONFIG = Path("main/doorbell_config.cc")
CONFIG_HEADER = Path("main/doorbell_config.h")
CMAKE = Path("main/CMakeLists.txt")


def read_source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"function body not found: {signature}")


def test_ota_parses_overview_topic_and_persists_doorbell_config():
    ota = read_source(OTA)
    config = read_source(CONFIG)

    assert 'cJSON_GetObjectItem(root, "doorbell_mqtt")' in ota
    assert 'JsonString(root, "overview_topic")' in config
    assert 'settings.SetString("overview_topic"' in config
    assert "ParseDoorbellMqttConfig" in ota
    assert "SaveDoorbellMqttConfig" in ota


def test_version_one_wake_only_config_is_usable_without_overview_topic():
    source = read_source(CONFIG)
    usable = function_body(source, "bool DoorbellMqttConfig::IsUsable() const")

    assert "version == 1" in usable
    assert "enabled" in usable
    assert "endpoint.empty()" in usable
    assert "client_id.empty()" in usable
    assert "username.empty()" in usable
    assert "password.empty()" in usable
    assert "status_topic.empty()" in usable
    assert "notification_topic.empty()" in usable
    assert "overview_topic" not in usable


def test_unsupported_versions_are_rejected_without_persisting():
    source = read_source(CONFIG)
    parse = function_body(source, "DoorbellMqttConfig ParseDoorbellMqttConfig")
    save = function_body(source, "bool SaveDoorbellMqttConfig")

    assert "ParseDoorbellMqttConfigContract(input)" in parse
    assert 'input.version = JsonNumber(root, "version")' in parse
    assert "config.IsUsable()" in save
    assert "return false;" in save


def test_cjson_predicates_are_converted_before_bool_aggregate_initialization():
    source = read_source(CONFIG)
    json_string = function_body(source, "DoorbellJsonStringField JsonString")
    json_number = function_body(source, "DoorbellJsonNumberField JsonNumber")
    json_bool = function_body(source, "DoorbellJsonBoolField JsonBool")

    assert "cJSON_IsString(item) != 0" in json_string
    assert "cJSON_IsNumber(item) != 0" in json_number
    assert "cJSON_IsBool(item) != 0" in json_bool
    assert "cJSON_IsTrue(item) != 0" in json_bool


def doorbell_ota_block() -> str:
    body = function_body(read_source(OTA), "esp_err_t Ota::CheckVersion()")
    start = body.index('cJSON *doorbell_mqtt = cJSON_GetObjectItem(root, "doorbell_mqtt");')
    end = body.index("has_mqtt_config_ = false;")
    return body[start:end]


def test_valid_version_one_disable_erases_persisted_and_runtime_enablement():
    ota = read_source(OTA)
    config = read_source(CONFIG)
    disable = function_body(config, "void DisableDoorbellMqttConfig")
    block = doorbell_ota_block()

    assert "DoorbellMqttConfig config = ParseDoorbellMqttConfig(doorbell_mqtt);" in block
    assert "config.version != 1" in block
    assert "if (!config.enabled)" in block
    assert "DisableDoorbellMqttConfig();" in ota
    assert "doorbell_mqtt_config_ = DoorbellMqttConfig{};" in ota
    assert 'Settings settings("doorbell_mqtt", true);' in disable
    assert "settings.EraseAll();" in disable


def test_version_two_disabled_is_rejected_before_any_disable_side_effect():
    block = doorbell_ota_block()

    parse = block.index("ParseDoorbellMqttConfig(doorbell_mqtt)")
    reject = block.index("config.version != 1")
    disable = block.index("DisableDoorbellMqttConfig();")
    assert parse < reject < disable
    rejected_branch = block[reject:disable]
    assert "LoadDoorbellMqttConfig()" in rejected_branch
    assert "DisableDoorbellMqttConfig" not in rejected_branch


def test_missing_version_disabled_preserves_last_valid_configuration():
    block = doorbell_ota_block()
    rejected_branch = block[block.index("config.version != 1") : block.index("if (!config.enabled)")]

    assert "doorbell_mqtt_config_ = LoadDoorbellMqttConfig();" in rejected_branch
    assert "DisableDoorbellMqttConfig" not in rejected_branch


def test_version_two_enabled_preserves_last_valid_configuration():
    block = doorbell_ota_block()
    rejected_branch = block[block.index("config.version != 1") : block.index("if (!config.enabled)")]

    assert "doorbell_mqtt_config_ = LoadDoorbellMqttConfig();" in rejected_branch
    assert "SaveDoorbellMqttConfig" not in rejected_branch


def test_absent_object_loads_last_valid_config():
    ota = read_source(OTA)
    body = function_body(ota, "esp_err_t Ota::CheckVersion()")
    object_lookup = body.index('cJSON *doorbell_mqtt = cJSON_GetObjectItem(root, "doorbell_mqtt");')
    protocol_lookup = body.index('cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");')
    doorbell_block = body[object_lookup:protocol_lookup]

    assert "LoadDoorbellMqttConfig()" in doorbell_block
    assert "if (cJSON_IsObject(doorbell_mqtt))" in doorbell_block
    assert "else" in doorbell_block


def test_password_is_persisted_but_never_logged():
    source = read_source(CONFIG)

    assert 'settings.SetString("password", config.password);' in source
    assert 'settings.GetString("password")' in source
    for line in source.splitlines():
        if "ESP_LOG" in line:
            assert "password" not in line.lower()


def test_ota_exposes_doorbell_config_accessors():
    header = read_source(OTA_HEADER)

    assert '#include "doorbell_config.h"' in header
    assert "bool HasDoorbellMqttConfig()" in header
    assert "const DoorbellMqttConfig& GetDoorbellMqttConfig() const" in header
    assert "DoorbellMqttConfig doorbell_mqtt_config_;" in header


def test_doorbell_config_is_included_in_component_sources():
    cmake = read_source(CMAKE)
    header = read_source(CONFIG_HEADER)

    assert '"doorbell_config.cc"' in cmake
    assert "struct DoorbellMqttConfig" in header
    assert "ParseDoorbellMqttConfig" in header
    assert "SaveDoorbellMqttConfig" in header
    assert "LoadDoorbellMqttConfig" in header
    assert "DisableDoorbellMqttConfig" in header
