# v3.4.0 内容导航

Mode: Operate / Read。Windows Qt Widgets，沿用现有灰白、雾蓝、原生字体与连续分区。

## Direction contract

THESIS：默认在同一工作面逐级阅读内容；返回保留现场，关闭才释放内容和处理草稿。

OWN-WORLD：继承 DESIGN.md，不改变全局标题栏、导航和主题。内容顶端采用紧凑的返回、当前内容名称、独立打开、关闭操作行。

STORY：用户从列表打开内容，继续深入详情，再返回原来的筛选、选择和位置；切换模块保留草稿，需要对照时主动独立打开并可收回。

FIRST VIEWPORT：全局窗口栏与左侧模块导航保持。内容区使用完整剩余宽高；Git 历史与提交详情互相切换，详情不常驻占用图的高度。集合和正文采用同样的层级。

FORM：用户授权按既有讨论实施，code-led，沿用明确方向，没有新视觉概念抽签。先统一导航与生命周期，再接入 Git、日记、提醒、协作记录和剪贴板。小型设置与安全确认保留原生对话框。

FINISH：unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance

## 实现与校核

- 内容容器与页面栈已接入时序记录、项目记录和剪贴板；返回与切换模块保留实例，关闭检查未保存修改，独立打开与收回移动同一内容分支。主窗口 Alt+左箭头只作用于当前嵌入内容，模块根页不影响独立窗口。
- 工作面使用现有 `workspaceSurface=content`，外层边距与间距为 0；操作行内边距为左右 16、上下 8，间距为 10，复用 `quiet` 按钮、现有 SVG 返回图标和继承字体。统一独立容器默认 1100×760、最小 720×520（Qt 逻辑尺寸），沿用系统窗口层级与浅色标题栏。
- 已比对 `DESIGN.md`、`.impeccable/design.json`、`ui/theme.h`、`theme.cpp` 与 `light.qss`。本次沿用既有灰白与雾蓝 token、原生字体、小圆角控件及平面层级；设计系统文件保持不变，没有新增分发栅格素材。
- 最终审阅对已修正的快捷返回问题给出 ship；回归与隔离 Windows 桌面验收范围见 `docs/development/content-navigation.md`，视觉证据位于 `.impeccable/review/v3.4.0/`。用户已验收导航与入口精简，并授权检查通过后合并、推送和发布。
