# 测试

Windows 共 27 个 CTest 套件；套件内还有多个用例及数据行。生产代码只编译一次，测试与应用链接相同的静态库。

```powershell
# 日常功能回归；单模块修改可用 -R 选择相关套件。
ctest --test-dir app/build-qt --output-on-failure --no-tests=error

# 发布前完整压力回归；CI 自动启用。
$env:ORCHESTRATE_TEST_STRESS = '1'
ctest --test-dir app/build-qt --output-on-failure --no-tests=error
Remove-Item Env:ORCHESTRATE_TEST_STRESS
```

| 范围 | 套件 |
| --- | --- |
| 数据与内置身份 | `builtin_identity`、`database_migration`、`database_schema` |
| 工具接入 | `manifest_parser`、`automation_contract`、`configurable_tools`、`builtin_acceptance`、`adapter_example`、`tool_copy` |
| 内容与窗口 | `markdown_edit`、`math_preview`、`heading_text`、`clipboard`、`workspace_navigation`、`git_history` |
| 启动与更新界面 | `autostart`、`startup_visibility`、`release_info`、`update_checker` |
| 更新器 | `update_backup`、`portable_update`、`update_tasks`、`update_startup` |
| 定时关机 | `shutdown_process_identity`、`shutdown_result_codes`、`shutdown_task_settings`、`shutdown_disable` |

容易重叠的场景按下表定位。

| 要验证的行为 | 负责的套件与入口 |
| --- | --- |
| 工具声明是否合法 | `manifest_parser` 直接检查字段、路径和错误输入。 |
| 用户如何接入和运行工具 | `automation_contract` 从“接入工具”按钮验证注册、执行和重读。SSH 使用本地模拟程序，WSL 可选择真实发行版。`configurable_tools` 验证目录识别、默认接入、自选命令和参数配置。 |
| 切换主题和侧栏 | `workspace_navigation` 在同一窗口切换三种主题测量布局，另行重开窗口验证偏好恢复。`markdown_edit` 检查编辑器光标、字号和撤销记录。`heading_text` 检查日历符号。 |
| 草稿与窗口关闭 | `workspace_navigation` 按日记、提醒、剪贴板检查各自字段的保存与丢弃。主窗口关闭和收起到托盘共用一个草稿用例。剪贴板的设置菜单、容量与去重由 `clipboard` 验证。 |
| 发现新版本 | `heading_text` 验证主窗口的侧栏提示和托盘入口。`update_checker` 验证更新说明的延迟加载、全文滚动、空说明和外部资源处理。 |

日常生命周期测试保留所有操作分支，减少重复轮数；压力模式恢复 12 次主窗口重建、1200 次页面切换和 40 轮内容生命周期。断言不变。

## 隔离与诊断

- 使用专用目录、合成数据库和模拟任务。CANNLab 接入由帮助命令夹具验证，不依赖个人工具，不运行业务查询。
- Python 样例使用 CMake 指定的解释器，子进程复用 `sys.executable`；测试不会安装 Python。
- `heading_text`、`workspace_navigation`、`configurable_tools`、`clipboard` 和 `update_checker` 的常规界面截图按需生成。设置 `ORCHESTRATE_TEST_ARTIFACTS=1` 后写入各自构建目录的 `artifacts/`；像素断言始终执行。
- 日志位于 `app/build-qt/Testing/Temporary/` 和各套件的 `results.txt`。CI 失败时上传日志。
- 真实 WSL 用例分别通过 `ORCHESTRATE_WSL_TEST_DISTRO`、`ORCHESTRATE_TEST_WSL` 显式选择发行版；offscreen 下的原生标题栏检查跳过。套件通过不代表这些现场场景已执行。

发布包另用 `python app/tests/verify_package.py 4.0.0 9` 验收；真实输入法、托盘、多屏和远程认证按相关改动补充检查。
