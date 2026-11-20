Orchestrate @VERSION@ · Windows x64

日历、项目、工具与剪贴板工作台。完整解压到可写目录，运行 Orchestrate.exe。程序随包附带所需运行库。

常用操作
- 在“设置 → 风格与布局”打开“主题库”，可直接启用内置主题，也可下载并启用扩展主题。已下载的主题可离线使用。
- 在“设置 → 风格与布局”的“界面动效”中选择“跟随系统”或“关闭”。
- 按 Alt+X 唤起或收起主窗口。在“设置 → 运行与托盘”可更换组合，改键立即保存。
- Ctrl+S 保存记录，Ctrl+滚轮调整字号。关闭窗口后默认驻留托盘，从托盘菜单可完全退出。
- 剪贴板监听默认关闭。需要自动记录时，在“剪贴板 → 设置”勾选“监听剪贴板”。

数据与更新
数据位于程序旁的 data/，内置工具状态位于 tools/<工具>/state/。
备份前从托盘完全退出，再复制整个 data/、工具状态及个人配置。剪贴板文件条目保存的是路径，原文件需单独备份。
在“设置 → 版本与更新”检查并安装新版。更新前结束运行中的工具、关机倒计时及引用程序目录的计划任务，确认程序目录及其上级可写。
升级沿用个人数据，启动失败时回滚。数据库迁移前在 data/backups/ 生成快照。手动降级时同时恢复完整程序包和对应数据库。
手动升级时完整解压新版并迁入个人数据。
从 v1.0.0、v1.1.0 或 v1.1.1 首次升级，请先参阅下方数据说明中的“内测版迁入”。

文档与来源
项目：https://github.com/VelvetEvening/Orchestrate
使用与数据：https://github.com/VelvetEvening/Orchestrate/blob/main/docs/data-and-updates.md
版本变化：https://github.com/VelvetEvening/Orchestrate/blob/main/CHANGELOG.md
源码提交与 Qt 版本见 build-info.json；第三方许可见 THIRD-PARTY-NOTICES.txt 与 licenses/。
