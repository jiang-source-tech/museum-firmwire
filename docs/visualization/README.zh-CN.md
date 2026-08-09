# 小新功能地图可视化

这里采用“Markdown 负责叙述，YAML 负责结构，HTML 负责可视化”的文档组织方式。

## 文件

- `xiaoxin-feature-map.yaml`：功能地图的结构化数据源。
- `xiaoxin-feature-map.html`：读取 YAML 并渲染筛选视图、功能卡片、实施阶段和风险。

## 本地预览

在本目录启动静态服务：

```powershell
cd docs\visualization
python -m http.server 8765
```

然后打开：

```text
http://127.0.0.1:8765/xiaoxin-feature-map.html
```

如果直接双击 HTML，浏览器可能会阻止它读取旁边的 YAML 文件。这时页面会显示一个手动选择 YAML 的入口。

## 维护规则

- 技术背景、设计取舍、实现细节继续写在 Markdown 文档里。
- 可筛选、可统计、可视化展示的内容放进 YAML。
- HTML 只做展示，不承载新的事实来源。
- 后续新增功能方向时，优先补充 `features` 下的一项，再在 `related_docs` 指向详细 Markdown 文档。
- 后续需求如果容易被遗忘，也要写入 YAML 的 `gaps` 或 `next_steps`。例如普通通知不再弹出悬浮窗后，主页未读提示、提示音/震动分级和通知卡片时间显示仍需要作为后续事项保留在功能地图里。
