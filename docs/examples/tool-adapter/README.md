# 工具接入最小样例

先读 [接入指南](../../automation-tool-contract-v1.md)。此样例只写自身目录内的 `state/current.json`，不联网、不操作系统任务。复制整个目录到自己的工具目录，使用 Python 3.9 或更新版本，无需安装第三方库。

```powershell
python -u adapter.py run --message "你好 Orchestrate" --count 2
python -u adapter.py fail
```

第一条命令应退出 0，状态为 `success`；第二条应退出 1，状态为 `failure`。失败也是验收的一部分。从其他工作目录通过绝对路径运行脚本，状态仍写在脚本旁的 `state` 目录。

- Windows：先确认 `python --version` 可用，再通过“注册本地工具”选择 `orchestrate-tool.json`。正式工具建议把两条命令的 `executable` 改成实际 Python/虚拟环境解释器的绝对路径，避免桌面进程 PATH 与终端不同。
- SSH：将脚本和 `orchestrate-tool.ssh.json` 上传到服务器，修改 `working_directory` 为真实部署路径（如果需要，也修改 `executable`），通过“注册服务器工具”输入 SSH 别名及声明的绝对路径。客户端不会自动上传文件。
- 只读监测：先在外部执行一次脚本，再导入 `orchestrate-tool.monitor.json`；手动刷新只读取已有文件，不执行脚本。

这三份声明故意使用相同的 `id`：同一 SSH 主机标识下是同一个工具的不同接入方式，重复导入会更新注册。要同时保留不同实例，必须为每个实例分配不同 `id`、状态路径，并同步修改适配器上报的 `tool_id`。

适配自己的工具时，替换 `main()` 中的示例业务，保留参数校验、异常写失败状态和原子替换逻辑。计划调度、凭证、跨进程锁、心跳及业务过期规则由实际工具实现；这个短任务样例没有提供这些能力，也不应直接用作多写入进程的状态存储方案。
