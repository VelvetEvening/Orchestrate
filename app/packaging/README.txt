Orchestrate @VERSION@ · Windows x64

日历、项目、工具与剪贴板工作台。完整解压到可写目录，运行 Orchestrate.exe；无需另装 Qt、Python 或 TeX。

常用操作
- 在“设置 → 风格切换”选择配色与布局密度。
- Ctrl+S 保存记录，Ctrl+滚轮调整字号。关闭窗口后默认驻留托盘，按 Alt+X 唤起，从托盘菜单完全退出。
- 剪贴板监听默认关闭。需要自动记录时，在“剪贴板 → 设置”勾选“监听剪贴板”。

数据与更新
数据位于程序旁的 data/，内置工具状态位于 tools/<工具>/state/。
备份前完全退出，再复制整个 data/、工具状态及个人配置；文件型剪贴板只保存路径。
在“设置 → 版本与更新”检查并安装新版。保持整包文件完整，不要只替换 EXE。
更新保留个人数据，启动失败时回滚；数据库迁移前在 data/backups/ 生成快照。降级须同时恢复对应数据库。
从 v1.0.0、v1.1.0 或 v1.1.1 首次升级，请先参阅下方数据说明中的“内测版迁入”。

文档与来源
项目：https://github.com/VelvetEvening/Orchestrate
使用与数据：https://github.com/VelvetEvening/Orchestrate/blob/main/docs/data-and-updates.md
版本变化：https://github.com/VelvetEvening/Orchestrate/blob/main/CHANGELOG.md
源码提交与 Qt 版本见 build-info.json；第三方许可见 THIRD-PARTY-NOTICES.txt 与 licenses/。
