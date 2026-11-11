#include "theme.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFontDatabase>
#include <QPalette>
#include <QPushButton>
#include <QStyle>
#include <QStyleHints>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

static void initializeUiResources()
{
    static const bool initialized = [] { Q_INIT_RESOURCE(uitheme); return true; }();
    Q_UNUSED(initialized);
}

namespace {
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

void UiTheme::install(QApplication &app)
{
    if (app.property("orchestrateLightTheme").toBool()) return;
    app.setProperty("orchestrateLightTheme", true);
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
    QFont font = app.font();
    if (QFontDatabase::hasFamily(QStringLiteral("Microsoft YaHei UI")))
        font.setFamily(QStringLiteral("Microsoft YaHei UI"));
    font.setPointSizeF(10.0);
    app.setFont(font);
    QFile sheet(QStringLiteral(":/ui/light.qss"));
    if (sheet.open(QIODevice::ReadOnly)) {
        QString css = QString::fromUtf8(sheet.readAll());
        const std::pair<const char *, const char *> tokens[] = {
            {"background", Background}, {"sidebar", Sidebar}, {"surface", Surface}, {"indexSurface", IndexSurface},
            {"text", Text}, {"muted", Muted}, {"border", Border}, {"controlBorder", ControlBorder},
            {"accent", Accent}, {"selection", Selection}, {"hover", Hover}, {"focus", Focus},
            {"success", Success}, {"successBackground", SuccessBackground},
            {"warning", Warning}, {"warningBackground", WarningBackground},
            {"danger", Danger}, {"dangerBackground", DangerBackground}, {"disabled", Disabled}
        };
        for (const auto &token : tokens)
            css.replace(QStringLiteral("@%1@").arg(QString::fromLatin1(token.first)), QString::fromLatin1(token.second));
        app.setStyleSheet(css);
    }
    app.installEventFilter(new WindowTheme(&app));
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

QColor UiTheme::statusColor(const QString &result)
{
    if (result == QStringLiteral("success") || result == QStringLiteral("already")) return QColor(Success);
    if (result == QStringLiteral("failure")) return QColor(Danger);
    if (result == QStringLiteral("running") || result == QStringLiteral("loading")) return QColor(Accent);
    if (result == QStringLiteral("none") || result == QStringLiteral("disabled") || result == QStringLiteral("skipped")) return QColor(Muted);
    return QColor(Warning);
}

QString UiTheme::statusStyle(const QString &result)
{
    const QColor ink = statusColor(result);
    const char *background = ink == QColor(Success) ? SuccessBackground
        : ink == QColor(Danger) ? DangerBackground : ink == QColor(Accent) ? Selection
        : ink == QColor(Muted) ? Hover : WarningBackground;
    return QStringLiteral("color: %1; background: %2; border: none; border-radius: 5px; padding: 3px 9px; font-weight: 500;")
        .arg(ink.name(), QString::fromLatin1(background));
}
