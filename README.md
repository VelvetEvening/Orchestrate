# Orchestrate

面向 Windows x64 的便携工作台，集成日历、项目、工具和剪贴板。个人数据保存在程序目录。

[下载正式版便携包](https://github.com/VelvetEvening/Orchestrate/releases/latest) · [版本变化](CHANGELOG.md)

## 功能

| 模块 | 能做什么 |
| --- | --- |
| 日历 | 按日期记录日记、设置提醒，查看日记集合与日期标记。 |
| 项目 | 按进行中与已完成归类项目，用文件夹、重点标记和搜索整理协作记录，可绑定本地、WSL 或 SSH 目录。 |
| Git 历史 | 只读查看提交图、分支、标签、文件差异及远端分支对比。 |
| 工具 | 接入 Windows、WSL、SSH 工具，配置命令、参数和动态下拉，查看运行输出与状态。内置定时关机。 |
| 剪贴板 | 按标签页和文件夹整理文本、富文本、图片和文件路径。列表与预览随工作区宽度调整。 |
| 设置 | 预览风格与布局，从主题库启用或下载主题。可关闭侧栏与页面动效，管理全局快捷键、开机启动、托盘和更新。 |

日记、提醒和项目协作记录支持 Markdown 即时编辑与[离线公式](docs/math-preview.md)，可在主窗口与独立窗口之间切换并保留草稿。

## 文档

- [使用、数据与更新](docs/data-and-updates.md)
- [工具接入协议](docs/automation-tool-contract-v1.md) · [可运行样例](docs/examples/tool-adapter/README.md)
- [构建与发布](app/README.md) · [测试说明](app/tests/README.md)
- [架构与边界](docs/architecture.md) · [界面设计约定](DESIGN.md)
