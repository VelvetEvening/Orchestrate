# Orchestrate

Windows 桌面工作台：时序记录、项目记录和自动化工具。使用 C++17、Qt Widgets 和 SQLite，数据随程序便携保存。

## 文档入口

- [客户端构建、运行与数据说明](app/README.md)
- **[工具接入与适配指南](docs/automation-tool-contract-v1.md)**：给准备接入的工具作者和开发代理，包含字段表、路径、参数、状态、SSH、验收及排错。
- [可运行的最小适配样例](docs/examples/tool-adapter/README.md)：本地、SSH 和只读监测三种声明。
- [项目术语](CONTEXT.md)
- [2026-09-29 项目审查](docs/project-review-2026-09-29.md)

`docs/adr/` 保存设计决策，部分早期设想尚未落地。判断工具能否接入时以当前适配指南和客户端实现为准，不应把 ADR 当作已实现功能清单。

`app/tools/` 是随程序分发的内置工具源码；外部工具放在自己的目录，通过界面注册，不直接操作客户端数据库。
