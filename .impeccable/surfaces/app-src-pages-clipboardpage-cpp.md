---
version: 1
slug: "app-src-pages-clipboardpage-cpp"
primary_target: "app/src/pages/clipboardpage.cpp"
related_targets: ["app/resources/ui/light.qss","app/src/widgets/textnavigation.h","app/src/widgets/textlayoutguard.h","app/src/widgets/markdownedit.cpp"]
---

# 剪贴板阅读与编辑导航

模式：Operate / Read。分支 `feat/v3.6.1-clipboard-readability`，版本 3.6.1，用户已于 2026-11-14 验收并授权合入、推送和发布。

THESIS：放大实际阅读内容，并用清晰的标签轮廓表达分类和当前选择。

OWN-WORLD：沿用 Microsoft YaHei UI、灰白工作面、雾蓝选中和原生 Qt 操作；正文 18pt、页签 11pt，局部页签使用 6px 圆角和 1px 边框，不改变主工作面的连续分区。

STORY：选择标签、浏览列表、阅读或编辑内容；首行按 ↑ 到开头，末行按 ↓ 到末尾。保留搜索、复制、分类、排序、保存和失败保护。

FIRST VIEWPORT：验证 1180×760 与 980×640；列表行高随字体适配，预览继续换行和滚动。多标签保留名称宽度并横向滚动，长标签省略，操作按钮始终可达。

FORM：既有界面的小范围改进，无栅格素材、动画或新字体依赖。

验证记录与截图位置见 `docs/development/clipboard-readability.md`。两轮截图检查完成；首轮标签挤压问题已修复。四个相关 CTest 套件通过，开发版版本为 3.6.1。

最终独立复核：可交付开发版，未发现阻断问题。原生 Qt 检查替代不适用的网页检测，本轮无交付栅格素材。

后续反馈：标签相连叠压 8px，未选中下沉 4px，选中由 Qt 最后绘制；补齐双向编辑边界。最终开发构建及两个受影响套件通过，最新截图确认叠压与滚动显示正常。
