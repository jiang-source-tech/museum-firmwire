from pathlib import Path


BOARD_SOURCE = Path(
    "main/boards/waveshare/esp32-s3-touch-lcd-1.46/"
    "esp32-s3-touch-lcd-1.46.cc"
)


def read_source() -> str:
    return BOARD_SOURCE.read_text(encoding="utf-8")


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


def test_power_latch_is_asserted_before_display_initialization():
    source = read_source()
    constructor = function_body(source, "CustomBoard()")
    statements = [
        line.strip()
        for line in constructor.splitlines()
        if line.strip()
    ]
    early_hold = function_body(source, "void InitializePowerHoldEarly()")

    assert "InitializePowerHoldEarly();" in statements
    assert "RequirePowerOnHold();" not in constructor
    hold_idx = statements.index("InitializePowerHoldEarly();")
    i2c_idx = statements.index("InitializeI2c();")
    assert hold_idx < i2c_idx
    assert "gpio_reset_pin(PWR_BUTTON_GPIO);" in early_hold
    assert "gpio_set_direction(PWR_BUTTON_GPIO, GPIO_MODE_INPUT);" in early_hold
    assert "gpio_set_pull_mode(PWR_BUTTON_GPIO, GPIO_PULLUP_ONLY);" in early_hold
    assert "pwr_ignore_until_release_ = gpio_get_level(PWR_BUTTON_GPIO) == 0;" in early_hold
    assert "gpio_reset_pin(PWR_Control_PIN);" in early_hold
    assert "gpio_set_direction(PWR_Control_PIN, GPIO_MODE_OUTPUT);" in early_hold
    assert "gpio_set_level(PWR_Control_PIN, 1);" in early_hold
    assert early_hold.index("gpio_set_level(PWR_Control_PIN, 1);") > early_hold.index(
        "gpio_set_direction(PWR_Control_PIN, GPIO_MODE_OUTPUT);"
    )
    # Power rail stabilization delay was added
    assert "vTaskDelay(pdMS_TO_TICKS(30));" in early_hold


def test_backlight_is_restored_immediately_after_lcd_initialization():
    source = read_source()
    constructor = function_body(source, "CustomBoard()")
    statements = [
        line.strip()
        for line in constructor.splitlines()
        if line.strip()
    ]

    # Display panel init must precede the FIRST backlight operation
    display_idx = statements.index("InitializeSpd2010Display();")
    backlight_calls = [
        i for i, s in enumerate(statements)
        if "GetBacklight()" in s and i > display_idx
    ]
    assert len(backlight_calls) > 0, "Backlight must be configured after display init"

    # The first backlight call (initialization + brightness set) must be
    # before touch init. Later brightness adjustments at constructor end
    # are fine to come after.
    first_backlight_idx = backlight_calls[0]
    touch_idx = statements.index("InitializeTouch();")
    assert first_backlight_idx < touch_idx, \
        "First backlight init must happen before touch init"


def test_battery_network_start_keeps_settling_delay_without_dimming_splash():
    source = read_source()

    assert "void StartNetwork() override" in source
    start_network = function_body(source, "void StartNetwork() override")

    assert "if (on_battery_)" in start_network
    assert "vTaskDelay(pdMS_TO_TICKS(300));" in start_network
    assert "WifiBoard::StartNetwork();" in start_network
    assert start_network.index("vTaskDelay(pdMS_TO_TICKS(300));") < start_network.index(
        "WifiBoard::StartNetwork();"
    )
    assert "SetBrightness(10, false)" not in start_network
    assert "RestoreBrightness();" not in start_network
    assert "vTaskDelay(pdMS_TO_TICKS(3000));" not in start_network


def test_runtime_battery_level_reporting_uses_reliable_state_snapshot_and_boot_detection_remains():
    source = read_source()
    runtime_battery = function_body(source, "virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override")
    boot_detection = function_body(source, "void DetectPowerSourceEarly()")

    assert "battery_has_snapshot_" in runtime_battery
    assert "battery_snapshot_.percent_reliable" in runtime_battery
    assert "level = battery_snapshot_.display_percent;" in runtime_battery
    assert "charging = battery_snapshot_.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL;" in runtime_battery
    assert "discharging = battery_snapshot_.power_source == XIAOXIN_BATTERY_POWER_BATTERY;" in runtime_battery
    assert "return false;" in runtime_battery
    assert "return true;" in runtime_battery

    assert "InitializeBatteryAdc();" in boot_detection
    assert "adc_oneshot_read(" in boot_detection
    assert "on_battery_ = voltage_mv <= k_external_power_voltage_mv;" in boot_detection


def test_battery_boot_does_not_use_diagnostic_backlight_blinks():
    source = read_source()
    constructor = function_body(source, "CustomBoard()")

    assert "Initial 3 blinks" not in constructor
    assert "Delimiter blink" not in constructor
    assert "Stage 8 done" not in constructor
    assert "Stage 9 done" not in constructor
    assert "Stage 10 done" not in constructor
    assert "Stage 11 done" not in constructor
    assert "GetBacklight()->SetBrightness(0, false);" not in constructor
    assert "backlight->SetBrightness(0, false);" not in constructor


def test_battery_boot_keeps_invisible_power_settling_delays_between_stages():
    source = read_source()
    constructor = function_body(source, "CustomBoard()")
    settle = function_body(source, "void StabilizeBatteryBootStage()")

    assert "if (on_battery_)" in settle
    assert "vTaskDelay(pdMS_TO_TICKS(150));" in settle
    assert constructor.count("StabilizeBatteryBootStage();") >= 4
    assert constructor.index("InitializeTouch();") < constructor.index("StabilizeBatteryBootStage();")
    assert constructor.index("InitializeButtons();") < constructor.index(
        "StabilizeBatteryBootStage();",
        constructor.index("InitializeButtons();"),
    )
    assert constructor.index("InitializePowerSaveTimer();") < constructor.index(
        "StabilizeBatteryBootStage();",
        constructor.index("InitializePowerSaveTimer();"),
    )
    assert constructor.index("ScheduleDeferredMotionInit();") < constructor.index(
        "StabilizeBatteryBootStage();",
        constructor.index("ScheduleDeferredMotionInit();"),
    )


def test_runtime_power_button_handles_press_level_directly():
    source = read_source()
    runtime_reader = function_body(source, "bool ReadPwrButtonPressedForRuntime()")
    pwr_section = source[source.index("// Power Button") : source.index("void InitializeDebugConsole()")]

    assert "bool pwr_ignore_until_release_ = false;" in source
    assert "ReadPwrButtonPressedForRuntime()" in source
    assert "pwr_ignore_until_release_" in runtime_reader
    assert "gpio_get_level(PWR_BUTTON_GPIO)" in runtime_reader
    assert "return !gpio_get_level(PWR_BUTTON_GPIO);" not in pwr_section
    assert "RequestPowerOff();" in pwr_section
