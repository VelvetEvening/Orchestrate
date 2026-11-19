#include "theme.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFontDatabase>
#include <QHash>
#include <QIconEngine>
#include <QLayout>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QAbstractButton>
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
enum class Palette { Mist, Paper, Precision, Mamba };
Palette activePalette = Palette::Mist;
bool compactLayout = false;

struct PaletteSample {
    const char *id;
    const char *sidebar;
    const char *accent;
};

constexpr PaletteSample samples[] = {
    {"mist", "#eff3f4", "#365f8c"},
    {"paper", "#f0ede7", "#80523e"},
    {"precision", "#e5ebf2", "#244f78"},
    {"mamba", "#552583", "#fdb927"}
};

Palette findPalette(const QString &id)
{
    for (const auto p : {Palette::Mist, Palette::Paper, Palette::Precision, Palette::Mamba})
        if (id == QLatin1String(samples[int(p)].id)) return p;
    return Palette::Mist;
}

const char *colorForPalette(const char *mist, const char *paper, const char *precision, const char *mamba)
{
    const char *colors[] = {mist, paper, precision, mamba};
    return colors[int(activePalette)];
}

const char *captionColor()
{
    return activePalette == Palette::Mamba ? "#280f35" : UiTheme::Sidebar;
}

// Widgets and models keep their QIcons across a live palette switch.
class PaletteIconEngine final : public QIconEngine
{
public:
    explicit PaletteIconEngine(const QString &name)
        : name_(name), source_(QStringLiteral(":/ui/icons/%1.svg").arg(name)) {}

    QIconEngine *clone() const override { return new PaletteIconEngine(name_); }
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
        if (!UiTheme::isDark()) return source_.pixmap(size, scale, mode, state);
        auto image = source_.pixmap(size, scale, QIcon::Normal, state);
        if (image.isNull()) return image;
        const char *ink = UiTheme::Text;
        if (mode == QIcon::Disabled) ink = UiTheme::Disabled;
        else if (name_ == QStringLiteral("project-complete")) ink = UiTheme::Success;
        else if (name_ == QStringLiteral("project-active")) ink = UiTheme::Accent;
        QPainter painter(&image);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(QRectF(QPointF(), image.deviceIndependentSize()), QColor(ink));
        painter.end();
        return image;
    }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        if (!UiTheme::isDark()) {
            source_.paint(painter, rect, Qt::AlignCenter, mode, state);
            return;
        }
        const auto image = scaledPixmap(rect.size(), mode, state, painter->device()->devicePixelRatioF());
        const auto position = QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, image.deviceIndependentSize().toSize(), rect);
        painter->drawPixmap(position.topLeft(), image);
    }
private:
    QString name_;
    QIcon source_;
};

void refreshItemForegrounds(QAbstractItemView *view, const QHash<QRgb, QColor> &colors)
{
    const auto column = view->property("themeForegroundColumn");
    auto *model = view->model();
    if (!column.isValid() || !model) return;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, column.toInt());
        const auto value = index.data(Qt::ForegroundRole);
        if (!value.canConvert<QBrush>()) continue;
        const auto color = value.value<QBrush>().color();
        const auto replacement = colors.constFind(color.rgba());
        if (replacement != colors.cend() && color != replacement.value())
            model->setData(index, replacement.value(), Qt::ForegroundRole);
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
    const BOOL dark = UiTheme::isDark();
    const auto nativeColor = [](const char *token) {
        const QColor color(QString::fromLatin1(token));
        return RGB(color.red(), color.green(), color.blue());
    };
    const COLORREF caption = nativeColor(captionColor());
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
QString paletteId() { return QString::fromLatin1(samples[int(activePalette)].id); }
bool isDark() { return activePalette == Palette::Mamba; }
bool isCompact() { return compactLayout; }
QString layoutId()
{
    return compactLayout ? QStringLiteral("compact") : QStringLiteral("standard");
}

void install(QApplication &app)
{
    if (!app.property("orchestrateThemeInstalled").toBool())
        apply(app, QStringLiteral("mist"), QStringLiteral("standard"));
}

static void applyPalette(QApplication &app)
{
    const auto &sample = samples[int(activePalette)];
    Background   = colorForPalette("#f7f9fa", "#f7f5f0", "#f4f6f8", "#291c31");
    Sidebar = sample.sidebar;
    IndexSurface = colorForPalette(Background, Background, "#edf1f6", "#2d1e34");
    Surface      = colorForPalette("#ffffff", "#fffdf8", "#ffffff", "#24172b");
    Text         = colorForPalette("#2f3b48", "#383734", "#253246", "#f8eddf");
    Muted        = colorForPalette("#5e6c78", "#6e6a64", "#536277", "#c4afcb");
    Border       = colorForPalette("#dfe6ea", "#e2ded6", "#d8e0e8", "#543d5d");
    ControlBorder = colorForPalette("#b3bfcd", "#b8b0a6", "#a8b6c7", "#8c7198");
    Accent = sample.accent;
    Selection    = colorForPalette("#dfe9f2", "#ece8e1", "#dce8f3", "#52364f");
    Hover        = colorForPalette("#edf2f5", "#f3f0ea", "#edf2f5", "#432c4d");
    Focus        = colorForPalette("#6f8fb6", "#96765f", "#6f8fb6", "#c5a6d6");
    DiaryMarker  = colorForPalette("#648098", "#986d54", "#58758f", "#e8b75a");
    ReminderMarker = colorForPalette("#887789", "#8a7786", "#82708c", "#cf9fe8");
    const bool dark = isDark();
    Success = dark ? "#92d3ae" : "#386b50";
    SuccessBackground = dark ? "#243b34" : "#edf5ef";
    Warning = dark ? "#f4c779" : "#87611e";
    WarningBackground = dark ? "#443324" : "#faf3e5";
    Danger = dark ? "#f8a6ad" : "#a64545";
    DangerBackground = dark ? "#492731" : "#fbefef";
    DangerHoverBorder = dark ? "#a25e70" : "#e6c5c5";
    Disabled = dark ? "#a08aa7" : "#919ba9";
    app.styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
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
        palette.setColor(group, QPalette::Accent, QColor(Accent));
        palette.setColor(group, QPalette::Light, QColor(Surface));
        palette.setColor(group, QPalette::Midlight, QColor(Hover));
        palette.setColor(group, QPalette::Mid, QColor(ControlBorder));
        palette.setColor(group, QPalette::Dark, QColor(Muted));
    }
    app.setPalette(palette);
}

void apply(QApplication &app, const QString &paletteId, const QString &layoutId)
{
    const auto selected = findPalette(paletteId);
    const bool paper = selected == Palette::Paper;
    const bool dark = selected == Palette::Mamba;
    const bool compact = layoutId == QStringLiteral("compact");
    const bool installed = app.property("orchestrateThemeInstalled").toBool();
    const bool paletteChanged = !installed || selected != activePalette;
    const bool layoutChanged = !installed || compact != compactLayout;
    if (!paletteChanged && !layoutChanged) return;

    const QColor oldMuted(Muted), oldAccent(Accent), oldSuccess(Success), oldWarning(Warning);
    activePalette = selected;
    compactLayout = compact;
    initializeUiResources();
    if (paletteChanged) applyPalette(app);
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
        if (dark) {
            QFile accents(QStringLiteral(":/ui/mamba.qss"));
            if (accents.open(QIODevice::ReadOnly)) css += QString::fromUtf8(accents.readAll());
        }
        const std::pair<const char *, const char *> tokens[] = {
            {"background", Background}, {"sidebar", Sidebar}, {"surface", Surface}, {"indexSurface", IndexSurface},
            {"text", Text}, {"muted", Muted}, {"border", Border}, {"controlBorder", ControlBorder},
            {"accent", Accent}, {"selection", Selection}, {"hover", Hover}, {"focus", Focus},
            {"header", captionColor()}, {"controlSurface", dark ? "#38263f" : Surface},
            {"icons", dark ? ":/ui/icons/dark" : ":/ui/icons"},
            {"primaryText", dark ? Surface : Accent}, {"checkFill", dark ? Accent : Selection},
            {"primaryBackground", colorForPalette(Selection, "#f4ebe2", Selection, Accent)},
            {"primaryBorder", colorForPalette("transparent", "#e2d5c8", "transparent", Accent)},
            {"primaryHover", colorForPalette("#d2e0ee", "#ece0d5", "#d2e0ee", "#ffcd64")},
            {"primaryHoverBorder", paper ? "#b69a84" : Focus},
            {"primaryPressed", colorForPalette("#c4d6e7", "#e6d7c9", "#c4d6e7", "#dfa22a")},
            {"navHover", colorForPalette("#e5ebee", "#e9e5de", "#e5ebee", "#593469")},
            {"progress", colorForPalette("#c7d7ed", "#e2d5ca", "#c7d7ed", "#674b6f")},
            {"scrollHandle", colorForPalette("#bdc8d3", "#c8c1b8", "#bdc8d3", "#705478")},
            {"scrollHandleHover", colorForPalette("#97a8b9", "#a39a8e", "#97a8b9", "#987da4")},
            {"success", Success}, {"successBackground", SuccessBackground},
            {"warning", Warning}, {"warningBackground", WarningBackground},
            {"danger", Danger}, {"dangerBackground", DangerBackground},
            {"dangerHoverBorder", DangerHoverBorder}, {"disabled", Disabled},
            {"controlPadding", compact ? "4px 9px" : "6px 11px"},
            {"controlRadius", compact ? "4px" : "6px"},
            {"inputPadding", compact ? "4px 8px" : "6px 9px"},
            {"choiceItemPadding", compact ? "5px 24px 5px 7px" : "8px 26px 8px 9px"},
            {"navPadding", compact ? "6px 10px" : "10px 12px"},
            {"navRadius", compact ? "4px" : "7px"},
            {"tabPadding", compact ? "4px 9px" : "7px 12px"},
            {"calendarPadding", compact ? "4px" : "7px"},
            {"calendarBottomInset", compact ? "5em" : "0px"},
            {"toolItemPadding", compact ? "3px 5px" : "8px 5px"},
            {"itemPadding", compact ? "3px 8px" : "7px 10px"},
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
    const QHash<QRgb, QColor> itemColors {{oldMuted.rgba(), QColor(Muted)}, {oldAccent.rgba(), QColor(Accent)},
                                          {oldSuccess.rgba(), QColor(Success)}, {oldWarning.rgba(), QColor(Warning)}};
    for (QWidget *widget : QApplication::allWidgets()) {
        if (layoutChanged && widget->layout()) updateLayoutMetrics(widget->layout());
        widget->update();
        if (!paletteChanged) continue;
        if (auto *calendar = qobject_cast<QCalendarWidget *>(widget))
            applyCalendarPalette(calendar);
        if (auto *view = qobject_cast<QAbstractItemView *>(widget))
            refreshItemForegrounds(view, itemColors);
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
    return QIcon(new PaletteIconEngine(name));
}

QIcon paletteSwatch(const QString &id)
{
    const auto &sample = samples[int(findPalette(id))];
    const QSize size(22, 16);
    QPixmap pixmap(size * 4);
    pixmap.setDevicePixelRatio(4);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(1, 1.5, 20, 13), 4, 4);
    painter.fillPath(shape, QColor(sample.sidebar));
    painter.setClipPath(shape);
    painter.fillRect(QRectF(12, 1, 10, 14), QColor(sample.accent));
    painter.setClipping(false);
    QColor outline(sample.accent);
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

void applyCalendarPalette(QCalendarWidget *calendar)
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
} // namespace UiTheme
