# 测试

生产代码只编译一次，测试与应用链接相同的静态库。用 `ctest --test-dir app/build-qt -N` 查看当前构建的套件。

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
| 文本与内容 | `markdown_edit`、`math_preview`、`clipboard`、`git_history` |
| 主窗口与设置 | `workspace_navigation` 验证导航、草稿、皮肤管理及旧偏好迁移。`heading_text` 验证编辑、设置控件和托盘入口。 |
| 皮肤文件与绘制 | `skin_library` 验证目录、安装、取消和损坏包。`theme_rendering` 检查浅深色下的实际绘制。 |
| Windows 集成 | `autostart`、`global_shortcut`、`startup_visibility`。快捷键占用与回滚在私有桌面验证。 |
| 更新检查 | `release_info` 验证版本与资产，`update_checker` 验证查询和更新界面。 |
| 更新器 | `update_backup`、`portable_update`、`update_tasks`、`update_startup` |
| 定时关机 | `shutdown_process_identity`、`shutdown_result_codes`、`shutdown_task_settings`、`shutdown_disable` |

压力模式使用相同断言，增加窗口重建、页面切换和内容生命周期的重复次数。

## 隔离与诊断

- 使用专用目录、合成数据库和模拟任务。CANNLab 接入由帮助命令夹具验证，不依赖个人工具，不运行业务查询。
- Python 样例使用 CMake 指定的解释器，子进程复用 `sys.executable`；测试不会安装 Python。
- `heading_text`、`workspace_navigation`、`configurable_tools`、`clipboard` 和 `update_checker` 的常规界面截图按需生成。设置 `ORCHESTRATE_TEST_ARTIFACTS=1` 后写入各自构建目录的 `artifacts/`；像素断言始终执行。
- 日志位于 `app/build-qt/Testing/Temporary/` 和各套件的 `results.txt`。CI 失败时上传日志。
- 真实 WSL 用例分别通过 `ORCHESTRATE_WSL_TEST_DISTRO`、`ORCHESTRATE_TEST_WSL` 显式选择发行版；offscreen 下的原生标题栏检查跳过。套件通过不代表这些现场场景已执行。

发布包的验收命令见[构建与发布](../README.md#发布)。真实输入法、托盘、多屏和远程认证按相关改动补充检查。
