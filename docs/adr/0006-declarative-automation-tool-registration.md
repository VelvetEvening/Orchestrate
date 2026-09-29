---
status: accepted
---

# Register automation tools through a declarative tool description

Orchestrate 不扫描目录中的任意脚本来猜测工具，而要求每个可注册的自动化工具提供一份工具接入声明。声明描述工具身份、所属自动化工具组、执行目标、可执行命令、交互属性以及当前状态接口；用户在 Orchestrate 中选择工具的接入声明文件（本机文件，或服务器上的文件）后，应用读取并校验声明，确认通过后才完成注册。

工具自己负责产生当前状态 JSON，Orchestrate 负责读取、校验和展示。状态文件的位置由声明中的 `state_path` 写明，Orchestrate 不假定默认位置。第一版不自动把工具目录中的安装、卸载、测试或维护脚本暴露成命令；命令必须由工具声明明确列出。工具声明是接入边界，也是将现有 Windows、WSL 和 SSH 工具纳入 Orchestrate 的共同入口。

## Consequences

- 工具目录可以继续保留自己的脚本、配置、浏览器资料和测试文件，不会因为被 Orchestrate 发现就全部暴露。
- 注册流程可以在真正运行前校验 JSON 协议、执行目标和命令入口。
- 工具声明文件本身属于工具的一部分，应与工具代码一起维护；Orchestrate 保存的是注册关系和用户选择，不替工具猜测业务结构。
- 第一条真实接入优先使用一个无副作用的演示工具验证协议，再接入每日签到等真实工具。
