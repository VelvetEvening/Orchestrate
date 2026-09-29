---
status: accepted
---

# Use Alt+X as the standard tray-opening shortcut

Orchestrate 使用 Alt+X 在后台运行时显示并激活主窗口，完全退出后不生效。用户在 2026-09-28 确认此组合：原先 Ctrl+S+T / Ctrl+S+X 的多普通键组合不再采用，Alt+S 则在用户环境中发生冲突。

使用 Windows RegisterHotKey（Alt 修饰键加 X，禁用按住重复触发）和 WM_HOTKEY；不使用全局键盘钩子、输入缓存/重放或持续轮询。设置提供持久化禁用开关、注册状态和重试按钮；第一版固定 Alt+X，不提供任意组合编辑。

## Consequences

- 成功注册后，其他应用内的 Alt+X 可能被覆盖；失败时明确显示冲突或系统错误，不静默改键。
- 禁用和退出时释放注册；关闭到托盘保留注册，且始终不显示关闭提示。
- 无托盘时关闭仍正常退出。日程提醒通知不受关闭静默规则影响。
- 离屏回归测试不注册全局热键，也不向用户前台软件注入按键。真实桌面的抢焦点、键盘布局和应用冲突仍须 Windows 实测。
