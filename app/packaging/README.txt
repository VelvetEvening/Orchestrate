Orchestrate @VERSION@ — Windows x64 便携版

使用方法
1. 完整解压 ZIP 到当前用户可写的文件夹。
2. 双击 Orchestrate.exe。无需安装 Qt、Qt Creator、MinGW 或 Python。
3. 保留 DLL、插件目录和 tools 目录的相对位置，不要只复制 EXE。

运行环境
Windows 10 1809 或更新版本的 64 位系统；建议使用仍受支持的 Windows 版本。
内置定时关机工具使用 Windows PowerShell 与任务计划程序。
SSH 工具需要本机 OpenSSH 客户端及已配置的免交互登录；外部工具需要自己的依赖。

数据与升级
首次运行在程序旁创建 data/orchestrate.sqlite3；工具状态在 tools/<工具>/state/。
升级或备份前，从托盘菜单选择“退出 Orchestrate”，再备份整个 data 目录和工具 state 目录。
本包不包含个人数据库、工具状态、凭证或开机启动配置。
移动程序后，如需开机自动启动，请在设置页关闭后重新开启以更新路径。

窗口和后台
关闭主窗口默认驻留托盘，Alt+X 可唤起；托盘菜单可完全退出。
开机自动启动默认关闭，在“设置 → 运行与托盘”中开启。

来源
https://github.com/VelvetEvening/Orchestrate
具体源码提交、构建模式和 Qt 版本见 build-info.json。
第三方组件说明和许可文本见 THIRD-PARTY-NOTICES.txt 与 licenses/。
