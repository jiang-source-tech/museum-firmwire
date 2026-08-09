from pathlib import Path


SOURCE = Path(
    "main/boards/waveshare/esp32-s3-touch-lcd-1.46/"
    "xiaoxin_card_pager.c"
)


def read_source() -> str:
    return SOURCE.read_text(encoding="utf-8")


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


def test_open_pages_use_lower_release_threshold_to_return_home():
    source = read_source()
    threshold_body = function_body(
        source,
        "static int16_t release_threshold_px(const xiaoxin_card_pager_t* pager)",
    )
    release_body = function_body(source, "void xiaoxin_card_pager_release(xiaoxin_card_pager_t* pager)")

    assert "static const int16_t k_return_home_threshold_percent = 8;" in source
    assert "pager->target_page == XIAOXIN_CARD_PAGE_HOME" in threshold_body
    assert "pager->current_page != XIAOXIN_CARD_PAGE_HOME" in threshold_body
    assert "(pager->screen_height * k_return_home_threshold_percent) / 100" in threshold_body
    assert "abs_i16(pager->offset_y) >= release_threshold_px(pager)" in release_body
