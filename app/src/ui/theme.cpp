#include "theme.h"
#include "skinlibrary.h"

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QApplication>
#include <QCalendarWidget>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFontDatabase>
#include <QIconEngine>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QStyle>
#include <QStyleHints>
#include <QSyntaxHighlighter>
#include <QTemporaryDir>
#include <QTextCharFormat>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <algorithm>
#include <array>
#include <cmath>

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
enum class Role {
    Background, Sidebar, Surface, IndexSurface, Header,
    Text, Muted, Border, ControlBorder, Accent, Selection, SelectionText, Hover, Focus,
    ControlSurface, ControlText, ControlHover, ControlPressed,
    PrimaryBackground, PrimaryBorder, PrimaryText, PrimaryHover, PrimaryHoverBorder, PrimaryPressed,
    NavHover, Progress, ScrollHandle, ScrollHandleHover,
    Success, SuccessBackground, Warning, WarningBackground, Danger, DangerBackground, DangerHoverBorder,
    Disabled, DiaryMarker, ReminderMarker, Icon, CheckFill, CheckInk, GraphSecondary, GraphTertiary, Count
};

struct ColorToken {
    const char *name;
    const char *mist;
    const char *paper;
    const char *dark;
};

constexpr ColorToken colorTokens[] = {
    {"background", "#f7f9fa", "#f7f5f0", "#202328"},
    {"sidebar", "#eff3f4", "#f0ede7", "#272c33"},
    {"surface", "#ffffff", "#fffdf8", "#252930"},
    {"indexSurface", nullptr, nullptr, nullptr},
    {"header", nullptr, nullptr, nullptr},
    {"text", "#2f3b48", "#383734", "#e8edf2"},
    {"muted", "#5e6c78", "#6e6a64", "#a9b4bf"},
    {"border", "#dfe6ea", "#e2ded6", "#3c444e"},
    {"controlBorder", "#b3bfcd", "#b8b0a6", "#737e8a"},
    {"accent", "#365f8c", "#80523e", "#a5c9f0"},
    {"selection", "#dfe9f2", "#ece8e1", "#354554"},
    {"selectionText", nullptr, nullptr, nullptr},
    {"hover", "#edf2f5", "#f3f0ea", "#303842"},
    {"focus", "#6f8fb6", "#96765f", "#8baac8"},
    {"controlSurface", nullptr, nullptr, nullptr},
    {"controlText", nullptr, nullptr, nullptr},
    {"controlHover", nullptr, nullptr, nullptr},
    {"controlPressed", nullptr, nullptr, nullptr},
    {"primaryBackground", nullptr, "#f4ebe2", nullptr},
    {"primaryBorder", "transparent", "#e2d5c8", nullptr},
    {"primaryText", nullptr, nullptr, nullptr},
    {"primaryHover", "#d2e0ee", "#ece0d5", nullptr},
    {"primaryHoverBorder", nullptr, "#b69a84", nullptr},
    {"primaryPressed", "#c4d6e7", "#e6d7c9", nullptr},
    {"navHover", "#e5ebee", "#e9e5de", "#323a44"},
    {"progress", "#c7d7ed", "#e2d5ca", "#3a4f64"},
    {"scrollHandle", "#bdc8d3", "#c8c1b8", "#566471"},
    {"scrollHandleHover", "#97a8b9", "#a39a8e", "#738493"},
    {"success", "#386b50", "#386b50", "#93d6ad"},
    {"successBackground", "#edf5ef", "#edf5ef", "#254034"},
    {"warning", "#87611e", "#87611e", "#f2cd91"},
    {"warningBackground", "#faf3e5", "#faf3e5", "#423622"},
    {"danger", "#a64545", "#a64545", "#ffb0b0"},
    {"dangerBackground", "#fbefef", "#fbefef", "#4b2c30"},
    {"dangerHoverBorder", "#e6c5c5", "#e6c5c5", "#a76e73"},
    {"disabled", "#919ba9", "#919ba9", "#88929e"},
    {"diaryMarker", "#648098", "#986d54", "#9dbbd4"},
    {"reminderMarker", "#887789", "#8a7786", "#b1a4cc"},
    {"icon", "#637184", "#637184", nullptr},
    {"checkFill", nullptr, nullptr, nullptr},
    {"checkInk", "#41638f", nullptr, nullptr},
    {"graphSecondary", "#805889", "#805889", "#d8a9d0"},
    {"graphTertiary", "#32747b", "#32747b", "#8bcbd5"}
};
static_assert(std::size(colorTokens) == size_t(Role::Count));

struct ResolvedTheme {
    QString id;
    QString sidebarImagePath;
    bool dark = false;
    std::array<QColor, size_t(Role::Count)> colors;

    bool operator==(const ResolvedTheme &other) const
    {
        return id == other.id && sidebarImagePath == other.sidebarImagePath
            && dark == other.dark && colors == other.colors;
    }
};

QList<SkinTheme> installed;
bool compactLayout = false;

QColor mix(const QColor &base, const QColor &ink, qreal amount)
{
    const auto channel = [amount](int from, int to) { return qRound(from + amount * (to - from)); };
    return QColor(channel(base.red(), ink.red()), channel(base.green(), ink.green()),
                  channel(base.blue(), ink.blue()));
}

qreal luminance(const QColor &color)
{
    const auto linear = [](qreal channel) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

QColor readableInk(const QColor &background, const QColor &preferred, const QColor &alternative)
{
    const qreal base = luminance(background);
    for (const auto &ink : {preferred, alternative}) {
        const qreal shade = luminance(ink);
        const qreal contrast = (qMax(base, shade) + 0.05) / (qMin(base, shade) + 0.05);
        if (contrast >= 4.5) return ink;
    }
    return base < 0.179 ? Qt::white : Qt::black;
}

ResolvedTheme resolveTheme(const QString &id)
{
    const bool paper = id == QStringLiteral("paper");
    const auto entry = std::find_if(installed.cbegin(), installed.cend(), [&id](const SkinTheme &theme) {
        return theme.id == id;
    });
    const bool custom = id != QStringLiteral("mist") && !paper && id != QStringLiteral("precision")
        && entry != installed.cend();
    ResolvedTheme result;
    result.id = custom || paper ? id : QStringLiteral("mist");
    result.dark = custom && entry->base == QStringLiteral("dark");
    if (custom) result.sidebarImagePath = entry->sidebarImagePath;

    for (size_t i = 0; i < std::size(colorTokens); ++i) {
        const auto &token = colorTokens[i];
        const auto *fallback = result.dark ? token.dark : paper ? token.paper : token.mist;
        if (fallback) result.colors[i] = QColor(QString::fromLatin1(fallback));
        if (!custom) continue;
        const auto value = entry->colors.value(QString::fromLatin1(token.name));
        if (value.size() != 7 || value.front() != QLatin1Char('#')) continue;
        const auto override = QColor::fromString(value);
        if (override.isValid()) result.colors[i] = override;
    }

    const auto color = [&result](Role role) -> QColor & { return result.colors[size_t(role)]; };
    const auto fallback = [&color](Role role, const QColor &value) {
        if (!color(role).isValid()) color(role) = value;
    };
    const auto &accent = color(Role::Accent);
    const auto &selection = color(Role::Selection);
    const auto &text = color(Role::Text);
    const auto &surface = color(Role::Surface);
    fallback(Role::IndexSurface, color(Role::Background));
    fallback(Role::Header, color(Role::Sidebar));
    fallback(Role::SelectionText, readableInk(selection, accent, text));
    fallback(Role::ControlSurface, surface);
    fallback(Role::ControlText, readableInk(color(Role::ControlSurface), text, accent));
    fallback(Role::ControlHover, color(Role::Hover));
    fallback(Role::ControlPressed, selection);
    fallback(Role::PrimaryBackground, result.dark ? accent : selection);
    const auto &primary = color(Role::PrimaryBackground);
    fallback(Role::PrimaryBorder, primary);
    fallback(Role::PrimaryText, readableInk(primary, result.dark ? surface : accent, text));
    fallback(Role::PrimaryHover, mix(primary, text, 0.14));
    fallback(Role::PrimaryHoverBorder, color(Role::Focus));
    fallback(Role::PrimaryPressed, mix(primary, surface, 0.19));
    fallback(Role::Icon, readableInk(color(Role::ControlSurface), color(Role::Muted), text));
    fallback(Role::CheckFill, result.dark ? accent : selection);
    fallback(Role::CheckInk, readableInk(color(Role::CheckFill), result.dark ? surface : accent, text));
    return result;
}

ResolvedTheme active = resolveTheme(QStringLiteral("mist"));

const QColor &currentColor(Role role)
{
    return active.colors[size_t(role)];
}

QString uiIconSource(const QString &name)
{
    return QStringLiteral(":/ui/icons/%1.svg").arg(name);
}

// Retained icons always paint with the active colors, even when their widget already existed.
class PaletteIconEngine final : public QIconEngine
{
public:
    PaletteIconEngine(const QString &path, Role role) : path_(path), source_(path), role_(role) {}

    QIconEngine *clone() const override { return new PaletteIconEngine(path_, role_); }
    bool isNull() override { return source_.isNull(); }
    QSize actualSize(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return source_.actualSize(size, mode, state);
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        auto image = source_.pixmap(size, scale, QIcon::Normal, state);
        if (!image.isNull()) {
            const auto role = mode == QIcon::Disabled ? Role::Disabled
                : (mode == QIcon::Selected || state == QIcon::On) && role_ == Role::Icon ? Role::SelectionText : role_;
            QPainter painter(&image);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(QRectF(QPointF(), image.deviceIndependentSize()), currentColor(role));
        }
        return image;
    }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const auto image = scaledPixmap(rect.size(), mode, state, painter->device()->devicePixelRatioF());
        const auto position = QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter,
                                                 image.deviceIndependentSize().toSize(), rect);
        painter->drawPixmap(position.topLeft(), image);
    }

private:
    QString path_;
    QIcon source_;
    Role role_;
};

// Stylesheet subcontrols require image filenames. Tint the bundled SVGs by color
// and use a Qt search path so the stylesheet never interpolates a filesystem path.
QString styleIconSource(const QString &name, Role role)
{
    static const QTemporaryDir directory;
    static const QString prefix = QStringLiteral("orchestrateThemeIcons");
    static const bool available = [] {
        if (!directory.isValid()) return false;
        QDir::setSearchPaths(prefix, {directory.path()});
        return true;
    }();
    const auto sourcePath = uiIconSource(name);
    if (!available) return sourcePath;
    const auto ink = currentColor(role).name();
    const auto filename = name + QLatin1Char('-') + ink.mid(1) + QStringLiteral(".svg");
    const auto path = prefix + QLatin1Char(':') + filename;
    QFile destination(directory.filePath(filename));
    if (destination.exists()) return path;
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) return sourcePath;
    static const QRegularExpression colorPattern(QStringLiteral("#[0-9a-fA-F]{6}\\b"));
    auto svg = QString::fromUtf8(source.readAll()).replace(colorPattern, ink).toUtf8();
    if (!destination.open(QIODevice::WriteOnly)) return sourcePath;
    if (destination.write(svg) != svg.size() || !destination.flush()) {
        destination.remove();
        return sourcePath;
    }
    return path;
}

constexpr int ForegroundColorRole = Qt::UserRole + 70;

int colorIndex(QLatin1StringView token)
{
    for (size_t i = 0; i < std::size(colorTokens); ++i) {
        if (token == QLatin1String(colorTokens[i].name)) return int(i);
    }
    return -1;
}

void refreshItemForegrounds(QAbstractItemView *view)
{
    const auto column = view->property("themeForegroundColumn");
    auto *model = view->model();
    if (!column.isValid() || !model) return;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, column.toInt());
        bool ok = false;
        const int role = index.data(ForegroundColorRole).toInt(&ok);
        if (ok && role >= 0 && role < int(Role::Count))
            model->setData(index, QBrush(active.colors[role]), Qt::ForegroundRole);
    }
}

void updateLayoutMetrics(QLayout *layout)
{
    const auto normalMargins = layout->property("themeMargins");
    if (!normalMargins.isValid()) return;
    QMargins margins = normalMargins.value<QMargins>();
    int spacing = layout->property("themeSpacing").toInt();
    if (compactLayout) {
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

void applyCaption(QWidget *window)
{
#ifdef Q_OS_WIN
    if (QApplication::platformName() != QStringLiteral("windows")) return;
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL dark = active.dark;
    const auto nativeColor = [](const QColor &color) {
        return RGB(color.red(), color.green(), color.blue());
    };
    const COLORREF caption = nativeColor(currentColor(Role::Header));
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

void updateCalendarArrows(QCalendarWidget *calendar)
{
    const bool rtl = calendar->layoutDirection() == Qt::RightToLeft;
    const auto left = UiTheme::icon(QStringLiteral("chevron-left"));
    const auto right = UiTheme::icon(QStringLiteral("chevron-right"));
    if (auto *previous = calendar->findChild<QToolButton *>(QStringLiteral("qt_calendar_prevmonth")))
        previous->setIcon(rtl ? right : left);
    if (auto *next = calendar->findChild<QToolButton *>(QStringLiteral("qt_calendar_nextmonth")))
        next->setIcon(rtl ? left : right);
}

class WindowTheme final : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::LayoutDirectionChange || event->type() == QEvent::StyleChange) {
            if (auto *calendar = qobject_cast<QCalendarWidget *>(object)) {
                // Qt sets its own arrows when processing the change.
                QTimer::singleShot(0, calendar, [calendar] { updateCalendarArrows(calendar); });
            }
        }
        if (event->type() != QEvent::Show) return false;
        auto *widget = qobject_cast<QWidget *>(object);
        if (!widget) return false;
        if (auto *calendar = qobject_cast<QCalendarWidget *>(widget))
            updateCalendarArrows(calendar);
        if (widget->isWindow() && (widget->windowType() == Qt::Window || widget->windowType() == Qt::Dialog))
            applyCaption(widget);
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

namespace UiTheme {
const QColor &Background = currentColor(Role::Background);
const QColor &Sidebar = currentColor(Role::Sidebar);
const QColor &IndexSurface = currentColor(Role::IndexSurface);
const QColor &Surface = currentColor(Role::Surface);
const QColor &Text = currentColor(Role::Text);
const QColor &Muted = currentColor(Role::Muted);
const QColor &Border = currentColor(Role::Border);
const QColor &ControlBorder = currentColor(Role::ControlBorder);
const QColor &Accent = currentColor(Role::Accent);
const QColor &Selection = currentColor(Role::Selection);
const QColor &SelectionText = currentColor(Role::SelectionText);
const QColor &Focus = currentColor(Role::Focus);
const QColor &DiaryMarker = currentColor(Role::DiaryMarker);
const QColor &ReminderMarker = currentColor(Role::ReminderMarker);
const QColor &Success = currentColor(Role::Success);
const QColor &SuccessBackground = currentColor(Role::SuccessBackground);
const QColor &Warning = currentColor(Role::Warning);
const QColor &Danger = currentColor(Role::Danger);
const QColor &DangerBackground = currentColor(Role::DangerBackground);
const QColor &Disabled = currentColor(Role::Disabled);
const QColor &GraphSecondary = currentColor(Role::GraphSecondary);
const QColor &GraphTertiary = currentColor(Role::GraphTertiary);

QString paletteId() { return active.id; }
bool isCompact() { return compactLayout; }
QString layoutId()
{
    return compactLayout ? QStringLiteral("compact") : QStringLiteral("standard");
}

void setInstalledThemes(const QList<SkinTheme> &themes) { installed = themes; }
QList<SkinTheme> installedThemes() { return installed; }
QString sidebarImagePath() { return active.sidebarImagePath; }

void install(QApplication &app)
{
    if (!app.property("orchestrateThemeInstalled").toBool())
        apply(app, QStringLiteral("mist"), QStringLiteral("standard"));
}

static void applyPalette(QApplication &app)
{
    app.styleHints()->setColorScheme(active.dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    QPalette palette;
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const bool enabled = group != QPalette::Disabled;
        const auto &ink = enabled ? Text : Disabled;
        palette.setColor(group, QPalette::Window, Background);
        palette.setColor(group, QPalette::WindowText, ink);
        palette.setColor(group, QPalette::Base, Surface);
        palette.setColor(group, QPalette::AlternateBase, Background);
        palette.setColor(group, QPalette::Text, ink);
        palette.setColor(group, QPalette::Button, currentColor(Role::ControlSurface));
        palette.setColor(group, QPalette::ButtonText, enabled ? currentColor(Role::ControlText) : Disabled);
        palette.setColor(group, QPalette::BrightText, Danger);
        palette.setColor(group, QPalette::PlaceholderText, enabled ? Muted : Disabled);
        palette.setColor(group, QPalette::Highlight, Selection);
        palette.setColor(group, QPalette::HighlightedText, enabled ? SelectionText : Disabled);
        palette.setColor(group, QPalette::ToolTipBase, Surface);
        palette.setColor(group, QPalette::ToolTipText, Text);
        palette.setColor(group, QPalette::Link, Accent);
        palette.setColor(group, QPalette::LinkVisited, Accent);
        palette.setColor(group, QPalette::Accent, Accent);
        palette.setColor(group, QPalette::Light, Surface);
        palette.setColor(group, QPalette::Midlight, currentColor(Role::Hover));
        palette.setColor(group, QPalette::Mid, ControlBorder);
        palette.setColor(group, QPalette::Dark, Muted);
        palette.setColor(group, QPalette::Shadow, Border);
    }
    app.setPalette(palette);
}

static void applyStylesheet(QApplication &app)
{
    QFile sheet(QStringLiteral(":/ui/light.qss"));
    if (!sheet.open(QIODevice::ReadOnly)) return;
    QString css = QString::fromUtf8(sheet.readAll());
    const auto replace = [&css](const char *token, const QString &value) {
        css.replace(QStringLiteral("@%1@").arg(QString::fromLatin1(token)), value);
    };
    for (size_t i = 0; i < std::size(colorTokens); ++i) {
        const auto &color = active.colors[i];
        replace(colorTokens[i].name, color.alpha() == 0 ? QStringLiteral("transparent") : color.name());
    }
    replace("chevronDownIcon", styleIconSource(QStringLiteral("chevron-down"), Role::Icon));
    replace("chevronUpIcon", styleIconSource(QStringLiteral("chevron-up"), Role::Icon));
    replace("chevronRightIcon", styleIconSource(QStringLiteral("chevron-right"), Role::Icon));
    replace("checkIcon", styleIconSource(QStringLiteral("check"), Role::CheckInk));
    replace("radioIcon", styleIconSource(QStringLiteral("radio"), Role::CheckInk));
    replace("checkDisabledIcon", styleIconSource(QStringLiteral("check"), Role::Disabled));
    replace("radioDisabledIcon", styleIconSource(QStringLiteral("radio"), Role::Disabled));
    const auto &face = currentColor(Role::ControlSurface);
    replace("buttonEdge", mix(face, currentColor(Role::ControlBorder), .65).name());
    replace("buttonTop", mix(face, Qt::white, active.dark ? .045 : .22).name());
    replace("buttonBottom", mix(face, currentColor(Role::ControlBorder), .10).name());
    replace("primaryEdge", mix(currentColor(Role::PrimaryBackground), currentColor(Role::PrimaryText), .23).name());
    const bool compact = compactLayout;
    const std::pair<const char *, const char *> metrics[] = {
        {"controlPadding", compact ? "4px 10px" : "6px 13px"},
        {"controlRadius", compact ? "6px" : "8px"},
        {"inputPadding", compact ? "4px 8px" : "6px 9px"},
        {"choiceItemPadding", compact ? "5px 24px 5px 7px" : "8px 26px 8px 9px"},
        {"navPadding", compact ? "6px 10px" : "10px 12px"},
        {"navRadius", compact ? "6px" : "8px"},
        {"tabPadding", compact ? "4px 9px" : "7px 12px"},
        {"calendarPadding", compact ? "4px" : "7px"},
        {"calendarBottomInset", compact ? "5em" : "0px"},
        {"toolItemPadding", compact ? "3px 5px" : "8px 5px"},
        {"itemPadding", compact ? "3px 8px" : "7px 10px"},
        {"recordMinHeight", compact ? "30px" : "42px"},
        {"diaryMinHeight", compact ? "30px" : "40px"},
        {"collectionMinHeight", compact ? "34px" : "46px"},
        {"appearancePreviewHeight", compact ? "140px" : "164px"},
        {"clipboardListSize", compact ? "10pt" : "11pt"},
        {"clipboardRowHeight", compact ? "30px" : "38px"}
    };
    for (const auto &metric : metrics) replace(metric.first, QString::fromLatin1(metric.second));
    app.setStyleSheet(css);
}

void apply(QApplication &app, const QString &paletteId, const QString &layoutId)
{
    const auto selected = resolveTheme(paletteId);
    const bool compact = layoutId == QStringLiteral("compact");
    const bool alreadyInstalled = app.property("orchestrateThemeInstalled").toBool();
    const bool paletteChanged = !alreadyInstalled || !(selected == active);
    const bool layoutChanged = !alreadyInstalled || compact != compactLayout;
    if (!paletteChanged && !layoutChanged) return;

    active = selected;
    compactLayout = compact;
    initializeUiResources();
    if (paletteChanged) applyPalette(app);
    if (!alreadyInstalled) {
        QFont font = app.font();
        if (QFontDatabase::hasFamily(QStringLiteral("Microsoft YaHei UI")))
            font.setFamily(QStringLiteral("Microsoft YaHei UI"));
        font.setPointSizeF(10.0);
        app.setFont(font);
    }
    applyStylesheet(app);
    if (!alreadyInstalled) {
        app.installEventFilter(new WindowTheme(&app));
        app.setProperty("orchestrateThemeInstalled", true);
    }
    // Repaint existing windows without rebuilding pages or touching editor drafts.
    for (QWidget *widget : QApplication::allWidgets()) {
        if (layoutChanged && widget->layout()) updateLayoutMetrics(widget->layout());
        widget->update();
        if (!paletteChanged) continue;
        if (auto *calendar = qobject_cast<QCalendarWidget *>(widget))
            applyCalendarPalette(calendar);
        if (auto *view = qobject_cast<QAbstractItemView *>(widget))
            refreshItemForegrounds(view);
        if (auto *edit = qobject_cast<QPlainTextEdit *>(widget)) {
            for (auto *highlighter : edit->document()->findChildren<QSyntaxHighlighter *>())
                highlighter->rehighlight();
        }
        if (widget->isWindow() && widget->isVisible()
            && (widget->windowType() == Qt::Window || widget->windowType() == Qt::Dialog)) applyCaption(widget);
    }
}

QIcon icon(const QString &name)
{
    initializeUiResources();
    auto role = Role::Icon;
    auto path = uiIconSource(name);
    if (name == QStringLiteral("project-active")) role = Role::Accent;
    else if (name == QStringLiteral("project-complete")) role = Role::Success;
    else if (name == QStringLiteral("star") || name == QStringLiteral("update-available")) {
        role = Role::Warning;
        path = QStringLiteral(":/icons/%1.svg").arg(name);
    }
    return QIcon(new PaletteIconEngine(path, role));
}

QIcon paletteSwatch(const QString &id)
{
    const auto theme = resolveTheme(id);
    const auto &sidebar = theme.colors[size_t(Role::Sidebar)];
    const auto &accent = theme.colors[size_t(Role::Accent)];
    const QSize size(22, 16);
    QPixmap pixmap(size * 4);
    pixmap.setDevicePixelRatio(4);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(1, 1.5, 20, 13), 4, 4);
    painter.fillPath(shape, sidebar);
    painter.setClipPath(shape);
    painter.fillRect(QRectF(12, 1, 10, 14), accent);
    painter.setClipping(false);
    QColor outline(accent);
    outline.setAlpha(84);
    painter.setPen(QPen(outline, 1));
    painter.drawPath(shape);
    painter.end();

    QIcon swatch(pixmap);
    swatch.addPixmap(pixmap, QIcon::Selected);
    return swatch;
}

void setRole(QWidget *widget, const char *role)
{
    const auto value = QString::fromLatin1(role);
    if (widget->property("uiRole").toString() == value) return;
    widget->setProperty("uiRole", value);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

void setSurface(QWidget *widget, const char *surface)
{
    widget->setAttribute(Qt::WA_StyledBackground, true);
    widget->setProperty("workspaceSurface", QString::fromLatin1(surface));
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

void setLayoutMetrics(QLayout *layout, const QMargins &margins, int spacing)
{
    layout->setProperty("themeMargins", QVariant::fromValue(margins));
    layout->setProperty("themeSpacing", spacing);
    updateLayoutMetrics(layout);
}

void setItemForeground(QAbstractItemModel *model, const QModelIndex &index, const char *colorToken)
{
    const int color = colorIndex(QLatin1String(colorToken));
    if (color < 0) return;
    model->setData(index, color, ForegroundColorRole);
    model->setData(index, QBrush(active.colors[color]), Qt::ForegroundRole);
}

void applyCalendarPalette(QCalendarWidget *calendar)
{
    updateCalendarArrows(calendar);
    auto palette = calendar->palette();
    palette.setColor(QPalette::Highlight, Selection);
    palette.setColor(QPalette::HighlightedText, SelectionText);
    calendar->setPalette(palette);

    QTextCharFormat header;
    header.setForeground(Muted);
    header.setBackground(IndexSurface);
    calendar->setHeaderTextFormat(header);
    calendar->setWeekdayTextFormat(Qt::Saturday, header);
    calendar->setWeekdayTextFormat(Qt::Sunday, header);
    if (calendar->property("themeCalendarToday").toBool()) {
        const auto date = QDate::currentDate();
        auto today = calendar->dateTextFormat(date);
        today.setBackground(Selection);
        calendar->setDateTextFormat(date, today);
    }
}
} // namespace UiTheme
