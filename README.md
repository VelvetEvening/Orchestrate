# Orchestrate

面向 Windows 的轻量桌面工作台，把日历、项目、工具和剪贴板放在一起。使用 Qt Widgets 与 SQLite，数据保存在程序目录，无需账号或云服务。

[下载正式版便携包](https://github.com/VelvetEvening/Orchestrate/releases/latest) · [v4.2.0 更新说明](CHANGELOG.md)

## 功能

| 模块 | 能做什么 |
| --- | --- |
| 日历 | 按日期记录日记、设置提醒，查看日记集合与日期标记。 |
| 项目 | 区分进行中与已完成项目；用文件夹、重点标记和搜索整理协作记录。项目可绑定本地、WSL 或 SSH 目录。 |
| Git 历史 | 查看提交图、分支、标签、文件差异及远端分支对比；只读，不执行拉取或修改仓库。 |
| 工具 | 接入 Windows、WSL、SSH 工具，编辑命令与参数、配置动态下拉、查看运行输出和工具状态；内置定时关机。 |
| 剪贴板 | 记录文本、富文本、图片和文件路径，按标签页和文件夹整理。双击复制，可选自动粘贴。 |
| 设置 | 分别选择风格与布局，下载皮肤，自定义全局快捷键，管理开机启动、托盘和更新。 |

记录支持 Markdown 即时编辑和[离线公式](docs/math-preview.md)。内容可在主窗口与独立窗口间切换，保留草稿；`Ctrl+S` 保存，`Ctrl+滚轮` 调整字号，离开时处理未保存修改。

## 开始使用

1. 下载 Windows x64 便携包，完整解压到可写目录，运行 `Orchestrate.exe`。
2. 在侧栏选择模块。工具通过“接入工具”选择目录或 JSON 声明；WSL、SSH 和外部工具需自行准备运行环境。
3. 关闭主窗口后默认留在托盘。按 `Alt+X` 在唤起和收起主窗口间切换，从托盘菜单可完全退出。开机自启和剪贴板监听默认关闭。

程序本身无需安装 Qt、Python 或 TeX。设置、升级、备份和迁移见[数据与更新](docs/data-and-updates.md)。

## 文档

- [工具接入协议](docs/automation-tool-contract-v1.md) · [可运行样例](docs/examples/tool-adapter/README.md)
- [构建与发布](app/README.md) · [测试说明](app/tests/README.md)
- [架构与边界](docs/architecture.md) · [界面设计约定](DESIGN.md)
