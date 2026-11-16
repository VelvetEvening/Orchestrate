# 工具接入样例

复制本目录到工具目录，使用 Python 3.9+，无第三方依赖。样例只写自己的 `state/current.json`，不联网、不创建计划任务。

```powershell
python -u adapter.py run --message "你好 Orchestrate" --count 2
python -u adapter.py fail
```

第一条应退出 0 并写入 `success`，第二条退出 1 并写入 `failure`。从其他工作目录运行时，状态仍写在脚本旁。

在“接入工具”中选择执行环境及 JSON 配置文件：

| 声明 | 用途 |
| --- | --- |
| `orchestrate-tool.json` | 本地执行，建议把 executable 改为解释器绝对路径。 |
| `orchestrate-tool.ssh.json` | 先自行上传文件并修改远端工作目录，再填写 SSH 主机与用户。 |
| `orchestrate-tool.monitor.json` | 只读取已有状态；先从外部运行一次脚本。 |

三份声明共用 `id`：同一执行目标内重复导入会更新已有工具。需要多实例时同时修改 `id`、状态路径和上报的 `tool_id`。

适配时替换示例业务，保留参数校验、失败状态和原子写入。调度、凭证、业务锁及有效期由工具负责。字段及动态下拉见[接入协议](../../automation-tool-contract-v1.md)。
