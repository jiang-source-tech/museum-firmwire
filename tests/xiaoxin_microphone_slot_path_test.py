import math
import re
from pathlib import Path


BOARD_SOURCE = Path(
    "main/boards/waveshare/esp32-s3-touch-lcd-1.46/"
    "esp32-s3-touch-lcd-1.46.cc"
)
BOARD_CONFIG = Path(
    "main/boards/waveshare/esp32-s3-touch-lcd-1.46/config.h"
)
SPEAKER_ENHANCER_HEADER = Path("main/audio/speaker_output_enhancer.h")
NO_AUDIO_CODEC_SOURCE = Path("main/audio/codecs/no_audio_codec.cc")
NO_AUDIO_CODEC_HEADER = Path("main/audio/codecs/no_audio_codec.h")


def read_source() -> str:
    return BOARD_SOURCE.read_text(encoding="utf-8")


def read_file(path: Path) -> str:
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


def test_waveshare_1_46_microphone_uses_right_i2s_slot():
    source = read_source()
    body = function_body(source, "virtual AudioCodec* GetAudioCodec() override")

    assert "NoAudioCodecSimplex" in body
    assert "AUDIO_I2S_MIC_GPIO_DIN" in body
    assert "AUDIO_I2S_MIC_GPIO_DIN, I2S_STD_SLOT_LEFT);" not in body
    assert "AUDIO_I2S_MIC_GPIO_DIN, I2S_STD_SLOT_RIGHT);" in body


def test_waveshare_1_46_speaker_forces_full_software_output_volume():
    source = read_source()
    body = function_body(source, "virtual AudioCodec* GetAudioCodec() override")

    assert "AUDIO_I2S_SPK_GPIO_DOUT" in body
    assert "audio_codec.SetOutputVolume(100);" in body
    assert body.index("audio_codec.SetOutputVolume(100);") > body.index("NoAudioCodecSimplex audio_codec")


def test_waveshare_1_46_speaker_applies_board_output_boost():
    source = read_source()
    body = function_body(source, "virtual AudioCodec* GetAudioCodec() override")

    assert "audio_codec.SetOutputBoost(AUDIO_OUTPUT_BOOST);" in body
    assert body.index("audio_codec.SetOutputBoost(AUDIO_OUTPUT_BOOST);") > body.index("NoAudioCodecSimplex audio_codec")


def test_waveshare_1_46_speaker_uses_aggressive_2x_output_boost_with_soft_limiter():
    config = read_file(BOARD_CONFIG)
    enhancer = read_file(SPEAKER_ENHANCER_HEADER)
    boost_match = re.search(
        r"#define\s+AUDIO_OUTPUT_BOOST\s+([0-9.]+)f",
        config,
    )
    ceiling_match = re.search(
        r"float limiter_ceiling_db = (-?[0-9.]+)f",
        enhancer,
    )

    assert boost_match is not None
    assert ceiling_match is not None

    output_boost = float(boost_match.group(1))
    limiter_ceiling_db = float(ceiling_match.group(1))
    limiter_peak = round(32767 * math.pow(10.0, limiter_ceiling_db / 20.0))

    boosted_peak_ratio = limiter_peak * output_boost / 32767

    assert output_boost == 2.0
    assert 1.05 < boosted_peak_ratio < 1.07


def test_no_audio_codec_output_boost_is_applied_before_clipping():
    source = read_file(NO_AUDIO_CODEC_SOURCE)
    header = read_file(NO_AUDIO_CODEC_HEADER)
    body = function_body(source, "int NoAudioCodec::Write(const int16_t* data, int samples)")

    assert "void SetOutputBoost(float boost)" in header
    assert "float output_boost_ = 1.0f;" in header
    assert "output_boost_" in body
    assert "volume_factor * output_boost_" in body
    assert body.index("volume_factor * output_boost_") < body.index("if (temp > INT32_MAX)")


def test_waveshare_1_46_microphone_does_not_set_input_gain_for_speaker_volume_issue():
    source = read_source()
    body = function_body(source, "virtual AudioCodec* GetAudioCodec() override")

    assert "SetInputGain" not in body


def test_standard_i2s_microphone_read_keeps_raw_scale_for_speaker_volume_issue():
    source = read_file(NO_AUDIO_CODEC_SOURCE)
    body = function_body(source, "int NoAudioCodec::Read(int16_t* dest, int samples)")

    assert "input_gain_" not in body
    assert "value = (int32_t)(value * input_gain_);" not in body
