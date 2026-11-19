#include "test_skin_fixture.h"
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
const auto DayId = QStringLiteral("test-day");
const auto DayAccent = QStringLiteral("#195c66");
const auto DaySelection = QStringLiteral("#cce9e5");
const auto DaySelectionText = QStringLiteral("#142f39");
const auto DayControl = QStringLiteral("#f0f6ec");
const auto DayIcon = QStringLiteral("#204462");
const auto StandardLayout = QStringLiteral("standard");

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
        UiTheme::setInstalledThemes({TestSkinFixture::theme(), dayTheme()});
        UiTheme::apply(*qApp, QStringLiteral("mist"), StandardLayout);
    }

    void appliesReadableBaseAndSelectionColors()
    {
        const auto mistBackground = qApp->palette().color(QPalette::Window);
        UiTheme::apply(*qApp, TestSkinFixture::Id, StandardLayout);
        const auto dark = qApp->palette();
        QVERIFY(luminance(dark.color(QPalette::Window)) < luminance(mistBackground));
        QVERIFY(contrast(dark.color(QPalette::Window), dark.color(QPalette::WindowText)) >= 4.5);
        QVERIFY(contrast(dark.color(QPalette::Highlight), dark.color(QPalette::HighlightedText)) >= 4.5);

        UiTheme::apply(*qApp, DayId, StandardLayout);
        const auto light = qApp->palette();
        QCOMPARE(light.color(QPalette::Window), mistBackground);
        QCOMPARE(light.color(QPalette::Link), QColor(DayAccent));
        QCOMPARE(light.color(QPalette::Highlight), QColor(DaySelection));
        QCOMPARE(light.color(QPalette::HighlightedText), QColor(DaySelectionText));
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

        for (const auto &id : {TestSkinFixture::Id, DayId}) {
            UiTheme::apply(*qApp, id, StandardLayout);
            QCoreApplication::processEvents();
            const bool dark = id == TestSkinFixture::Id;
            const QColor normalColor = buttonFill(*normal);
            const QColor primaryColor = buttonFill(*primary);
            QCOMPARE(normalColor, dark ? UiTheme::Surface : QColor(DayControl));
            QCOMPARE(primaryColor, dark ? UiTheme::Accent : UiTheme::Selection);
            QVERIFY(contrast(normalColor, normal->palette().color(QPalette::ButtonText)) >= 4.5);
            QVERIFY(contrast(primaryColor, primary->palette().color(QPalette::ButtonText)) >= 4.5);

            QCOMPARE(list->item(0)->foreground().color(), QColor(dark ? TestSkinFixture::Accent : DayAccent));
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

        for (const auto &id : {TestSkinFixture::Id, DayId}) {
            UiTheme::apply(*qApp, id, StandardLayout);
            QCOMPARE(iconInk(statusIcon), QColor(id == DayId ? DayAccent : TestSkinFixture::Accent));
            const auto ink = iconInk(previous->icon());
            QVERIFY(ink.isValid());
            QCOMPARE(iconInk(next->icon()), ink);
            QVERIFY(contrast(ink, UiTheme::IndexSurface) >= 4.5);
            if (id == DayId) QCOMPARE(ink, QColor(DayIcon));
            QCOMPARE(calendar.selectedDate(), chosen);
            QCOMPARE(calendar.palette().color(QPalette::HighlightedText), UiTheme::SelectionText);
        }
    }

    void recolorsCachedMathRaster()
    {
        QImage raster(3, 1, QImage::Format_ARGB32);
        raster.fill(Qt::transparent);
        raster.setPixelColor(0, 0, Qt::black);
        raster.setPixelColor(1, 0, QColor(0, 0, 0, 128));
        const auto cached = raster.copy();

        QColor previousInk;
        for (const auto &id : {TestSkinFixture::Id, DayId}) {
            UiTheme::apply(*qApp, id, StandardLayout);
            const auto image = MathText::displayImage(raster, raster.size());
            const auto ink = image.pixelColor(0, 0);
            QCOMPARE(ink, UiTheme::Text);
            QVERIFY(ink != previousInk);
            QCOMPARE(image.pixelColor(1, 0).alpha(), 128);
            QCOMPARE(image.pixelColor(2, 0).alpha(), 0);
            previousInk = ink;
        }
        QCOMPARE(raster, cached);
    }
};

QTEST_MAIN(ThemeRenderingTest)
#include "test_theme_rendering.moc"
