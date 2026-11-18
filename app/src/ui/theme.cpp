#include "theme.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFontDatabase>
#include <QLayout>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QStyleHints>
#include <QWidget>
#include <QCalendarWidget>
#include <QTextCharFormat>
#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QSyntaxHighlighter>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

static void initializeUiResources()
{
    static const bool initialized = [] {
        Q_INIT_RESOURCE(uitheme);
        Q_INIT_RESOURCE(resources);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace {
UiTheme::Preset activePreset = UiTheme::Preset::Mist;

void refreshItemForegrounds(QAbstractItemView *view, const QColor &oldMuted, const QColor &oldAccent)
{
    const auto column = view->property("themeForegroundColumn");
    auto *model = view->model();
    if (!column.isValid() || !model) return;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, column.toInt());
        const auto value = index.data(Qt::ForegroundRole);
        if (!value.canConvert<QBrush>()) continue;
        const auto color = value.value<QBrush>().color();
        if (color == oldMuted) model->setData(index, QColor(UiTheme::Muted), Qt::ForegroundRole);
        else if (color == oldAccent) model->setData(index, QColor(UiTheme::Accent), Qt::ForegroundRole);
    }
}

void updateLayoutMetrics(QLayout *layout)
{
    const auto normalMargins = layout->property("themeMargins");
    if (!normalMargins.isValid()) return;
    QMargins margins = normalMargins.value<QMargins>();
    int spacing = layout->property("themeSpacing").toInt();
    if (UiTheme::preset() == UiTheme::Preset::Precision) {
        const auto compact = [](int value) {
            return value > 0 ? qMax(4, qRound(value / 6.0) * 4) : 0;
        };
        margins = {compact(margins.left()), compact(margins.top()),
                   compact(margins.right()), compact(margins.bottom())};
        if (spacing > 0) spacing = qMax(4, spacing * 2 / 3);
    }
    layout->setContentsMargins(margins);
    if (spacing >= 0) layout->setSpacing(spacing);
}

void lightCaption(QWidget *window)
{
#ifdef Q_OS_WIN
    if (QApplication::platformName() != QStringLiteral("windows")) return;
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL dark = FALSE;
    const auto nativeColor = [](const char *token) {
        const QColor color(QString::fromLatin1(token));
        return RGB(color.red(), color.green(), color.blue());
    };
    const COLORREF caption = nativeColor(UiTheme::Sidebar);
    const COLORREF text = nativeColor(UiTheme::Text);
    const COLORREF border = nativeColor(UiTheme::Border);
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36, &text, sizeof(text));
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
#else
    Q_UNUSED(window);
#endif
}

class WindowTheme final : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() != QEvent::Show) return false;
        auto *widget = qobject_cast<QWidget *>(object);
        if (!widget) return false;
        if (widget->isWindow() && (widget->windowType() == Qt::Window || widget->windowType() == Qt::Dialog))
            lightCaption(widget);
        if (auto *box = qobject_cast<QDialogButtonBox *>(widget)) {
            for (auto *button : box->buttons()) {
                const auto role = box->buttonRole(button);
                if (role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::YesRole)
                    UiTheme::setRole(button, "primary");
            }
        }
        return false;
    }
};
}

UiTheme::Preset UiTheme::preset() { return activePreset; }
QString UiTheme::presetId()
{
    return activePreset == Preset::Paper ? QStringLiteral("paper")
        : activePreset == Preset::Precision ? QStringLiteral("precision") : QStringLiteral("mist");
}

void UiTheme::install(QApplication &app)
{
    if (!app.property("orchestrateThemeInstalled").toBool()) apply(app, QStringLiteral("mist"));
}

void UiTheme::apply(QApplication &app, const QString &id)
{
    const auto selected = id == QStringLiteral("paper") ? Preset::Paper
        : id == QStringLiteral("precision") ? Preset::Precision : Preset::Mist;
    const bool installed = app.property("orchestrateThemeInstalled").toBool();
    if (installed && selected == activePreset) return;
    const QColor oldMuted(Muted), oldAccent(Accent);
    activePreset = selected;
    const bool paper = activePreset == Preset::Paper;
    const bool compact = activePreset == Preset::Precision;
    Background = paper ? "#f7f5f0" : compact ? "#f4f6f8" : "#f7f9fa";
    Sidebar = paper ? "#f0ede7" : compact ? "#e5ebf2" : "#eff3f4";
    IndexSurface = compact ? "#edf1f6" : Background;
    Surface = paper ? "#fffdf8" : "#ffffff";
    Text = paper ? "#383734" : compact ? "#253246" : "#2f3b48";
    Muted = paper ? "#6e6a64" : compact ? "#536277" : "#5e6c78";
    Border = paper ? "#e2ded6" : compact ? "#d8e0e8" : "#dfe6ea";
    ControlBorder = paper ? "#b8b0a6" : compact ? "#a8b6c7" : "#b3bfcd";
    Accent = paper ? "#80523e" : compact ? "#244f78" : "#365f8c";
    Selection = paper ? "#ece8e1" : compact ? "#dce8f3" : "#dfe9f2";
    Hover = paper ? "#f3f0ea" : "#edf2f5";
    Focus = paper ? "#96765f" : "#6f8fb6";
    DiaryMarker = paper ? "#986d54" : compact ? "#58758f" : "#648098";
    ReminderMarker = paper ? "#8a7786" : compact ? "#82708c" : "#887789";
    initializeUiResources();
    app.styleHints()->setColorScheme(Qt::ColorScheme::Light);
    QPalette palette;
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const QColor ink(group == QPalette::Disabled ? Disabled : Text);
        palette.setColor(group, QPalette::Window, QColor(Background));
        palette.setColor(group, QPalette::WindowText, ink);
        palette.setColor(group, QPalette::Base, QColor(Surface));
        palette.setColor(group, QPalette::AlternateBase, QColor(Background));
        palette.setColor(group, QPalette::Text, ink);
        palette.setColor(group, QPalette::Button, QColor(Surface));
        palette.setColor(group, QPalette::ButtonText, ink);
        palette.setColor(group, QPalette::BrightText, QColor(Danger));
        palette.setColor(group, QPalette::PlaceholderText, QColor(Muted));
        palette.setColor(group, QPalette::Highlight, QColor(Selection));
        palette.setColor(group, QPalette::HighlightedText, QColor(Accent));
        palette.setColor(group, QPalette::ToolTipBase, QColor(Surface));
        palette.setColor(group, QPalette::ToolTipText, QColor(Text));
        palette.setColor(group, QPalette::Link, QColor(Accent));
        palette.setColor(group, QPalette::Light, QColor(Surface));
        palette.setColor(group, QPalette::Midlight, QColor(Hover));
        palette.setColor(group, QPalette::Mid, QColor(ControlBorder));
        palette.setColor(group, QPalette::Dark, QColor(Muted));
    }
    app.setPalette(palette);
    if (!installed) {
        QFont font = app.font();
        if (QFontDatabase::hasFamily(QStringLiteral("Microsoft YaHei UI")))
            font.setFamily(QStringLiteral("Microsoft YaHei UI"));
        font.setPointSizeF(10.0);
        app.setFont(font);
    }
    QFile sheet(QStringLiteral(":/ui/light.qss"));
    if (sheet.open(QIODevice::ReadOnly)) {
        QString css = QString::fromUtf8(sheet.readAll());
        const std::pair<const char *, const char *> tokens[] = {
            {"background", Background}, {"sidebar", Sidebar}, {"surface", Surface}, {"indexSurface", IndexSurface},
            {"text", Text}, {"muted", Muted}, {"border", Border}, {"controlBorder", ControlBorder},
            {"accent", Accent}, {"selection", Selection}, {"hover", Hover}, {"focus", Focus},
            {"primaryBackground", paper ? "#f4ebe2" : Selection}, {"primaryBorder", paper ? "#e2d5c8" : "transparent"},
            {"primaryHover", paper ? "#ece0d5" : "#d2e0ee"}, {"primaryHoverBorder", paper ? "#b69a84" : Focus},
            {"primaryPressed", paper ? "#e6d7c9" : "#c4d6e7"},
            {"navHover", paper ? "#e9e5de" : "#e5ebee"}, {"progress", paper ? "#e2d5ca" : "#c7d7ed"},
            {"scrollHandle", paper ? "#c8c1b8" : "#bdc8d3"}, {"scrollHandleHover", paper ? "#a39a8e" : "#97a8b9"},
            {"success", Success}, {"successBackground", SuccessBackground},
            {"warning", Warning}, {"warningBackground", WarningBackground},
            {"danger", Danger}, {"dangerBackground", DangerBackground},
            {"dangerHoverBorder", DangerHoverBorder}, {"disabled", Disabled},
            {"controlPadding", compact ? "4px 9px" : paper ? "7px 12px" : "6px 11px"},
            {"controlRadius", compact ? "4px" : "6px"},
            {"inputPadding", compact ? "4px 8px" : "6px 9px"},
            {"navPadding", compact ? "6px 10px" : "10px 12px"},
            {"navRadius", compact ? "4px" : "7px"},
            {"tabPadding", compact ? "4px 9px" : "7px 12px"},
            {"calendarPadding", compact ? "4px" : "7px"},
            {"calendarBottomInset", compact ? "5em" : "0px"},
            {"toolItemPadding", compact ? "3px 5px" : paper ? "9px 7px" : "8px 5px"},
            {"itemPadding", compact ? "3px 8px" : paper ? "9px 10px" : "7px 10px"},
            {"recordMinHeight", compact ? "30px" : "42px"},
            {"diaryMinHeight", compact ? "30px" : "40px"},
            {"collectionMinHeight", compact ? "34px" : "46px"}
        };
        for (const auto &token : tokens)
            css.replace(QStringLiteral("@%1@").arg(QString::fromLatin1(token.first)), QString::fromLatin1(token.second));
        app.setStyleSheet(css);
    }
    if (!installed) {
        app.installEventFilter(new WindowTheme(&app));
        app.setProperty("orchestrateThemeInstalled", true);
    }
    // Repaint existing windows without rebuilding pages or touching editor drafts.
    for (QWidget *widget : QApplication::allWidgets()) {
        if (widget->layout()) updateLayoutMetrics(widget->layout());
        if (auto *calendar = qobject_cast<QCalendarWidget *>(widget))
            applyCalendarPalette(calendar);
        if (auto *view = qobject_cast<QAbstractItemView *>(widget))
            refreshItemForegrounds(view, oldMuted, oldAccent);
        if (auto *edit = qobject_cast<QPlainTextEdit *>(widget)) {
            for (auto *highlighter : edit->document()->findChildren<QSyntaxHighlighter *>())
                highlighter->rehighlight();
        }
        widget->update();
        if (widget->isWindow() && widget->isVisible()
            && (widget->windowType() == Qt::Window || widget->windowType() == Qt::Dialog)) lightCaption(widget);
    }
}

QIcon UiTheme::icon(const QString &name)
{
    initializeUiResources();
    return QIcon(QStringLiteral(":/ui/icons/%1.svg").arg(name));
}

void UiTheme::setRole(QWidget *widget, const char *role)
{
    const auto value = QString::fromLatin1(role);
    if (widget->property("uiRole").toString() == value) return;
    widget->setProperty("uiRole", value);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

void UiTheme::setSurface(QWidget *widget, const char *surface)
{
    widget->setAttribute(Qt::WA_StyledBackground, true);
    widget->setProperty("workspaceSurface", QString::fromLatin1(surface));
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

void UiTheme::setLayoutMetrics(QLayout *layout, const QMargins &margins, int spacing)
{
    layout->setProperty("themeMargins", QVariant::fromValue(margins));
    layout->setProperty("themeSpacing", spacing);
    updateLayoutMetrics(layout);
}

void UiTheme::applyCalendarPalette(QCalendarWidget *calendar)
{
    auto palette = calendar->palette();
    palette.setColor(QPalette::Highlight, QColor(Selection));
    palette.setColor(QPalette::HighlightedText, QColor(Accent));
    calendar->setPalette(palette);

    QTextCharFormat header;
    header.setForeground(QColor(Muted));
    header.setBackground(QColor(IndexSurface));
    calendar->setHeaderTextFormat(header);
    calendar->setWeekdayTextFormat(Qt::Saturday, header);
    calendar->setWeekdayTextFormat(Qt::Sunday, header);
}
