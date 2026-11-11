---
version: 1
slug: "app-src-mainwindow-cpp"
primary_target: "app/src/mainwindow.cpp"
related_targets: ["app/src/pages/calendarpage.cpp","app/src/pages/projectspage.cpp","app/src/pages/automationpage.cpp","app/src/pages/clipboardpage.cpp"]
---

# 连续分区工作台

分支：`design/seamless-workspace`，基于 v3.2.1。用户已认可交互样例的视觉方向并要求正式实现；本轮交付本地验收副本，等待用户验收后再合入、推送和发布。

## Direction contract

THESIS：五个主模块共用连续工作面，去除大区域四周的卡片间隙、圆角和重复描边，把留白放在分区内部。

OWN-WORLD：延续 Windows 原生字体与浅色系统；侧栏 `#eff3f4`、索引区 `#f7f9fa`、正文白色，以极淡的共享边界和雾蓝选择态组织内容。

STORY：用户沿用已有导航、目录、记录编辑、工具运行与剪贴板操作，在更连贯的界面中完成原有工作。保留数据、快捷键、拖动、独立编辑窗口、失败反馈及安全确认。

FIRST VIEWPORT：保持 1180×760 默认窗口、980×640 最小窗口，以及 216/72 的原生侧栏宽度。日历或目录区直接接白色正文；依据用户最新验收意见，统一浅灰顶栏独占一行，左侧收起/展开，右侧原生窗口按钮，下方才开始侧栏与内容分区，无额外标题。

FORM：用户指定并认可的连续分区方向；code-led，未生成新的方向 seed。原型为聊天目录中的 `seamless-workspace.html`，Qt 实现保留原生控件的实际尺寸和交互。

FINISH：unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance

## 实现与验收边界

- 仅调整布局、绘制和视觉角色，不改变数据结构或后台任务策略。
- 顶栏使用普通 QFrame 与统一主题样式，无分区跟踪、轮询、动画或常驻后台任务；保留现有 Windows 标题区域修复。
- 原型中的示例数据不写入实际用户数据库。验收副本独立存放，使用合成数据并默认关闭监听、快捷键与自动更新。
- 版本号与发布记录保持 v3.2.1，待验收后按用户指示发布。
