---
name: "Orchestrate"
description: "Windows 原生、灰白与雾蓝的连续日常工作台"
colors:
  background: "#f7f9fa"
  sidebar: "#eff3f4"
  index-surface: "#f7f9fa"
  surface: "#ffffff"
  text: "#34404c"
  muted: "#66737f"
  border: "#e8edef"
  control-border: "#c6d0dd"
  accent: "#41638f"
  selection: "#e4edf3"
  hover: "#eef2f5"
  focus: "#7895bc"
  success: "#386b50"
  success-background: "#edf5ef"
  warning: "#87611e"
  warning-background: "#faf3e5"
  danger: "#a64545"
  danger-background: "#fbefef"
  disabled: "#919ba9"
  primary-hover: "#dce7f6"
  primary-pressed: "#d4e1f3"
  nav-hover: "#e7edef"
  danger-hover-border: "#e6c5c5"
typography:
  body:
    fontFamily: "Microsoft YaHei UI"
    fontSize: "10pt"
  page-title:
    fontSize: "16px"
    fontWeight: 600
  tool-title:
    fontSize: "19px"
    fontWeight: 600
  card-title:
    fontSize: "15px"
    fontWeight: 600
  navigation:
    fontSize: "14px"
  navigation-selected:
    fontSize: "14px"
    fontWeight: 600
  label:
    fontSize: "12px"
  tag:
    fontSize: "11px"
  button-primary:
    fontWeight: 600
  status:
    fontWeight: 500
  calendar-date:
    fontSize: "11pt"
rounded:
  none: "0px"
  item: "4px"
  status: "5px"
  control: "6px"
  navigation: "7px"
  radio: "8px"
  card: "10px"
spacing:
  "0": "0px"
  "4": "4px"
  "6": "6px"
  "8": "8px"
  "10": "10px"
  "12": "12px"
  "14": "14px"
  "16": "16px"
  "18": "18px"
  "20": "20px"
  "22": "22px"
  "24": "24px"
components:
  button-secondary:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.text}"
    rounded: "{rounded.control}"
    padding: "6px 11px"
  button-secondary-hover:
    backgroundColor: "{colors.hover}"
  button-quiet:
    backgroundColor: "transparent"
    textColor: "{colors.text}"
    rounded: "{rounded.control}"
    padding: "6px 11px"
  button-quiet-hover:
    backgroundColor: "{colors.hover}"
  button-quiet-active:
    backgroundColor: "{colors.selection}"
  button-primary:
    backgroundColor: "{colors.selection}"
    textColor: "{colors.accent}"
    typography: "{typography.button-primary}"
    rounded: "{rounded.control}"
    padding: "6px 11px"
  button-primary-hover:
    backgroundColor: "{colors.primary-hover}"
  button-primary-active:
    backgroundColor: "{colors.primary-pressed}"
  button-danger:
    backgroundColor: "transparent"
    textColor: "{colors.danger}"
    rounded: "{rounded.control}"
    padding: "6px 11px"
  button-danger-hover:
    backgroundColor: "{colors.danger-background}"
  button-disabled:
    backgroundColor: "{colors.background}"
    textColor: "{colors.disabled}"
  input:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.text}"
    rounded: "{rounded.control}"
    padding: "6px 9px"
  input-readonly:
    backgroundColor: "{colors.background}"
  input-search:
    backgroundColor: "{colors.index-surface}"
    textColor: "{colors.text}"
    rounded: "{rounded.control}"
    padding: "6px 9px"
  navigation:
    backgroundColor: "transparent"
    textColor: "{colors.text}"
    typography: "{typography.navigation}"
    rounded: "{rounded.navigation}"
    padding: "10px 12px"
  navigation-hover:
    backgroundColor: "{colors.nav-hover}"
  navigation-selected:
    backgroundColor: "{colors.selection}"
    textColor: "{colors.accent}"
    typography: "{typography.navigation-selected}"
  card:
    backgroundColor: "{colors.surface}"
    rounded: "{rounded.card}"
  workspace-content:
    backgroundColor: "{colors.surface}"
    rounded: "{rounded.none}"
  workspace-index:
    backgroundColor: "{colors.index-surface}"
    rounded: "{rounded.none}"
  workspace-section:
    backgroundColor: "{colors.surface}"
    rounded: "{rounded.none}"
  workspace-inset:
    backgroundColor: "{colors.index-surface}"
    rounded: "{rounded.none}"
  status-success:
    backgroundColor: "{colors.success-background}"
    textColor: "{colors.success}"
    typography: "{typography.status}"
    rounded: "{rounded.status}"
    padding: "3px 9px"
  tag:
    backgroundColor: "{colors.selection}"
    textColor: "{colors.accent}"
    typography: "{typography.tag}"
    rounded: "{rounded.item}"
    padding: "2px 7px"
  tab:
    backgroundColor: "transparent"
    textColor: "{colors.muted}"
    rounded: "{rounded.control}"
    padding: "7px 12px"
  tab-selected:
    backgroundColor: "{colors.selection}"
    textColor: "{colors.accent}"
    typography: "{typography.button-primary}"
  clipboard-tab:
    backgroundColor: "transparent"
    textColor: "{colors.muted}"
    rounded: "{rounded.none}"
    padding: "7px 4px"
  clipboard-tab-selected:
    backgroundColor: "transparent"
    textColor: "{colors.accent}"
    typography: "{typography.button-primary}"
  calendar-selected:
    backgroundColor: "{colors.selection}"
    textColor: "{colors.accent}"
    typography: "{typography.calendar-date}"
    rounded: "{rounded.card}"
  run-panel:
    backgroundColor: "{colors.background}"
    textColor: "{colors.text}"
    rounded: "{rounded.none}"
    padding: "10px 14px"
---

# Design System: Orchestrate

## Overview

**Creative North Star: "浅色日常工作台"**

以浅灰导航、灰白索引区、白色工作面和雾蓝操作构成安静、紧凑的 Windows 工作台。时序记录、项目记录、自动化工具、剪贴板及设置共用连续分区：大区域直接衔接，留白位于分区内部；各模块保留适合其内容的组织方式。

这是 C++17 / Qt 6 Widgets 原生桌面界面，不是网页或移动应用。全局浅色调色板覆盖主窗口、独立阅读编辑窗口、弹窗和菜单；Windows 主窗口在标题区域保留独立统一的窗口操作栏，工作分区从其下方开始，独立窗口继续沿用 DWM 浅色标题栏。

**Key Characteristics:**

- 灰白表面、深灰正文，雾蓝标识选择与主要操作。
- 工作面无外围卡片间隙和圆角，控件保留小圆角；以底色、共享细边界和内部留白建立层级，不使用自定义投影。
- 导航、输入、保存反馈与工具运行状态共享语义。

依据：`app/src/ui/theme.h`、`theme.cpp`、`app/resources/ui/light.qss`、`app/src/mainwindow.cpp`、页面与共享控件实现。产品约束见 `PRODUCT.md`；浅色主题背景见 `docs/development/light-ui.md`，连续分区方向与验收范围见 `docs/development/seamless-workspace.md`。本文记录用户已验收的 v3.2.2 实现。前置 token 是规范值，Qt 实现是最终渲染依据。

## Colors

### Primary

雾蓝 `accent` 用于选中内容、主要操作文字和活动状态；`selection` 是列表、导航与主要操作共用的轻量选中底色。主按钮保持浅底，悬停和按下分别使用 `primary-hover`、`primary-pressed`。导航悬停使用独立的 `nav-hover` 浅底。

### Neutral

`sidebar` 是导航、主窗口统一顶栏与独立窗口原生标题栏底，`index-surface` 是项目及工具索引、日历和局部内衬，`surface` 是白色正文、根窗口和弹窗面。顶栏不随下方分区改变底色。`background` 用于运行区、状态行、只读及禁用控件，与 `index-surface` 目前同色但保留独立语义。`text` 与 `muted` 区分正文和说明；`border` 分隔内容区域，`control-border` 勾勒常规输入和次要按钮，`focus` 明示焦点边界，`disabled` 表达不可用状态。

### 状态

`success` / `success-background` 表达完成与成功；`warning` / `warning-background` 表达提醒和需留意的信息；`danger` / `danger-background` 表达失败、错误和删除操作。运行与加载复用雾蓝，未运行、禁用、跳过复用弱化灰色。状态文字说明含义，颜色只是辅助。

**The 浅色贯通 Rule.** 主窗口、独立窗口、菜单与运行输出共用浅色系统；不要新增显眼的大面积深色区域。

## Typography

正文优先使用 Microsoft YaHei UI；若系统未安装，保留 `QApplication` 原有字体。全局字号使用 `body`，控件继承字体家族；QSS 只对命名角色覆写字号和字重。系统字体用于原生操作与内容阅读，不建立独立展示字体。

- 主窗口不再显示模块页标题；`page-title` 仅保留给未接入主窗口的 PlaceholderPage，不能据此为五个模块新增重复标题。工具标题使用 `tool-title`；`card-title` 沿用实现中的角色名，现也用于连续分区的标题。
- 导航使用 `navigation`，选中后使用 `navigation-selected`；帮助文字使用 `label`，短标签使用 `tag`。
- 日历日期使用 `calendar-date`，选中或今天加粗；运行输出使用 `QFontDatabase::FixedFont`，不指定某个必装等宽字体。
- 正文的一至六级标题按正文基准字号乘以 1.6 / 1.35 / 1.15 / 1.1 / 1.05 / 1.0，加粗；它们属于文档内容，不替代界面标题层级。即时编辑与阅读共用排版，当前段落或表格单元格展开语法；整篇源码入口放在编辑器右键菜单中，源码视图右键提供“返回即时编辑”；表格与代码使用现有浅灰色和控件边框。编辑器允许 Ctrl + 鼠标滚轮缩放文字。

**The 原生字号 Rule.** 保留 Qt 的 pt / px 区分与字体回退，不把全部字号强制换成网页单位，不虚构统一行高。

v3.5.6 将同样的保存与布局规则扩展到日记、提醒及剪贴板编辑：标题与保存状态同排，底部不保留保存／关闭按钮。日记日期同排，提醒选项在正文上方，内容框占用剩余高度。日记与提醒默认 20pt，分别持久化；剪贴板沿用既有正文大小。协作记录的切换控件按内容宽度布局，末项取消右间距，与正文框右沿对齐。保存失败显示简短状态，详情保留在悬停提示和可访问说明。

v3.5.5 协作记录采用独立内容字号：正文 20pt、摘要 24pt，Ctrl＋滚轮的调整分别持久化。标题输入最大宽度为 520 个 Qt 逻辑像素，并可随窗口缩小；同一行右侧为摘要／正文切换，两者之间显示单行保存状态。仅通过 Ctrl+S 或离开确认保存，底部不保留按钮或状态栏，内容框向下扩展。打开和保存后均直接编辑，不再显示只读预览、编辑、取消编辑或复制正文按钮；离开时统一处理未保存修改。失败详情保留在状态的工具提示与可访问说明中。

## Layout

主窗口默认 1180×760，最小 980×640，均为 Qt 逻辑尺寸。侧栏展开宽 216、收起宽 72；收起后保留图标、可访问名称与工具提示。四个模块与设置共用侧栏状态，以导航选中状态标识当前模块；设置位于底部，但不降低视觉重要性。

全局顶部是横跨整窗的独立窗口操作栏，由左侧固定位置的侧栏收起/展开与右侧最小化、最大化、关闭独占；不同底色的导航、索引和正文分区从其下方开始。顶栏不显示模块名称或页面操作，侧栏展开、收起时开关不横移。Windows 主窗口通过 Qt 扩展客户区将该操作栏融入标题区域，不再单独绘制应用图标与 Orchestrate 名称；窗口标题元数据仍用于任务栏和 Alt+Tab，窗口按钮与拖动、缩放等交互由 Qt Windows 后端负责。

顶栏高度取系统标题安全区域高度，下限为 32px，本机为 32px；内边距为左 18、上下 0，右侧为三颗窗口按钮预留顶栏高度的 4.5 倍与额外 12px。offscreen 等未启用融合的环境沿用 32px 高度和右边距 24。顶栏使用普通 `QFrame`，统一采用 `sidebar` 底色，以 1px `border` 底线与工作区分开；原有 Windows 标题区域、系统按钮与窗口恢复规则保留。侧栏从导航直接开始，内边距为左右 14、上下 16，导航间距为 `spacing.6`；五个页面不再各自重复标题行。

主工作面直接衔接，没有外围卡片间隙；日历、项目、自动化和设置页面的外层边距与分区间距为 0。项目与工具索引内边距为左右 16、顶部 22、底部 24；常见正文、日历和设置分节为左右 24、顶部 22、底部 24，自动化身份区底部为 22。剪贴板是一整块白色工作面，其页面布局边距 24 / 22 / 24 / 24 是内容内留白，不是卡片外间隙。局部表单与操作行仍使用 6 / 8 / 10 / 12 / 14 等紧凑间距，保留原生布局变体，不宣称存在严格倍数网格。

项目与自动化工具的横向 `QSplitter`、剪贴板的纵向 `QSplitter` 使用 1px 共享分隔线，悬停为焦点色，仍可原生拖动调整。项目与工具的初始分栏分配分别为 230:650、300:900，由可用尺寸与最小宽度决定实际宽度；不沿用聊天原型的固定列宽。日历与详情保持 3:2 的布局伸展关系。

页面采用日历与详情、列表与详情或树与滚动详情的原生布局。窄窗口通过侧栏收起、可调整分隔区、标题换行与内容滚动适配，没有网页媒体查询断点。自动化工具长标题允许收缩和换行；工具操作按钮保持可访问。独立窗口按内容设置自身最小尺寸。

## Elevation & Depth

主工作面通过导航、索引和正文的浅色分区建立层级，分节只保留底部共享边界；运行区、状态行和剪贴板预览使用平直的浅灰内衬。代码未定义自定义阴影或装饰动画。窗口、菜单等系统层级的实际阴影由 Windows / Qt 处理，不记录为应用阴影 token；状态变化直接响应交互。

**The 平面层级 Rule.** 用连续灰白表面、共享细边界和内部留白区分区域；不要给主工作面恢复独立卡片外壳或增加自定义投影。

## Shapes

主工作面、分节、内衬和运行区使用 `none`；列表项与标签使用 `item`，运行状态徽标使用 `status`，按钮与输入框使用 `control`，导航使用 `navigation`，选中或今天的日历日期块使用 `card`。既有卡片样式仍供未接入主窗口的 PlaceholderPage 与 UpdateWidget 独立使用，主窗口更新分节由工作面角色覆盖。边框通常为 1px，焦点通过边框换色呈现；剪贴板页签以底线表达状态。原生单选指示器采用圆形，复选指示器为小圆角方形。

17 个主题 SVG 沿用线性轮廓，常规图标以 24×24 画布、1.6 描边和圆端点绘制；导航显示为 19×19，全局顶部分栏开关使用 `panel-left.svg`，显示为 18×18。项目状态图标显示为 18×18：雾蓝圆环加圆点表示进行中，绿色圆环加勾表示已完结。复选、单选及上下箭头按控件分别缩放，不强制单一图标尺寸。应用图标与既有更新标记保留；本次未添加展示性栅格素材。

## Components

### Buttons

常规次要按钮仍为白底、深灰文字和控件边框，用于对话框等既有操作。主工作面内的大纲、打开、编辑、刷新与设置等轻量操作使用 `button-quiet`：默认透明底和透明边框，悬停为 `hover`，按下为 `selection`，键盘焦点为 `focus` 边框。主要按钮为浅雾蓝底、雾蓝加粗文字和默认透明边框，悬停及按下使用各自状态色；显式的 enabled focus 规则保证主要和默认按钮都有 `accent` 焦点边框。默认内边距见 token，QSS 内容最小高度为 20px，不能误记为含内边距的总高度。常规、轻量和主要按钮禁用后使用浅灰底、弱化文字与内容边框。

删除按钮为透明底、危险色文字；悬停出现淡红底与淡红边框，焦点边框为危险色。确认对话框的 Accept / Yes 按钮复用主要按钮角色。

顶部分栏开关的 QSS 内容尺寸为 28×28，内边距为 0，加四边 1px 边框后整体为 30×30，圆角沿用 `control`。默认透明底与透明边框；悬停为浅灰蓝，按下复用选中底色，键盘焦点使用 `focus` 边框。工具提示与可访问名称随状态在“展开侧边栏”和“收起侧边栏”间切换。

### Inputs / Fields

常规输入框和编辑器共用白底、控件边框与 `control` 圆角；焦点改为强调色边框。单行输入、下拉与数值框悬停时改变边框。项目记录和剪贴板搜索使用 `input-search` 的灰白底与默认透明边框，悬停出现内容边框，焦点使用 `focus`。只读单行框使用 `background` 与弱边框；禁用态弱化文字。正文编辑与预览内边距为上下 12、左右 14，预览使用更轻的内容边框。剪贴板预览是无外框、无圆角的灰白内衬，文字内边距为 12。表单错误通过邻近状态文字反馈，不虚构未实现的输入框错误描边。

### Navigation / Tabs

导航默认透明底，悬停与选中使用各自浅底；选中文字加粗，焦点边框保持可见。五个入口都使用 SVG 图标。分栏开关位于全局顶部，侧栏直接呈现导航，不重复显示应用名或“工作台”分组标签；模块名称仅显示在展开的导航入口中。摘要 / 正文页签使用浅雾蓝选中面与控件边框，保留原生键盘操作；编辑页不再显示预览 / 编辑切换。

剪贴板标签页使用独立的 `clipboard-tab`：透明底、直角、2px 底线，选中底线为 `accent`，焦点底线为 `focus`；标签右侧间距为 14。其底部左侧为主要“新建”、轻量“编辑”和危险“删除”，右下角为轻量“设置”；不再放置常驻“复制并收起”按钮，双击条目、Enter 与原右键菜单保留该操作。“设置”打开原生菜单：可勾选的“监听剪贴板”、分隔线、“历史设置…”。菜单沿用全局白面、控件边框和浅雾蓝选中状态；监听开关即时保存，失败回退勾选状态并显示错误，历史设置进入原有保留上限对话框。主窗口键盘焦点依次经过分栏开关、当前选中的导航与正文；导航保留 Qt 排他按钮组的 Tab 与方向键行为，底部设置菜单支持键盘打开与选择。

### Work Surfaces / Lists

`workspaceSurface` 区分白色 `content`、灰白 `index`、带底边界的白色 `section` 与灰白 `inset`，全部无外围圆角。列表项悬停为浅灰、选择为浅雾蓝、焦点为可见细框；项目、日记、提醒、工具树和剪贴板列表去掉重复外框。空内容提示显示在视口中，不伪造数据行。保留的 `card` 样式仅服务既有独立组件，不用于五个主页面的外壳。

项目列表行高为 44px，只显示名称与状态图标；工具提示和 `Qt::AccessibleTextRole` 保留“进行中 / 已完结”文字。右键菜单先列状态操作，再经分隔线放置末项“项目设置”。详情顶部将“大纲”和“新建文件夹”置于同一操作行，不重复放置“协作记录”标签或常驻项目设置按钮。

自动化工具左列底部同一行仅保留“新建工具组”和“注册工具”两个按钮；注册工具使用原生下拉菜单，包含本地、WSL 和 SSH 服务器工具。工具组重命名与删除保留在树的右键菜单中。右侧身份、状态和命令连续排列，命令行以底边界分隔、行间距为 0；状态明细与运行输出使用浅灰内衬。设置页以白色平面分节组织运行、托盘、快捷键及版本更新。

### Chips / Status

短标签使用 `tag`；运行状态徽标使用 `status` 字重和圆角。成功、失败、运行、未运行等文字与语义底色配对。保存结果在编辑区域附近显示，错误不会被隐藏成普通说明。

### Calendar

日历导航、星期标题与日期单元格共同使用 `index-surface` 连续底色；仅选中或今天的日期绘制圆角块。选中日期用浅雾蓝，今天增加焦点色边框；未来日期以 `muted` 文字区分，其他月份用 `disabled`。日期块最大边长为 44，随单元格尺寸收缩。日记用小方块、提醒用三角形，并配文字图例；颜色不承担唯一识别职责。导航、键盘输入和可访问性仍由 `QCalendarWidget` 负责。右侧直接呈现提醒事项与日记，不重复显示“当天记录”、日期和条数摘要；两组列表的可访问名称包含所选日期。

### Run Output

运行面板使用浅灰底、紧凑内边距与直角，无外框。输出文字为深灰、系统等宽字体，编辑器背景透明且无重复边框；运行状态和错误信息使用统一状态语义。

`.impeccable/design.json` 的 HTML/CSS 只供文档面板预览，是上述原生组件的浏览器近似表达；Qt 绘制、DWM 标题栏、字体度量和系统行为不能由 Shadow DOM 完整复现。色阶预览是生成的辅助色阶，不是可直接新增使用的应用 token。

## Do's and Don'ts

### Do:

- Do 从 `UiTheme` 与全局 QSS 复用颜色和状态；新增独立窗口同样安装全局主题。
- Do 使用连续工作面角色组织主页面，把留白放在分区内部；独立统一的顶栏只容纳窗口操作，工作分区从其下方开始。
- Do 为导航与图标操作保留可访问名称、工具提示和键盘焦点；状态通过可见文字、工具提示或可访问名称保留明确语义。
- Do 在主窗口最小尺寸下检查长标题换行、表单收缩、滚动区和可见操作；中文正文优先保持阅读面积。
- Do 保留现有应用图标与更新标记，新增通用操作沿用 SVG 线性图标。

### Don't:

- Don't 新增大块深色导航、标题栏、编辑面或终端背景。
- Don't 把成功、警告、危险颜色当作装饰色，也不要只用颜色区分日历记录类型。
- Don't 添加装饰动画、大型背景效果、网页式展示大标题或为了装饰引入常驻进程。
- Don't 用侧栏位置推断模块重要性；设置与四大模块遵循同等视觉标准。
