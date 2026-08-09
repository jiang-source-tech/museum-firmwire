from pathlib import Path


DOC_DIR = Path("docs/visualization")
YAML_SOURCE = DOC_DIR / "xiaoxin-feature-map.yaml"
HTML_VIEW = DOC_DIR / "xiaoxin-feature-map.html"
README = DOC_DIR / "README.zh-CN.md"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def feature_blocks(yaml: str) -> list[str]:
    return ["  - id:" + block for block in yaml.split("  - id:")[1:]]


def test_visualization_files_exist():
    assert YAML_SOURCE.exists()
    assert HTML_VIEW.exists()
    assert README.exists()


def test_yaml_keeps_markdown_as_source_and_structure_as_data():
    yaml = read(YAML_SOURCE)

    assert "source_documents:" in yaml
    assert "../xiaoxin-feature-roadmap.zh-CN.md" in yaml
    assert "features:" in yaml
    assert "phases:" in yaml
    assert "risks:" in yaml
    assert "status_legend:" in yaml


def test_yaml_contains_expected_feature_map_entries():
    yaml = read(YAML_SOURCE)

    for feature_id in [
        "notification_center",
        "dynamic_overview",
        "pet_mood",
        "local_settings",
        "web_config_panel",
        "low_power_experience",
    ]:
        assert f"  - id: {feature_id}" in yaml


def test_notification_map_tracks_non_interrupting_delivery_and_later_feedback_needs():
    yaml = read(YAML_SOURCE)

    assert "../xiaoxin-notification-delivery-policy.zh-CN.md" in yaml
    assert "真实通知中心与非打断式到达策略" in yaml
    assert "普通通知只写入通知卡片，不再创建顶部 heads-up 悬浮窗" in yaml
    assert "为主页设计不展示正文的未读状态" in yaml
    assert "通知中心卡片还缺通知发生时间" in yaml
    assert "后续为通知卡片补充通知时间显示" in yaml
    assert "提示音、震动和语音播报还缺按优先级、隐私和场景分级的策略" in yaml
    assert "后续按通知优先级评估提示音、语音和震动反馈" in yaml


def test_feature_entries_have_renderable_core_fields():
    yaml = read(YAML_SOURCE)
    blocks = feature_blocks(yaml)

    assert len(blocks) >= 6
    for block in blocks:
        assert "\n    title:" in block
        assert "\n    priority:" in block
        assert "\n    status:" in block
        assert "\n    goal:" in block
        assert "\n    implemented:" in block
        assert "\n    gaps:" in block
        assert "\n    next_steps:" in block
        assert "\n    modules:" in block
        assert "\n    tests:" in block


def test_html_reads_yaml_and_renders_dashboard_sections():
    html = read(HTML_VIEW)

    assert 'const YAML_URL = "./xiaoxin-feature-map.yaml";' in html
    assert "function parseYaml(text)" in html
    assert "renderStats(data)" in html
    assert "renderFeatures()" in html
    assert "renderPhases(data)" in html
    assert "renderRisks(data)" in html
    assert "搜索功能、模块、测试或下一步" in html


def test_html_has_refined_frontend_structure():
    html = read(HTML_VIEW)

    assert 'class="shell-header"' in html
    assert 'class="hero-row"' in html
    assert "YAML 驱动的技术文档视图" in html
    assert 'class="toolbar"' in html
    assert 'class="search-wrap"' in html
    assert 'id="featureVisibleCount"' in html
    assert 'class="progress"' in html
    assert 'class="card-top"' in html
    assert 'class="card-body"' in html
    assert "priorityClass(feature.priority)" in html


def test_html_uses_responsive_dense_tool_layout_without_marketing_hero():
    html = read(HTML_VIEW)

    assert "position: sticky;" in html
    assert "grid-template-columns: repeat(2, minmax(0, 1fr));" in html
    assert "grid-template-columns: repeat(5, minmax(0, 1fr));" in html
    assert "min-height: 510px;" in html
    assert "landing" not in html.lower()
    assert "hero image" not in html.lower()


def test_readme_documents_local_preview_path():
    readme = read(README)

    assert "python -m http.server 8765" in readme
    assert "http://127.0.0.1:8765/xiaoxin-feature-map.html" in readme
    assert "YAML 负责结构" in readme
