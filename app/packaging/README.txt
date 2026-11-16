Orchestrate @VERSION@ · Windows x64

日历、项目、工具与剪贴板工作台。完整解压到可写目录，运行 Orchestrate.exe；无需另装 Qt、Python 或 TeX。

主要功能
- 日历：日记、提醒、集合浏览。
- 项目：进行中/已完成分类、协作记录、文件夹、搜索和重点标记。
- Git：本地、WSL、SSH 仓库的只读提交图、差异和分支对比。
- 工具：目录或 JSON 接入、命令参数、动态下拉、运行输出与状态；内置定时关机。
- 剪贴板：可选监听，支持文本、富文本、图片和文件路径，按标签页和文件夹整理。
- 编辑：Markdown、离线公式、独立打开与收回、草稿保护。

常用操作
Ctrl+S 保存，Ctrl+滚轮调整字号。关闭主窗口默认驻留托盘，Alt+X 唤起；完全退出使用托盘菜单。开机自启与剪贴板监听默认关闭。

数据与更新
数据位于程序旁的 data/，内置工具状态位于 tools/<工具>/state/。
备份前完全退出，再复制整个 data/、工具状态及个人配置；文件型剪贴板只保存路径。
在“设置 → 版本与更新”检查并安装新版。保持整包文件完整，不要只替换 EXE。
更新保留个人数据，启动失败时回滚；数据库迁移前在 data/backups/ 生成快照。降级须同时恢复对应数据库。
v1.1.0/v1.1.1 内测版首次升级前需手动停用相关计划任务，完成后恢复；v1.0.0 需手动迁入旧数据。详见在线数据说明。

文档与来源
项目：https://github.com/VelvetEvening/Orchestrate
使用与数据：https://github.com/VelvetEvening/Orchestrate/blob/main/docs/data-and-updates.md
版本变化：https://github.com/VelvetEvening/Orchestrate/blob/main/CHANGELOG.md
源码提交与 Qt 版本见 build-info.json；第三方许可见 THIRD-PARTY-NOTICES.txt 与 licenses/。
