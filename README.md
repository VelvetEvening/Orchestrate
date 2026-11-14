# Orchestrate

Windows 桌面工作台：时序记录、项目记录、自动化工具和剪贴板。使用 C++17、Qt Widgets 和 SQLite，数据随程序便携保存。

v3.6.1 放大剪贴板内容字体，采用相连叠压的标签页，并补齐编辑页的 ↑ 到文首／↓ 到文末，详见[版本说明](docs/releases/v3.6.1.md)。

## 文档入口

- [v3.6.1 剪贴板阅读与编辑导航](docs/releases/v3.6.1.md)
- [客户端构建、运行与数据说明](app/README.md)
- **[工具接入与适配指南](docs/automation-tool-contract-v1.md)**：统一 v1 最小 JSON 模板、字段、动态下拉、运行环境和验收。
- [每个工具的 README 清单](docs/automation-tool-contract-v1.md#13-每个工具-readme-应写什么)：入口、依赖、参数、查询关联、完整值和输出示例，以及现有 v1 声明写法。
- [可运行的最小适配样例](docs/examples/tool-adapter/README.md)：本地、SSH 和只读监测三种声明。
- [项目术语](CONTEXT.md)
- [v3.6.0 可配置工具与命令](docs/releases/v3.6.0.md)
- [v3.5.6 统一内容编辑](docs/releases/v3.5.6.md)
- [v3.5.5 协作记录直接编辑](docs/releases/v3.5.5.md)
- [v3.5.0 Qt 原生即时编辑开发说明](docs/development/native-markdown.md)
- [v3.4.0 内容导航开发说明](docs/development/content-navigation.md)
- [v3.4.1 发布说明](docs/releases/v3.4.1.md)
- [v3.4.0 发布说明](docs/releases/v3.4.0.md)
- [v3.3.0 开发说明与检查步骤](docs/development/git-history.md)
- [v3.3.0 发布说明](docs/releases/v3.3.0.md)
- [v3.2.2 发布说明](docs/releases/v3.2.2.md)
- [连续分区与独立顶栏的实现及验收](docs/development/seamless-workspace.md)
- [v3.2.1 稳定性与后台资源审查](docs/project-review-2026-11-11-layout.md)
- [记录中的公式写法与示例](docs/math-preview.md)
- [2026-11-10 公式预览与资源回归](docs/project-review-2026-11-10.md)
- [2026-11-11 日常操作与后台资源审查](docs/project-review-2026-11-11.md)
- [2026-11-09 工具组折叠无响应排查](docs/project-review-2026-11-09.md)
- [2026-10-21 崩溃排查与资源测量](docs/project-review-2026-10-21.md)
- [2026-10-15 发布前审查](docs/project-review-2026-10-15.md)
- [2026-09-29 项目审查](docs/project-review-2026-09-29.md)

`docs/adr/` 保存设计决策，部分早期设想尚未落地。判断工具能否接入时以当前适配指南和客户端实现为准，不应把 ADR 当作已实现功能清单。

`app/tools/` 是随程序分发的内置工具源码；外部工具放在自己的目录，通过界面注册，不直接操作客户端数据库。
