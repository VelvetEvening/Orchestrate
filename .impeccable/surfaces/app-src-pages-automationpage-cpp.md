---
version: 1
slug: "app-src-pages-automationpage-cpp"
primary_target: "app/src/pages/automationpage.cpp"
related_targets: ["app/src/automation/tooleditor.h","app/src/automation/discoverydialog.h","app/src/automation/tooldirectory.h","app/src/automation/clidiscovery.h","app/src/automation/commandform.h","app/src/automation/optionsdiscovery.h","app/src/automation/optionsformat.h","app/src/automation/optionssource.h"]
---

# 可配置工具与命令

范围：自动化工具接入、命令编辑、运行参数和配置替换。模式为 Operate。沿用当前 Qt Widgets 连续分区工作台，用户需快速把现成工具接入并调整账号、实例与执行时间。设置窗口提供明确保存/取消，运行窗口提供运行/取消。

## Direction contract

THESIS：以一个“接入工具”入口选择工具目录或 JSON 配置文件及本地、WSL 或 SSH 环境；默认接入整组命令及参数，自行挑选与细调按需展开，页面保持简洁。

OWN-WORLD：保留原生字体、白色正文、浅灰工作面、雾蓝选择态与现有控件；信息通过原生表单和紧凑列表组织。

STORY：选择接入方式、执行环境及目录／JSON 路径；指定配置时读取同一 v1 声明，目录无声明时从入口帮助生成命令。默认加入整组命令，menu 仅作为其中一条；需要时展开挑选和编辑。

FIRST VIEWPORT：左侧工具树，底部只有新建工具组与接入工具；右侧命令名称下显示单行简介，过长时省略号截断，悬停查看全文；下方保留最近运行输出，未配置状态时隐藏状态区。接入窗口显示接入方式、执行环境、目录／配置文件路径、一行说明与底部操作，WSL／SSH 同窗显示主机／发行版和用户；展开后显示命令清单。设置表单底部保存／取消保持可达，详细提示放在悬停说明中。

FORM：沿用用户已认可的连续分区与原生设置表单；code-led 普通功能扩展，无新方向 seed，不改全局视觉系统。

FINISH：unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance

无新增发布用位图。屏幕证据覆盖默认／紧凑工作区、目录与 JSON 接入、配置窗口、联动参数、入口及查询错误、空状态和替换预览。使用测试数据库及隔离目录／帮助读取，不操作真实排队任务。

## 已实现行为（v3.6.0）

- 注册只有“接入工具”一个按钮，同窗的“接入方式”可选“工具目录”或“JSON 配置文件”，“执行环境”可选本地 Windows、WSL 或 SSH。按环境显示主机／发行版、登录用户和路径；用户留空时读取并固定实际身份。无状态文件时隐藏状态区，已有工具设置和手动命令编辑保留。
- 目录接入初始为短表单，展示默认接入说明、“自行挑选与调整…”和底部“按默认配置接入”／“取消”。优先读取目录中的 `orchestrate-tool.json`，无声明时扫描启动入口并读取目标环境帮助。默认加入全部声明或可发现的命令及参数，menu 作为其中一条保留。展开后显示入口、读取帮助、命令清单和参数编辑，也可通过“添加命令…”直接手填，无需先成功读取帮助；已有工具的“发现更多命令…”默认展开。
- JSON 模式的路径标签为“配置文件”；本地选择任意名称的 `.json` 文件，WSL／SSH 填目标环境绝对或 `~/` 路径。先读取父目录固定实际用户和规范路径，再读取指定声明；沿用现有 v1 解析及替换预览，不自动改写声明参数。配置错误清空旧列表并禁止接入，修正后可重新读取。默认说明压为一行，来源编辑长说明移至 tooltip，列映射预览只保留简短操作提示。
- 本地及远端发现均优先采用本目录的 Python 模块与已有 venv 解释器，跳过 README 中常见安装模块示例。WSL／SSH 扫描使用固定只读 POSIX 脚本，无 Python 扫描依赖，支持 `~/`，解析并固定实际身份。远端系统解释器优先 `python3`，缺少时回退 `python`；已有 venv 优先。目录扫描异步进行，30 秒超时、1 MiB 上限。
- CANNLab 默认接入 demo、menu、accounts、import、environments、start、reset-pending 共 7 条命令及可提取参数；menu 和 start 使用外部终端，工作目录为所选目录，并补齐 `PYTHONUTF8=1`。start 提取 account、resource、minutes、interval、jitter、poll、timeout、max_attempts 共 8 项。account／resource 由通用规则关联 accounts／environments 查询；接入只读目录和帮助，不运行真实查询或修改账号数据。帮助解析仍是规则提取，完整实例值须由工具输出，不能恢复已截断的 ID。
- 动态下拉来源按明确 list／列出／查询列表帮助与参数关系唯一匹配，不按工具名硬编码。默认和高级选择接入均先读取相关查询帮助；排除明显操作、交互及已标记高风险命令，歧义保留原字段，声明和已编辑命令优先。输出默认自动识别唯一 JSON 数组、逐行列表和有列名表格；多个数组、未知字段或无列名表格需明确映射。
- 更改执行环境或目录使旧目录结果失效，更改启动入口立即清除旧命令、参数和帮助结果。读取失败显示原因，不能保存上个入口的命令；空工具组提示使用当前“接入工具”按钮名。
- 命令编辑保留粘贴现成命令、程序及独立参数项、执行方式与可展开的高级设置。“运行前填写参数”按需开启；勾选的参数在运行窗口填写，其余以默认值固定。手动编辑、启停、删除、拖动及 Ctrl+↑/↓ 排序均已实现。
- 命令名称下直接显示已有或从帮助中提取的单行简介，沿用弱化说明文字，过长时省略号截断，悬停查看全文。空白简介不占额外一行，完整说明保留在工具提示与可访问描述中；命令行的双击、右键及排序操作保持不变。
- 运行参数支持文本、整数、小数、时间、开关、下拉、文件和目录；下拉可使用固定选项、文件或查询命令，并通过上游字段联动。依赖变化清空旧选择、丢弃过期请求；读取失败显示原因且禁用运行，允许刷新重试。WSL／SSH 使用目标环境路径。
- 选项来源编辑可从工具已有查询中选取，默认自动识别格式。无列名表格通过“测试读取”展示预览，选择实际值／显示文字两列后保存并复用；运行表单来源下拉旁有“设置下拉”，沿用当前上游值测试，将映射写回命令配置。查询使用现有本地／WSL／SSH 启动通道及认证，不新增外部服务。
- 无声明的同目录重复接入追加缺失命令、补齐空白简介；仅将与当前帮助原始推断完全相同的参数升级为动态下拉，已调整字段保留。非空的自定义说明及已有名称、ID、启停和顺序保持不变，menu 继续保留。自定义配置在内置同步时保留；恢复默认或重新导入声明先展示两份配置，默认保留当前，确认后整套替换。应用内运行显示输出与退出码；外部终端只表示终端已打开，业务状态继续由工具负责。数据库仍为 schema v9，声明协议仍为 v1。

## 方向契约落实

单一入口落实用户最后确认的方向：一个窗口适配三种执行环境，并可直接选目录或 JSON 文件；默认加入整组命令，选取子集和细调按需展开。初始窗口没有常驻命令清单和长段说明；高级发现与参数编辑采用可滚动正文，保存／接入／取消等底部操作固定在滚动区之外。工作区保留浅灰索引、白色正文、雾蓝选择态、连续分区及浅灰运行输出，没有新增卡片外壳、字体、颜色或装饰效果。

本次是现有视觉系统内的普通功能扩展，沿用原生表单、预览表格和下拉控件。`DESIGN.md` 仅更新同窗选择目录／JSON 的行为事实，不新增视觉规则、token 或方向 seed；`.impeccable/design.json` 保持原样。截图是验收证据，不是发布素材；无新增发布用位图。

## 完成审查与证据

2026-11-14 本轮独立 finish review 最终结论为 **`ship`**，三项发现全部解决：来源切换遗留字段已按类型提取／保存，合并前完整校验参数防止缺依赖或成环，JSON 重读开始时清空缓存且失败后禁止保存。末次复审仅确认最后的重读缓存修复；前两项修复、JSON 选择流程及简洁文案已在前一次复审中认可。结论依据对应实现、回归和截图，不外推真实远端或业务执行。

| 场景 | `.impeccable/review/v3.6.0/` 中的证据 |
| --- | --- |
| 默认与紧凑工具工作区，无状态文件、停用命令及运行输出 | `tool-desktop.png`、`tool-compact.png` |
| 此前命令简介：默认、紧凑窗口及长简介省略 | `command-descriptions-desktop.png`、`command-descriptions-compact.png`、`command-description-long.png` |
| 工具、命令与选项来源编辑 | `tool-editor.png`、`command-editor.png`、`options-source-editor.png` |
| 默认整组接入与展开后的 7 条命令清单 | `cannlab-recommended.png`、`cannlab-discovery.png` |
| 同窗口切换 SSH／WSL 执行环境 | `directory-ssh.png`、`directory-wsl.png` |
| 改入口后读取失败与空工具组提示 | `directory-entry-error.png`、`tool-empty.png` |
| 下拉联动与查询失败反馈 | `linked-parameters.png`、`linked-error.png` |
| 通用下拉发现、已有查询选取和无列名输出选列 | `discovered-dropdowns.png`、`options-source-editor.png`、`options-column-mapping.png` |
| 简洁默认接入与 JSON 配置文件接入 | `cannlab-recommended.png`、`json-file-import.png` |
| JSON 同路径重读失败后清空旧命令 | `json-file-reread-error.png` |
| 恢复默认／重读声明前对照 | `replacement-preview.png` |

本轮 Orchestrate 和相关测试目标构建通过；`configurable_tools`、`automation_contract`、`builtin_acceptance` 三套 CTest 在末次补丁前通过。JSON 重读缓存修复后，仅重跑受影响专项及原位置启动检查，均通过，专项 **26 passed / 1 skipped**。覆盖通用发现／格式、列映射、来源切换与依赖合并、本地和模拟 WSL／SSH JSON 接入，以及重读父目录失败后的清空与禁保存。最终截图已查看，列映射及 JSON 接入无裁切，错误页显示原因且无旧命令；界面证据使用 Qt offscreen 和测试数据库。

本轮未重跑历史全 26 套件和真实 WSL，未连接真实 SSH，也未执行 CANNLab accounts／environments 查询、菜单或排队业务，未验收外部终端交互。最新构建在 `app/build-qt/Orchestrate.exe`，保留原开发数据；`Test-UpdateStartup.ps1` 通过，覆盖真实应用就绪、互斥锁交接、重启及更新器自身清理，检查使用隔离数据、模拟系统任务。`app/build-qt/directory-tools-preview/Orchestrate.exe` 仍是旧版本及独立空数据库。

审查结论不等于发布授权。v3.6.0 仍在 `feat/v3.6.0-configurable-tools` 本地开发分支，尚未提交、合入或发布；先前版本的验收与发布授权不自动延伸至本版。

实现与产品边界以 `docs/development/configurable-tools.md`、`docs/releases/v3.6.0.md`、`docs/automation-tool-contract-v1.md` 及当前源码为准；审查方向记录位于 `.impeccable/review/v3.6.0/direction.md`。
