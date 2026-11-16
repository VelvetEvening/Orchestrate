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

声明字段与路径规则集中在 `manifest_parser`，集成套件验证注册、持久化和执行。旧名称数据迁移与 schema 迁移、备份清理与工作目录清理、模拟启动与真实进程交接各有职责，不因名称相近而合并。

日常生命周期测试保留所有操作分支，减少重复轮数；压力模式恢复 12 次主窗口重建、1200 次页面切换和 40 轮内容生命周期。断言不变。

## 隔离与诊断

- 使用专用目录、合成数据库和模拟任务。CANNLab 接入由帮助命令夹具验证，不依赖个人工具，不运行业务查询。
- Python 样例使用 CMake 指定的解释器，子进程复用 `sys.executable`；测试不会安装 Python。
- `heading_text`、`workspace_navigation`、`configurable_tools` 默认不输出审查截图。设置 `ORCHESTRATE_TEST_ARTIFACTS=1` 后写入各自构建目录的 `artifacts/`；像素断言始终执行。
- 日志位于 `app/build-qt/Testing/Temporary/` 和各套件的 `results.txt`。CI 失败时上传日志。
- 真实 WSL 用例分别通过 `ORCHESTRATE_WSL_TEST_DISTRO`、`ORCHESTRATE_TEST_WSL` 显式选择发行版；offscreen 下的原生标题栏检查跳过。套件通过不代表这些现场场景已执行。

发布包另用 `python app/tests/verify_package.py 4.0.0 9` 验收；真实输入法、托盘、多屏和远程认证按相关改动补充检查。
