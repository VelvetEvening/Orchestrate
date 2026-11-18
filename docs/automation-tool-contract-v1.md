# 工具接入协议 v1

工具自行管理业务、依赖、凭证、调度和日志。Orchestrate 负责接入、启动命令、显示参数表单和运行状态。[可运行样例](examples/tool-adapter/README.md)

## 接入与声明

“接入工具”支持 Windows、WSL、SSH。选择目录时优先读取 `orchestrate-tool.json`；没有声明则读取入口与 `--help`，生成可编辑配置。也可直接选择任意名称的 JSON 文件。

```json
{
  "schema": "orchestrate-tool/v1",
  "id": "example-tool",
  "name": "示例工具",
  "target_type": "windows-local",
  "state_path": "state/current.json",
  "commands": [{
    "name": "运行",
    "description": "生成示例结果",
    "executable": "python",
    "arguments": ["adapter.py", "run"]
  }]
}
```

使用 UTF-8 JSON，不写注释或尾随逗号。以下字段均在声明顶层：

| 字段 | 规则 |
| --- | --- |
| `schema` | 必填，固定 `orchestrate-tool/v1`。 |
| `id`、`name` | 必填非空字符串；id 稳定，name 是显示名。 |
| `target_type` | 必填，`windows-local`、`wsl` 或 `ssh`，须匹配接入入口。 |
| `description` | 可选用途说明。 |
| `group` | 可选，匹配已有工具组，不自动建组；首次接入时当前所选组优先。 |
| `working_directory` | 可选，默认声明所在目录。 |
| `state_path` | 可选，省略或留空则不读取、展示状态。 |
| `environment` | 可选字符串值对象，如 `{"PYTHONUTF8":"1"}`，覆盖目标进程环境。 |
| `commands` | 可选对象数组；缺省或 `[]` 表示无可执行命令。 |

主机、发行版和登录用户在接入窗口填写，不由 JSON 覆盖。未知字段不会自动启用调度、产物管理或内置身份。

**身份与更新：**工具按 `id` 加执行目标识别；SSH 目标包括主机别名和实际用户，WSL 包括发行版和实际用户。同一目标内不同目录的相同 id 仍是同一工具。重复导入声明经预览确认后整套替换命令，保存失败回滚；分组和内置身份保留。

无声明的同目录重复接入则追加缺失命令、补齐空白简介，保留自定义名称、说明、参数、启停和顺序。刷新状态不会重新读取声明。

## 路径与运行环境

| 项目 | Windows | WSL | SSH |
| --- | --- | --- | --- |
| 相对工作目录 | 相对声明目录 | 相对声明目录 | 不允许，须为绝对路径 |
| 相对状态路径 | 相对声明目录 | 相对声明目录 | 相对工作目录 |
| 命令相对路径 | 由程序按工作目录解释 | 同左，使用 Linux 路径 | 同左，使用 Linux 路径 |
| 程序入口 | 建议解释器绝对路径 | Linux 程序；本目录程序写 `./tool` | 远端程序；本目录程序写 `./tool` |

接入窗口的远端路径支持 `~/`，读取后保存规范路径与实际用户名。声明字段不展开 `~` 或环境变量；Linux 路径使用 `/`，不用 Windows 盘符或反斜杠。

WSL 需已安装目标发行版及工具依赖。SSH 需本机 OpenSSH 和可用的非交互认证；下拉仅列默认 SSH 配置直接声明的 Host，也可手动输入。客户端不上传文件、不激活虚拟环境、不安装依赖、不创建账号。状态读取可能唤醒 WSL。

## 命令与参数

| 命令字段 | 规则 |
| --- | --- |
| `name`、`executable` | 必填非空字符串；executable 仅放程序或解释器。 |
| `description` | 可选，一句话说明用途，显示在命令名下。 |
| `arguments` | 字符串数组，每项一个参数；缺省 `[]`。 |
| `parameters` | 可选参数定义数组，见下表。 |
| `enabled` | 布尔值，默认 true。 |
| `interactive` | 布尔值，默认 false；true 使用独立终端。 |
| `high_risk` | 布尔值，默认 false；高风险命令执行前需确认。 |

参数名通过 `{key}` 引用，例如 `"arguments":["run.py","--count","{count}"]`：

| 参数字段 | 规则 |
| --- | --- |
| `key` | 必填、命令内唯一；英文字母、数字、`_`、`.`、`-`。 |
| `type` | 默认 `string`；还支持 `integer`、`number`、`time`、`choice`、`boolean`、`readonly`、`file`、`directory`。 |
| `label`、`description` | 显示名与说明；label 默认使用 key。 |
| `default` | 与类型一致的默认值。 |
| `required` | 布尔值；readonly 不可必填，也不可作为命令参数。 |
| `state_key` | 状态对象的点分路径；存在且非 null 时优先于 default，不支持数组下标。 |
| `placeholder` | string 的输入提示或 readonly 的缺值文字。 |
| `min`、`max`、`step`、`unit` | 数值类型；默认 0、1000000、1、无单位，单位不传入命令。 |
| `options` | choice 的非空数组；标量或 `{"value":"a","label":"名称","description":"说明"}`，值不能重复。 |
| `options_state_key` | 从状态读取 choice 数组的点分路径。 |
| `options_source` | 从文件或命令读取 choice，见下一节。三种选项来源互斥。 |
| `text`、`true_value`、`false_value` | boolean 的标签与参数映射；映射默认 `"true"`、`"false"`。 |
| `flag`、`omit_empty` | 如 `"flag":"--name","omit_empty":true`，空值时连同紧邻的前置 flag 一起省略。 |

替换规则：

- 每个数组元素仍是一个参数；不要给含空格的值额外加引号，默认不经过 shell。
- 只替换 arguments，且只替换一次；不修改程序、路径或环境字段。未定义的占位符原样保留。
- 整项 `{key}` 为空时移除该项；`--name={key}` 为空时仍传 `--name=`。固定空字符串保留。
- 未设置匹配的 flag/omit_empty 时，空值不会移除前面的 `--name`。
- string 裁剪首尾空白，time 输出 `HH:mm`，choice 传 value，boolean 传映射值。
- 动态 choice 失效时清空选择，必须重新选择；带 state_key 的 choice 缺值时不退回 default 或首项。

脚本须显式使用解释器，例如 PowerShell 加 `-File`，Python 加脚本路径。客户端不会替你处理 shell 运算符或激活 Conda。命令行和输出会展示，勿放入凭证；业务权限仍由工具核验。

同一工具在一个客户端内同时运行一条命令，不代替业务锁。外部终端“已打开”不代表业务完成；应用内没有通用停止按钮或任务调度。最近运行保留末尾 256 Ki 个 UTF-16 代码单元，完整日志由工具保存。

## 动态下拉

文件来源使用 `kind: "file"`、`path`；命令来源使用 `kind: "command"`、`executable`、`arguments`。来源沿用工具的执行环境和工作目录：

```json
{"key":"instance","type":"choice","required":true,
 "options_source":{"kind":"command","executable":"python",
 "arguments":["adapter.py","instances","--account","{account}"],
 "format":"json","items_path":"items","value_key":"id","label_key":"name"}}
```

此例依赖同一表单中的 account 参数。

| format | 规则 |
| --- | --- |
| `auto` | 识别唯一 JSON 数组、逐行列表或有列名表格；存在歧义时要求明确映射。 |
| `json` | 根数组或 items_path 指向的数组；对象项用 value_key/label_key 映射，默认 value/label。 |
| `lines` | 每个非空行一项。 |
| `delimited` | delimiter 分隔；value_column/label_column 从 1 开始；columns 可校验列数，header 可跳过表头。 |

`empty_text` 指定空列表提示，`value_pattern` 校验完整值。无列名表格可在“测试读取”中选择值列和显示列；截断的 ID 无法还原。

来源路径和参数可引用上游 `{key}`。上游变化会清空下游、重读并丢弃过期结果；循环或缺失依赖拒绝保存。查询限 15 秒、1 MiB、500 项，失败或重复值不会部分加载。

目录接入仅根据明确且唯一的列表查询帮助推荐来源，不运行真实查询；实际读取发生在打开表单、刷新或测试读取时。工具作者须保证查询只读。

## 状态文件

状态由工具写入 state_path；客户端不从 stdout 自动提取状态，也不代为执行 status 命令。最小示例：

```json
{
  "schema":"orchestrate-state/v1",
  "tool_id":"example-tool",
  "current_date":"2026-11-17",
  "updated_at":"2026-11-17T09:30:00+08:00",
  "result":"success",
  "summary":"任务完成"
}
```

| 字段 | 规则 |
| --- | --- |
| 上述六个字段 | 必填；schema 固定，tool_id 匹配声明 id；日期有效，updated_at 带时区。 |
| `expires_at` | 可选带时区时间，不早于 updated_at；到期显示“已过期”，省略则不推断有效期。 |
| `items` | 可选对象数组，每项包含字符串 name、result、summary，最多 500 项。 |
| `last_command` | 可选业务操作标识，不关联客户端命令 ID。 |
| 自定义字段 | 可用于 state_key/options_state_key，不放凭证；不自动获得产物、日志或心跳管理能力。 |

result 支持 `success`、`already`、`partial_success`、`failure`、`running`、`skipped`、`disabled`、`unknown`、`none`；未知值原样显示。子项不自动汇总顶层结果，退出码也不会改写 JSON 状态。

- 客户端启动、选择工具、手动刷新和命令结束时读取，不周期轮询；有效期检查只更新缓存显示。
- 状态限 1 MiB。字段错误、归属不符或读取失败会清除旧缓存；本地文件缺失显示暂无状态，远端缺失显示读取失败。
- 工具应先原子写完状态再退出。使用同目录临时文件写入、关闭后替换，参考 [adapter.py](examples/tool-adapter/adapter.py)；并发写入仍需工具自身的锁。
- 应用内退出 0 表示命令正常结束，非零或崩溃表示运行失败。业务结果以 JSON 为准；写状态失败应报错并非零退出。

## 交付前检查

验证成功、失败、重复运行、中文与空格路径、空值、无效下拉值、损坏/过期状态及并发写入。工具 README 只需交代：入口、依赖、参数、凭证位置、状态路径、查询输出格式和错误恢复。不要把客户端表单校验当成业务权限校验。
