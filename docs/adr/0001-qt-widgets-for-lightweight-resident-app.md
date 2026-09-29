---
status: accepted
---

# Use Qt Widgets for the lightweight resident Windows client

Orchestrate 需要长期驻留后台，并以项目记录、自动化任务监测和时约提醒等传统桌面工具能力为主。第一版采用 Qt 6 Widgets + C++ + CMake，而不是以 Qt Quick/QML 作为整体 UI 技术：Widgets 更符合静态桌面界面和低频更新场景，避免为了动画与复杂图形引入不必要的运行时复杂度。性能重点放在托盘常驻、事件驱动、限制轮询、增量读取和异步 I/O，而不是依赖 UI 框架本身解决后台性能问题。

## Considered Options

- **Qt Quick/QML**：适合动画、复杂图形和高动态界面，但第一版不需要这些能力，并会增加混合语言与运行时管理复杂度。
- **Qt Widgets**：适合 Orchestrate 的表单、树、列表、日志、设置和托盘场景，技术路线与当前 Qt 学习基础一致。

## Consequences

- 第一版主窗口和后台托盘程序统一使用 Widgets。
- 监测器不能通过忙等或高频无意义刷新实现“实时”；应使用事件驱动、低频或自适应轮询，并将耗时工作与 UI 解耦。
- 如果未来出现真正需要高频动画或复杂可视化的页面，再单独评估局部引入 Qt Quick，而不是预先让整个应用采用混合架构。
