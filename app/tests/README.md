# 项目测试与隔离验收

## 当前测试入口（2026-09-30）

更新清理与任务恢复新增 `update_tasks`、`update_startup`，当前共 15 个 CTest 套件。`portable_update` 补充成功清理、失败保留、任务恢复失败保留诊断、旧更新器残留清理、其他安装目录/活动更新/链接路径拒绝、文件占用后跨启动重试及删除边界。`update_tasks` 用模拟任务验证原状态保留、只暂停本目录、部分暂停失败恢复、恢复权限失败与任务定义变更拒绝；不操作系统计划任务。

`update_startup` 在随机临时目录复制实际编译的 Orchestrate EXE，使用全新合成 SQLite 和 offscreen 平台，验证事件循环启动确认、等待旧进程正常退出、互斥锁交接、原路径启动新进程和更新器自清理。没有工具脚本，任务 API 被替代，不运行关机或操作个人数据；测试通过 Windows 事件循环退出消息正常结束测试进程。它不代替用户对原生托盘、更新按钮交互及真实任务调度的现场验收。

v1.1.0 更新机制增加三个 CTest 套件，总计 13 个：`release_info` 验证正式版本筛选、版本排序、附件命名与 URL、校验信息；`portable_update` 使用临时合成包验证数据/状态/个人配置保留、外部工具保留、旧 DLL 清理、完整备份、替换失败回滚、启动失败恢复和路径/校验拒绝；`database_schema` 验证旧结构快照、版本追踪、未来结构拒绝与迁移事务回滚。更新器测试替代计划任务检查与启动函数，不执行真实工具、计划任务或程序重启。

`ReleaseInfoTest --live-check` 是额外的联网验收入口，不加入 CTest；它通过真实 Qt Network/TLS 请求 GitHub 最新正式 Release。设置页截图覆盖更新入口和最小窗口下的滚动布局。

v1.1.1 的 `autostart` 回归覆盖不同目录开关隔离、路径大小写/分隔符/`.`/`..` 规范化、中文空格路径、同目录重开保留设置、移动后独立身份、旧启动项仅归属匹配 EXE、旧 Windows 禁用标记迁移、新项禁用决定保留及迁移失败不删除旧启动项。原生注册表测试在随机临时键下验证 REG_SZ、REG_BINARY 和旧项迁移；不更改现用正式版的登录配置。真实自动更新及重新登录由用户现场验收。

开机自动启动设置新增 `autostart` 套件，当前共 10 个套件。覆盖中文/空格路径、持久化、搬迁后更新路径、外部变更、保存失败及 Windows 禁用标记。注册表集成测试只写入随机命名的 `HKCU\Software\OrchestrateAutoStartTest-*` 临时键并清理；不操作真实 Run 或 StartupApproved 项。设置页通过注入临时 INI 存储验证点击、重开、错误提示和系统禁用入口；普通 offscreen 界面测试不访问真实开机启动配置。截图为 `settings-autostart.png`、`settings-autostart-compact.png` 和 `settings-autostart-disabled.png`。实际注销/重启登录行为未自动验收。

文字窗口与项目目录回归已通过：编辑／预览的 Ctrl＋滚轮缩放、三级标题比例、像素／点字号、连续滚轮增量、字号边界、普通滚动、选择与撤销保留、预览阅读位置、窗口间隔离及大纲窗口重开。缩放不发送正文修改信号。项目列表使用实际单击／双击事件，覆盖中文、空格、`#`、`%` 路径、项目切换、目录设置更新、未设置目录、失效目录及文件路径；通过 `QDesktopServices` URL 接收器核对打开目标，不启动真实文件管理器。10 个 CTest 套件全部通过，HeadingTextTest 为 47 passed / 0 failed。新增截图为 `project-outline-zoom-preview.png` 和 `project-outline-zoom-editor.png`；原生滚轮设备和资源管理器窗口仍属于桌面验收范围。

从仓库根目录配置并构建后，运行 `ctest --test-dir app/build-qt --output-on-failure`。六项待办及删除防护修复后，通过 9 个套件：`builtin_identity`、`database_migration`、`automation_contract`、`tool_copy`、`adapter_example`、`builtin_acceptance`、`heading_text`、`shutdown_process_identity`、`shutdown_disable`。各套件使用自己的测试目录/模拟数据；Python 样例测试仅在配置时找到 Python 3 解释器时注册，应用不依赖 Python。

新增回归：SQLite 触发器故障注入验证资料与命令一起回滚；本地/模拟 SSH 状态归属、字段与有效期验证；辅助进程模拟读取阻塞、重试、启动失败、逐字节 UTF-8 及大量输出；PowerShell 模拟进程验证精确脚本路径及旧路径接管。项目界面覆盖项目列表与工作记录双击、项目设置内的低强调删除入口、精确删除文字、取消后保留未保存设置、删除后关闭设置窗口及目录文件保留，大纲保存失败阻止两种退出路径、恢复后重试，以及数据库初始化失败禁用编辑。HeadingTextTest 当前 47 项通过；删除相关截图位于 `heading-text-test/artifacts/projects-delete-hidden.png`、`heading-text-test/artifacts/project-settings-delete.png` 和 `heading-text-test/artifacts/project-delete-confirmation.png`，自动化详细结果位于 `automation-contract-test/results.txt`。

新增的 `automation_contract` 使用专用 `automation-contract-test/data/` 和真实声明解析器，不执行声明中的程序；同时读取文档示例及内置声明，防止说明和解析器漂移。`tool_copy` 只操作专用测试目录，验证同步不覆盖 state；`adapter_example` 将本地样例复制到临时目录，检查状态写入和失败处理。详见 [审查报告](../../docs/project-review-2026-09-29.md)。

下文保留阶段性验收历史，其中的测试数量和界面说明属于对应阶段；当前功能以主 README 和接入指南为准。

## 范围与结果（2026-09-28）

Qt 6.11.2 / MinGW 13.1 环境中完成隔离全量构建，CTest 三项均通过：

| 测试 | 场景 | 结果 |
| --- | --- | --- |
| `builtin_identity` | 内置标志的 4 种组合、重复更新、重开数据库后的持久化 | 4 / 4 |
| `shutdown_disable` | 任务不存在／已停用／已启用 × 有无倒计时 × 有无排队关机 | 12 / 12 |
| `builtin_acceptance` | 下表所列的真实页面与 SQLite 集成流程 | 5 / 5 |

| 界面验收场景 | 关键断言 |
| --- | --- |
| 全新安装 | 自动注册唯一工具，创建“系统工具”并归组；显示内置来源、“暂无状态”和 8 条命令；二次启动不重复注册或建组 |
| 旧版未分组工具接管 | 从没有 `builtin` 列的合成旧库升级；保留原工具 ID；自动归组；路径跟随程序目录；刷新偏好保留 |
| 旧版已分组工具接管 | 同样升级旧库；保留自定义分组和刷新偏好，不强制归入“系统工具”；重启后保持 |
| 重读声明与重复导入 | 点击实际重读按钮、文件选择及更新确认，连续运行两轮；内置身份、分组、刷新偏好保持，命令不累积，重启后保持 |
| 删除分组后重启 | 点击实际删除分组按钮并确认，工具移入未分组；重新建立主窗口后不重建已删除的分组 |

移除保护同时检查：详情按钮隐藏、右键菜单没有“移除注册”、强制调用隐藏按钮的处理函数也不会删除内置工具。声明仍有 8 条命令，不含演示命令，倒计时持续时间设置仅有一条。

## 安全隔离

- `BuiltinAcceptanceTest` 仅允许在专用 `builtin-acceptance-test` 目录运行。
- 使用随机应用名和 Qt 测试模式，避免旧 AppData 数据迁入测试副本。
- 链接真实 `MainWindow`、`AutomationPage` 和数据库实现，但使用 offscreen 平台，不显示原生窗口和托盘图标。
- CMake 仅复制内置工具声明，测试在发现 PowerShell 脚本时拒绝运行；所有验收步骤均不启动工具命令。
- 每场景只重置专用目录的数据库和状态文件；不读取／导入用户数据库，不操作 Windows 计划任务。
- 截图及各场景的合成 SQLite 数据保存在测试目录的 `artifacts/`，重复运行会覆盖同名测试产物。
- 已检查截图中的中文、内置来源、缺少状态提示和分组结果。offscreen 不验证原生标题栏、DPI／多屏、托盘交互及真实定时关机。

## 复现

从 `app` 目录按主 README 配置并构建（默认 `BUILD_TESTING=ON`，需要 Qt Test），然后运行：

```powershell
ctest --test-dir build-qt --output-on-failure
# 仅运行界面验收
ctest --test-dir build-qt -R builtin_acceptance --output-on-failure
```

测试支持在任意独立构建目录运行，不要求使用用户的 `build-qt`。此次使用临时构建目录，未重新构建或替换用户正在使用的主程序。

## 三级标题专项测试（2026-09-28）

后续新增 `HeadingTextTest` / CTest `heading_text`，与上面的内置工具验收独立：

- 16 个语法数据场景：三级标题、多个空格、四级标记、缺少空格、制表符、缩进、正文井号、空标题、HTML、链接／图片、星号、普通记录和末尾井号。
- 格式与空白：字号递减，正文不继承标题格式，保留空行和缩进，兼容 CRLF。
- 编辑状态：反复切换预览不改变原文或光标，不清除撤销栈；富文本粘贴只保留纯文本；切换记录后预览刷新。
- 列表：使用模型原文而非 Qt 标准代理替换了换行的显示文本；窄窗口重新计算行高，长记录可逐像素滚动，选择不丢失。
- 实际页面：日记、项目大纲、工作记录的新增／修改／取消及 SQLite 原文重读；项目名保持普通字段。
- 紧凑窗口：项目正文与保存按钮不重叠，详情滚动后可到达底部操作按钮。
- 提醒：通过数据库 API 创建提醒，检查说明预览、悬浮提示和取消；后续新增的提醒保存测试见下文。
- 空白记录不允许保存。

Qt Test 汇总为 **25 passed / 0 failed**（含初始化与清理）；CTest 四个套件全部通过。测试链接实际页面，使用 offscreen 和独立 `heading-text-test/data/`，不加载自动化工具或真实用户数据。该目录下的 `artifacts/results.txt` 保存详细结果，PNG 保存界面截图。原生 Windows DPI／标题栏并非此次验证范围。

```powershell
ctest --test-dir build-qt -R heading_text --output-on-failure
```

### 后台交互回归（2026-09-28）
HeadingTextTest 新增当天选中/未选中颜色与截图、三次关闭到托盘、唤起/最小化恢复、关闭退出分支，以及 Alt+X 开关保存/重开/重试测试。离屏运行不注册系统热键，不发送全局按键；窗口唤起通过信号模拟。系统级热键冲突与前台激活、原生托盘通知仍需桌面实测。

### 日历与界面精简
新增日历实际蓝色像素检查（包括人为设置灰色非激活调色板）、键盘换日与月份导航检查；页面说明及编辑器语法教学不再常驻。截图包含 calendar-inactive、calendar-today-selected、calendar-today-unselected、settings-clean 和 projects-clean。原生 Windows 首次启动仍需用户复核；测试使用离屏窗口，不打开真实数据。

### 项目页重构回归
覆盖空名称禁用、新建/取消/项目设置、可选及不存在目录、大纲自动保存/上次选择、整行点击、标题与正文保存/取消/搜索、实际双击工作记录标题打开正文。旧结构测试在独立数据库删除 title 列后重新打开，验证迁移保留正文、ID 和时间戳，旧接口更新正文保留标题。截图为 project-headings、project-compact、work-record-editor。

### 双击大纲文字与记录标题
取消大纲页内折叠及两处“展开”按钮。回归覆盖大纲文字单击不打开、双击或回车打开、重复打开复用同一窗口、可用编辑高度、预览与撤销保留、自动保存及数据库重读、项目切换、关闭和 Esc 隐藏窗口；旧折叠设置不再让页内显示编辑器。界面截图为 project-title-entry、project-title-entry-compact 和 project-outline-window；原生 Windows 标题栏仍需桌面实测。

### 提醒保存与默认预览

在隔离库复现并修复提醒 `content` / `last_notified_at` 的 NOT NULL 失败：无说明和未通知状态写入空字符串。回归覆盖有无说明的新增、修改清空说明、数据库重开、重复提醒完成后进入下一天，以及实际页面新增和修改保存。原始失败日志保存在隔离构建的 artifacts/reminder-before-fix.txt。

日记、提醒、工作记录及大纲打开时默认预览，测试验证预览不可输入、点击实际“编辑”标签后可修改、简介及提醒字段随模式解锁、取消不改数据、大纲重开恢复预览且保留撤销记录。新建记录仍直接编辑；简介前缀在框内显示且不写入数据，模式标签高度至少 36px。已检查 work-record-preview、reminder-preview 和 diary-preview 截图。HeadingTextTest 为 34 passed / 0 failed，四组 CTest 全部通过。

### 日记简介、集合及日历标记

简介现为固定浅灰标签，与可编辑值分隔；日记和工作记录共用该字段样式。日记新增独立简介，当天列表和集合只显示简介；集合按日记日期、创建时间、ID 倒序，支持简介／日期搜索及查看后编辑，保存同步刷新。

新增测试在隔离库移除 diary_entries.title 后重开数据库，验证升级保留旧正文、日期、ID 和时间戳，旧更新接口保留新简介。验证集合跨日和同日排序、旧日记简介回退、搜索、修改后刷新及重复打开。日历验证实际日期单元格像素：方块和三角的颜色及位置互不覆盖、选中态仍可辨认、未来日期浅色且本月外保持灰淡；新增及删除日记后标记与集合即时同步。37 个 Qt Test 项通过，四个 CTest 套件通过；已检查 calendar-record-markers、diary-collection、work-record-preview 和 diary-preview 截图。

### Orchestrate 产品名称统一（2026-09-28）

文档、窗口标题、侧栏、托盘菜单、通知、快捷键提示及内置工具文案统一使用 Orchestrate。已检查日历、设置截图的完整名称显示，并核对重新编译的可执行文件和部署工具文件。实际便携数据库和定时关机状态文件的更新前后哈希一致。

新增 `database_migration` 隔离测试：不同旧应用名的数据库可复制迁入，日记简介、正文、日期及时间戳完整，旧库不被修改；已有便携库优先；多个候选旧库拒绝自动选择；当前应用名下的旧库优先于其他候选。测试使用随机组织名与 Qt 测试路径，不接触用户旧库。工具导入验收改为在文件名输入框填写绝对路径，避免依赖异步目录模型的选择时序。
