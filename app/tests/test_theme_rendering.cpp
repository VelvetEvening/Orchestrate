#include "ui/skinlibrary.h"
#include "ui/theme.h"
#include "widgets/mathtext.h"

#include <QApplication>
#include <QCalendarWidget>
#include <QImage>
#include <QItemSelectionModel>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>
#include <cmath>

namespace {
const auto NightId = QStringLiteral("test-night");
const auto DayId = QStringLiteral("test-day");
const auto NightAccent = QStringLiteral("#ba955b");
const auto DayAccent = QStringLiteral("#195c66");
const auto DaySelection = QStringLiteral("#cce9e5");
const auto DaySelectionText = QStringLiteral("#142f39");
const auto DayControl = QStringLiteral("#f0f6ec");
const auto DayIcon = QStringLiteral("#204462");
const auto StandardLayout = QStringLiteral("standard");
const auto CompactLayout = QStringLiteral("compact");
const auto MistId = QStringLiteral("mist");

SkinTheme nightTheme()
{
    return {NightId, QStringLiteral("Test night"), QStringLiteral("dark"),
            {{QStringLiteral("accent"), NightAccent}}, {}};
}

SkinTheme dayTheme()
{
    return {DayId, QStringLiteral("Test day"), QStringLiteral("light"),
            {{QStringLiteral("accent"), DayAccent},
             {QStringLiteral("selection"), DaySelection},
             {QStringLiteral("selectionText"), DaySelectionText},
             {QStringLiteral("controlSurface"), DayControl},
             {QStringLiteral("icon"), DayIcon}}, {}};
}

qreal luminance(const QColor &color)
{
    const auto linear = [](qreal channel) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

qreal contrast(const QColor &first, const QColor &second)
{
    const auto a = luminance(first);
    const auto b = luminance(second);
    return (qMax(a, b) + 0.05) / (qMin(a, b) + 0.05);
}

QColor buttonFill(QPushButton &button)
{
    // Sample well inside the button, away from its outline and centered label.
    button.setAttribute(Qt::WA_UnderMouse, false);
    const auto image = button.grab().toImage();
    if (image.isNull()) return {};
    return image.pixelColor(image.width() / 8, image.height() / 2);
}

QColor iconInk(const QIcon &icon)
{
    const auto image = icon.pixmap(QSize(32, 32)).toImage();
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const auto color = image.pixelColor(x, y);
            if (color.alpha() == 255) return color;
        }
    }
    return {};
}
} // namespace

class ThemeRenderingTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qApp->setStyle(QStringLiteral("Fusion"));
    }

    void init()
    {
        UiTheme::setInstalledThemes({nightTheme(), dayTheme()});
        UiTheme::apply(*qApp, MistId, StandardLayout);
    }

    void resolvesThemesWithoutChangingLayout()
    {
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        const QMargins normalMargins(18, 12, 24, 6);
        const int normalSpacing = 12;
        UiTheme::setLayoutMetrics(layout, normalMargins, normalSpacing);
        const QColor mistAccent = UiTheme::Accent;
        const QColor mistBackground = UiTheme::Background;

        UiTheme::apply(*qApp, NightId, StandardLayout);
        QCOMPARE(UiTheme::paletteId(), NightId);
        QVERIFY(UiTheme::isDark());
        QCOMPARE(qApp->palette().color(QPalette::Link), QColor(NightAccent));
        QVERIFY(luminance(UiTheme::Background) < luminance(mistBackground));
        QVERIFY(contrast(UiTheme::Background, UiTheme::Text) >= 4.5);
        QVERIFY(contrast(UiTheme::Selection, UiTheme::SelectionText) >= 4.5);
        QCOMPARE(layout->contentsMargins(), normalMargins);
        QCOMPARE(layout->spacing(), normalSpacing);

        const QColor darkBackground = UiTheme::Background;
        UiTheme::apply(*qApp, NightId, CompactLayout);
        QCOMPARE(UiTheme::paletteId(), NightId);
        QCOMPARE(UiTheme::Background, darkBackground);
        const auto compactMargins = layout->contentsMargins();
        const auto compactSpacing = layout->spacing();
        QVERIFY(compactMargins.left() < normalMargins.left());
        QVERIFY(compactSpacing < normalSpacing);

        UiTheme::apply(*qApp, DayId, CompactLayout);
        QCOMPARE(UiTheme::paletteId(), DayId);
        QVERIFY(!UiTheme::isDark());
        QCOMPARE(UiTheme::Background, mistBackground);
        QCOMPARE(qApp->palette().color(QPalette::Link), QColor(DayAccent));
        QCOMPARE(qApp->palette().color(QPalette::Highlight), QColor(DaySelection));
        QCOMPARE(qApp->palette().color(QPalette::HighlightedText), QColor(DaySelectionText));
        QCOMPARE(layout->contentsMargins(), compactMargins);
        QCOMPARE(layout->spacing(), compactSpacing);

        UiTheme::setInstalledThemes({nightTheme()});
        UiTheme::apply(*qApp, DayId, CompactLayout);
        QCOMPARE(UiTheme::paletteId(), MistId);
        QCOMPARE(UiTheme::Accent, mistAccent);
        QCOMPARE(UiTheme::layoutId(), CompactLayout);
        QCOMPARE(layout->contentsMargins(), compactMargins);

        UiTheme::apply(*qApp, QStringLiteral("precision"), StandardLayout);
        QCOMPARE(UiTheme::paletteId(), MistId);
        QCOMPARE(UiTheme::layoutId(), StandardLayout);
        QCOMPARE(layout->contentsMargins(), normalMargins);
        QCOMPARE(layout->spacing(), normalSpacing);
    }

    void updatesExistingButtonsAndListSelection()
    {
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        auto *normal = new QPushButton(QStringLiteral("Browse"), &window);
        auto *primary = new QPushButton(QStringLiteral("Run"), &window);
        UiTheme::setRole(primary, "primary");
        layout->addWidget(normal);
        layout->addWidget(primary);
        auto *list = new QListWidget(&window);
        list->addItems({QStringLiteral("First"), QStringLiteral("Second")});
        list->setProperty("themeForegroundColumn", 0);
        UiTheme::setItemForeground(list->model(), list->model()->index(0, 0), "accent");
        list->setCurrentRow(1, QItemSelectionModel::ClearAndSelect);
        auto *const selected = list->currentItem();
        layout->addWidget(list);
        window.resize(320, 200);
        window.show();

        for (const auto &id : {NightId, DayId}) {
            UiTheme::apply(*qApp, id, StandardLayout);
            QCoreApplication::processEvents();
            const bool dark = id == NightId;
            const QColor normalColor = buttonFill(*normal);
            const QColor primaryColor = buttonFill(*primary);
            QCOMPARE(normalColor, dark ? UiTheme::Surface : QColor(DayControl));
            QCOMPARE(primaryColor, dark ? UiTheme::Accent : UiTheme::Selection);
            QVERIFY(contrast(normalColor, normal->palette().color(QPalette::ButtonText)) >= 4.5);
            QVERIFY(contrast(primaryColor, primary->palette().color(QPalette::ButtonText)) >= 4.5);

            QCOMPARE(list->currentItem(), selected);
            QCOMPARE(list->selectedItems(), QList<QListWidgetItem *>{selected});
            QCOMPARE(list->item(0)->foreground().color(), QColor(dark ? NightAccent : DayAccent));
            const QRect rect = list->visualItemRect(selected);
            QVERIFY(!rect.isEmpty());
            const auto viewport = list->viewport()->grab();
            const QPoint sample = (QPointF(rect.center()) * viewport.devicePixelRatio()).toPoint();
            const auto image = viewport.toImage();
            QVERIFY(image.rect().contains(sample));
            QCOMPARE(image.pixelColor(sample), UiTheme::Selection);
        }
    }

    void retintsRetainedIconsAndCalendarArrows()
    {
        const auto statusIcon = UiTheme::icon(QStringLiteral("project-active"));
        QCOMPARE(iconInk(statusIcon), UiTheme::Accent);
        QCalendarWidget calendar;
        const QDate chosen(2024, 5, 12);
        calendar.setSelectedDate(chosen);
        calendar.show();
        auto *previous = calendar.findChild<QToolButton *>(QStringLiteral("qt_calendar_prevmonth"));
        auto *next = calendar.findChild<QToolButton *>(QStringLiteral("qt_calendar_nextmonth"));
        QVERIFY(previous && next);

        UiTheme::apply(*qApp, NightId, StandardLayout);
        QCOMPARE(iconInk(statusIcon), QColor(NightAccent));
        const QColor nightInk = iconInk(previous->icon());
        QVERIFY(nightInk.isValid());
        QVERIFY(contrast(nightInk, UiTheme::IndexSurface) >= 4.5);
        QCOMPARE(iconInk(next->icon()), nightInk);
        QCOMPARE(calendar.selectedDate(), chosen);
        QCOMPARE(calendar.palette().color(QPalette::HighlightedText), UiTheme::SelectionText);

        UiTheme::apply(*qApp, DayId, StandardLayout);
        QCOMPARE(iconInk(statusIcon), QColor(DayAccent));
        QCOMPARE(iconInk(previous->icon()), QColor(DayIcon));
        QCOMPARE(iconInk(next->icon()), QColor(DayIcon));
        QVERIFY(contrast(iconInk(next->icon()), UiTheme::IndexSurface) >= 4.5);
        QCOMPARE(calendar.selectedDate(), chosen);
        QCOMPARE(calendar.palette().color(QPalette::HighlightedText), QColor(DaySelectionText));
    }

    void recolorsCachedMathRaster()
    {
        QImage raster(3, 1, QImage::Format_ARGB32);
        raster.fill(Qt::transparent);
        raster.setPixelColor(0, 0, Qt::black);
        raster.setPixelColor(1, 0, QColor(0, 0, 0, 128));

        UiTheme::apply(*qApp, NightId, StandardLayout);
        const auto dark = MathText::displayImage(raster, raster.size());
        QCOMPARE(dark.pixelColor(0, 0), UiTheme::Text);
        QCOMPARE(dark.pixelColor(1, 0).alpha(), 128);
        QCOMPARE(dark.pixelColor(2, 0).alpha(), 0);

        UiTheme::apply(*qApp, DayId, StandardLayout);
        const auto light = MathText::displayImage(raster, raster.size());
        QCOMPARE(light.pixelColor(0, 0), UiTheme::Text);
        QVERIFY(light.pixelColor(0, 0) != dark.pixelColor(0, 0));
        QCOMPARE(light.pixelColor(1, 0).alpha(), 128);
        QCOMPARE(light.pixelColor(2, 0).alpha(), 0);
        QCOMPARE(raster.pixelColor(0, 0), QColor(Qt::black));
    }
};

QTEST_MAIN(ThemeRenderingTest)
#include "test_theme_rendering.moc"
