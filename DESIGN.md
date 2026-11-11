---
name: "Orchestrate"
description: "Windows 原生、灰白与雾蓝的紧凑日常工作台"
colors:
  background: "#f6f7f9"
  sidebar: "#f0f2f5"
  surface: "#ffffff"
  text: "#344254"
  muted: "#637184"
  border: "#e1e6ed"
  control-border: "#c6d0dd"
  accent: "#41638f"
  selection: "#e7eef8"
  hover: "#f0f3f7"
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
  nav-hover: "#e9edf3"
  nav-selected: "#e2eaf5"
  danger-hover-border: "#e6c5c5"
typography:
  body:
    fontFamily: "Microsoft YaHei UI"
    fontSize: "10pt"
  page-title:
    fontSize: "19px"
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
  item: "4px"
  status: "5px"
  control: "6px"
  navigation: "7px"
  radio: "8px"
  card: "10px"
spacing:
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
  navigation:
    backgroundColor: "transparent"
    textColor: "{colors.text}"
    typography: "{typography.navigation}"
    rounded: "{rounded.navigation}"
    padding: "10px 12px"
  navigation-hover:
    backgroundColor: "{colors.nav-hover}"
  navigation-selected:
    backgroundColor: "{colors.nav-selected}"
    textColor: "{colors.accent}"
    typography: "{typography.navigation-selected}"
  card:
    backgroundColor: "{colors.surface}"
    rounded: "{rounded.card}"
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
  calendar-selected:
    backgroundColor: "{colors.selection}"
    textColor: "{colors.accent}"
    typography: "{typography.calendar-date}"
    rounded: "{rounded.card}"
  run-panel:
    backgroundColor: "{colors.background}"
    textColor: "{colors.text}"
    rounded: "{rounded.control}"
    padding: "10px 14px"
---

# Design System: Orchestrate

## Overview

**Creative North Star: "浅色日常工作台"**

以浅灰桌面底、白色工作面和雾蓝操作构成安静、紧凑的 Windows 工作台。时序记录、项目记录、自动化工具、剪贴板及设置共用同一套视觉语言；各模块保留适合其内容的组织方式。

这是 C++17 / Qt 6 Widgets 原生桌面界面，不是网页或移动应用。全局浅色调色板覆盖主窗口、独立阅读编辑窗口、弹窗和菜单；支持的 Windows 版本通过 DWM 应用浅色标题栏。

**Key Characteristics:**

- 灰白表面、深灰正文，雾蓝标识选择与主要操作。
- 紧凑间距、小圆角、细边界，不使用自定义投影制造层级。
- 导航、输入、保存反馈与工具运行状态共享语义。

依据：`app/src/ui/theme.h`、`theme.cpp`、`app/resources/ui/light.qss`、`app/src/mainwindow.cpp`、页面与共享控件实现。产品约束见 `PRODUCT.md`；浅色主题背景见 `docs/development/light-ui.md`，本轮布局方向与验收范围见 `docs/development/page-layout.md`。前置 token 是规范值，Qt 实现是最终渲染依据。

## Colors

### Primary

雾蓝 `accent` 用于选中内容、主要操作文字和活动状态；`selection` 是轻量选中底色。主按钮保持浅底，悬停和按下分别使用 `primary-hover`、`primary-pressed`。导航有独立的 `nav-hover` 与 `nav-selected` 浅底。

### Neutral

`background` 是桌面底，`sidebar` 是导航与原生标题栏底，`surface` 是白色内容和弹窗面。`text` 与 `muted` 区分正文和说明；`border` 分隔内容区域，`control-border` 勾勒可操作控件，`focus` 明示焦点边界，`disabled` 表达不可用状态。

### 状态

`success` / `success-background` 表达完成与成功；`warning` / `warning-background` 表达提醒和需留意的信息；`danger` / `danger-background` 表达失败、错误和删除操作。运行与加载复用雾蓝，未运行、禁用、跳过复用弱化灰色。状态文字说明含义，颜色只是辅助。

**The 浅色贯通 Rule.** 主窗口、独立窗口、菜单与运行输出共用浅色系统；不要新增显眼的大面积深色区域。

## Typography

正文优先使用 Microsoft YaHei UI；若系统未安装，保留 `QApplication` 原有字体。全局字号使用 `body`，控件继承字体家族；QSS 只对命名角色覆写字号和字重。系统字体用于原生操作与内容阅读，不建立独立展示字体。

- 主窗口当前页标题使用 `page-title`，只在全局顶部显示；工具标题和卡片标题分别使用 `tool-title`、`card-title`。当前页标题取固定模块名，不随设置入口的更新提示改变。
- 导航使用 `navigation`，选中后使用 `navigation-selected`；帮助文字使用 `label`，短标签使用 `tag`。
- 日历日期使用 `calendar-date`，选中或今天加粗；运行输出使用 `QFontDatabase::FixedFont`，不指定某个必装等宽字体。
- 正文预览的一级至三级标题按正文基准字号乘以 1.6 / 1.35 / 1.15，加粗；它们属于文档内容，不替代界面标题层级。编辑器允许 Ctrl + 鼠标滚轮缩放文字。

**The 原生字号 Rule.** 保留 Qt 的 pt / px 区分与字体回退，不把全部字号强制换成网页单位，不虚构统一行高。

## Layout

主窗口默认 1180×760，最小 980×640，均为 Qt 逻辑尺寸。侧栏展开宽 216、收起宽 72；收起后保留图标、可访问名称与工具提示。四个模块与设置共用侧栏状态和页面标题，设置位于底部，但不降低视觉重要性。

全局顶部横跨侧栏和正文，左侧为固定位置的分栏开关与当前页标题，间距使用 `spacing.12`；侧栏展开、收起时两者均不横移。顶栏内边距为左 18、上 12、右 24、下 12，其底部以内容边框分隔。侧栏从导航直接开始，内边距为左右 14、上下 16，导航间距为 `spacing.6`；五个页面不再各自重复标题行。

五个主页面外边距为左右和底部 `spacing.24`、顶部 `spacing.16`，正文与侧栏首项距顶一致；页内间距依内容使用 14 / 16 / 18。常见卡片内边距为四边 18，或左右 22、上下 18；设置卡片左右 22、上下 20。局部表单与操作行使用 6 / 8 / 10 / 12 的紧凑间距。保留这些原生布局变体，不宣称存在严格倍数网格。

页面采用日历与详情、列表与详情或树与滚动详情的原生布局。窄窗口通过侧栏收起、可调整分隔区、标题换行与内容滚动适配，没有网页媒体查询断点。自动化工具长标题允许收缩和换行；工具操作按钮保持可访问。独立窗口按内容设置自身最小尺寸。

## Elevation & Depth

内容卡片是平面白色工作面，使用细边界；运行区与状态行使用浅灰内衬。代码未定义自定义阴影或装饰动画。窗口、菜单等系统层级的实际阴影由 Windows / Qt 处理，不记录为应用阴影 token；状态变化直接响应交互。

**The 平面层级 Rule.** 用灰白表面、细边界和留白区分区域；不要给内容卡片增加自定义投影。

## Shapes

圆角按语义区分：列表项与标签使用 `item`，运行状态徽标使用 `status`，按钮与输入框使用 `control`，导航使用 `navigation`，卡片与日历日期块使用 `card`。边框通常为 1px，焦点通过边框换色呈现。原生单选指示器采用圆形，复选指示器为小圆角方形。

17 个主题 SVG 沿用线性轮廓，常规图标以 24×24 画布、1.6 描边和圆端点绘制；导航显示为 19×19，全局顶部分栏开关使用 `panel-left.svg`，显示为 20×20。项目状态图标显示为 18×18：雾蓝圆环加圆点表示进行中，绿色圆环加勾表示已完结。复选、单选及上下箭头按控件分别缩放，不强制单一图标尺寸。应用图标与既有更新标记保留；本次未添加展示性栅格素材。

## Components

### Buttons

次要按钮为白底、深灰文字和控件边框；主要按钮为浅雾蓝底、雾蓝加粗文字。默认内边距见 token，QSS 内容最小高度为 20px，不能误记为含内边距的总高度。悬停改变底色和边框，按下加深浅蓝，键盘焦点改为强调色边框。禁用后使用浅灰底、弱化文字与内容边框。

删除按钮为透明底、危险色文字；悬停出现淡红底与淡红边框，焦点边框为危险色。确认对话框的 Accept / Yes 按钮复用主要按钮角色。

顶部分栏开关的 QSS 内容尺寸为 32×32，内边距为 0，加四边 1px 边框后整体为 34×34，圆角沿用 `control`。默认透明底与透明边框；悬停为浅灰蓝，按下复用选中底色，键盘焦点使用 `focus` 边框。工具提示与可访问名称随状态在“展开侧边栏”和“收起侧边栏”间切换。

### Inputs / Fields

输入框和编辑器共用白底、控件边框与 `control` 圆角；焦点改为强调色边框。单行输入、下拉与数值框悬停时改变边框。只读单行框使用桌面底与弱边框；禁用态弱化文字。正文编辑与预览内边距为上下 12、左右 14，预览使用更轻的内容边框。表单错误通过邻近状态文字反馈，不虚构未实现的输入框错误描边。

### Navigation / Tabs

导航默认透明底，悬停与选中使用各自浅底；选中文字加粗，焦点边框保持可见。五个入口都使用 SVG 图标。分栏开关位于全局顶部，侧栏直接呈现导航，不重复显示应用名或“工作台”分组标签；应用名仍保留在原生标题栏。预览 / 编辑页签使用浅雾蓝选中面与控件边框，保留原生键盘操作。

剪贴板页的“监听剪贴板”和“历史设置”位于全局顶部右侧，间距使用 `spacing.12`，仅该页显示；独立使用 ClipboardPage 时操作仍留在页面内。主窗口键盘焦点依次经过分栏开关、当前页顶部操作、导航与正文；导航保留 Qt 排他按钮组的 Tab 与方向键行为。

### Cards / Lists

卡片由白面、内容边框与 `card` 圆角组成，内边距按 Layout 的现有变体选择。列表项悬停为浅灰、选择为浅雾蓝、焦点为可见细框；嵌在卡片内的项目、日记、提醒与工具树列表去掉重复外框。空内容提示显示在视口中，不伪造数据行。

项目列表行高为 44px，只显示名称与状态图标；工具提示和 `Qt::AccessibleTextRole` 保留“进行中 / 已完结”文字。右键菜单先列状态操作，再经分隔线放置末项“项目设置”。详情顶部将“大纲”和“新建文件夹”置于同一操作行，不重复放置“协作记录”标签或常驻项目设置按钮。

### Chips / Status

短标签使用 `tag`；运行状态徽标使用 `status` 字重和圆角。成功、失败、运行、未运行等文字与语义底色配对。保存结果在编辑区域附近显示，错误不会被隐藏成普通说明。

### Calendar

日期单元格独立绘制白底；选中日期用浅雾蓝圆角块，今天增加焦点色边框，未来日期用浅灰块；日期块最大边长为 44，随单元格尺寸收缩。日记用小方块、提醒用三角形，并配文字图例；颜色不承担唯一识别职责。导航、键盘输入和可访问性仍由 `QCalendarWidget` 负责。右侧直接呈现提醒事项与日记，不重复显示“当天记录”、日期和条数摘要；两组列表的可访问名称包含所选日期。

### Run Output

运行面板使用浅灰底、紧凑内边距与 `control` 圆角。输出文字为深灰、系统等宽字体，编辑器背景透明且无重复边框；运行状态和错误信息使用统一状态语义。

`.impeccable/design.json` 的 HTML/CSS 只供文档面板预览，是上述原生组件的浏览器近似表达；Qt 绘制、DWM 标题栏、字体度量和系统行为不能由 Shadow DOM 完整复现。色阶预览是生成的辅助色阶，不是可直接新增使用的应用 token。

## Do's and Don'ts

### Do:

- Do 从 `UiTheme` 与全局 QSS 复用颜色和状态；新增独立窗口同样安装全局主题。
- Do 为导航与图标操作保留可访问名称、工具提示和键盘焦点；状态通过可见文字、工具提示或可访问名称保留明确语义。
- Do 在主窗口最小尺寸下检查长标题换行、表单收缩、滚动区和可见操作；中文正文优先保持阅读面积。
- Do 保留现有应用图标与更新标记，新增通用操作沿用 SVG 线性图标。

### Don't:

- Don't 新增大块深色导航、标题栏、编辑面或终端背景。
- Don't 把成功、警告、危险颜色当作装饰色，也不要只用颜色区分日历记录类型。
- Don't 添加装饰动画、大型背景效果、网页式展示大标题或为了装饰引入常驻进程。
- Don't 用侧栏位置推断模块重要性；设置与四大模块遵循同等视觉标准。
