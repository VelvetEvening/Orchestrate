#include "mainwindow.h"
#include "test_skin_fixture.h"
#include "version.h"
#include "test_support.h"
#include "ui/contentworkspace.h"
#include "ui/motion.h"
#include "ui/theme.h"
#include "pages/calendarpage.h"
#include "pages/projectspage.h"
#include "pages/clipboardpage.h"
#include "pages/githistorywindow.h"
#include "pages/gitcommitdialog.h"
#include "pages/collaborationrecorddialog.h"
#include "update/updatewidget.h"
#include "widgets/headingtextedit.h"
#include "clipboard/clipboardcontent.h"
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QCalendarWidget>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStyleOptionComboBox>
#include <QStandardPaths>
#include <QTableView>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QTextEdit>
#include <QTabBar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSystemTrayIcon>
#include <QProcess>
#include <QTemporaryDir>

namespace {

const auto SkinLibraryKey = QStringLiteral("skin-library");
const auto SkinRemovalPrompt = QStringLiteral("skinRemoveConfirmation");
constexpr int SkinStatusColumn = 1;
constexpr int SkinRemovalColumn = 2;

using TestSkinFixture::Network;
const auto CatalogUrl = SkinLibrary::defaultCatalogUrl();
const auto CatalogPath = CatalogUrl.path();

QString skinAssetPath(const QString &id, const QString &filename)
{
    return CatalogUrl.resolved(QUrl(QStringLiteral("themes/%1/%2").arg(id, filename))).path();
}

QTreeWidgetItem *skinRow(QTreeWidget *list, const QString &id)
{
    for (int index = 0; index < list->topLevelItemCount(); ++index) {
        const auto *group = list->topLevelItem(index);
        for (int child = 0; child < group->childCount(); ++child) {
            auto *row = group->child(child);
            if (row->data(0, Qt::UserRole).toString() == id) return row;
        }
    }
    return nullptr;
}

QPoint skinCellPoint(QTreeWidget *list, QTreeWidgetItem *row, int column = SkinStatusColumn)
{
    return list->visualRect(list->indexFromItem(row, column)).center();
}

QString skinState(QTreeWidget *list, const QString &id)
{
    const auto *row = skinRow(list, id);
    return row ? row->data(SkinStatusColumn, Qt::AccessibleTextRole).toString() : QString();
}

bool hasSidebarArtwork(QWidget &sidebar)
{
    const QPixmap pixmap = sidebar.grab();
    const QImage image = pixmap.toImage();
    for (int i = 0; i < sidebar.layout()->count(); ++i) {
        const auto *spacer = sidebar.layout()->itemAt(i)->spacerItem();
        if (!spacer) continue;
        const QRect gap = spacer->geometry();
        const qreal scale = pixmap.devicePixelRatio();
        const int x = qRound(gap.center().x() * scale);
        for (int y = qRound(gap.top() * scale); y < qRound(gap.bottom() * scale); ++y) {
            if (image.pixelColor(x, y) != QColor(UiTheme::Sidebar)) return true;
        }
    }
    return false;
}

bool hasNavigationMarker(QWidget &sidebar, const QPushButton &button)
{
    const QRect marker(5, button.geometry().center().y() - 2, 3, 5);
    const QImage sample = sidebar.grab(marker).toImage();
    return sample.pixelColor(sample.width() / 2, sample.height() / 2) == UiTheme::Accent;
}

} // namespace

static QString navigationRepository() {
    static QTemporaryDir fixture;
    static bool initialized = false;
    if (!fixture.isValid()) qFatal("Could not create navigation Git fixture");
    if (!initialized) {
        const auto git = [&](const QStringList &arguments) {
            QProcess process;
            process.start("git", QStringList{"-C", fixture.path(), "-c", "commit.gpgsign=false"} + arguments);
            if (!process.waitForStarted() || !process.waitForFinished(15000)
                || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
                qFatal("Navigation Git fixture failed: %s", process.readAllStandardError().constData());
        };
        git({"init", "-b", "main"});
        git({"config", "user.name", "Navigation Test"});
        git({"config", "user.email", "navigation@example.invalid"});
        for (int i = 0; i < 3; ++i) {
            QFile file(fixture.filePath("record.txt"));
            if (!file.open(QIODevice::WriteOnly)) qFatal("Could not write navigation Git fixture");
            file.write("Navigation revision " + QByteArray::number(i) + '\n');
            file.close();
            git({"add", "record.txt"});
            git({"commit", "-m", QString("Navigation revision %1").arg(i)});
        }
        initialized = true;
    }
    return fixture.path();
}

static QPushButton *button(QWidget *root,const QString &text) {
    for (auto *b:root->findChildren<QPushButton*>()) if (b->text()==text && b->isVisible()) return b;
    return nullptr;
}
static void seed(int diaryCount = 5) {
    AppDatabase db; QString error; if (!db.open(&error)) qFatal("%s",qPrintable(error));
    db.setSetting("global_shortcut_enabled","false"); db.setSetting("clipboard.monitor","false");
    QFile cache(qApp->applicationDirPath()+"/data/update-check.json");
    if (cache.open(QIODevice::WriteOnly)) cache.write("{\"automatic_enabled\":false}");
    if (!db.projects().isEmpty()) return;
    AppDatabase::Project project; project.name=QStringLiteral("Orchestrate 导航验收（示例）");
    project.directory=navigationRepository(); int id=0;
    if (!db.addProject(project,&id,&error)) qFatal("%s",qPrintable(error));
    AppDatabase::WorkRecord record; record.projectId=id; record.title=QStringLiteral("v3.4.0 内容导航验收");
    record.summary=QStringLiteral("# 验收要点\n\n返回时保留筛选与阅读位置。切换模块后草稿保持。\n\n独立打开后，可以将同一份内容收回主窗口。");
    record.content=QStringLiteral("# 合成验收记录\n\n这份记录仅用于独立测试目录，不属于日用数据。\n\n## 操作顺序\n\n打开、编辑、返回、切换模块，再回到同一份草稿。\n\n## 公式\n\n\\(a^2+b^2=c^2\\)");
    db.saveWorkRecord(&record,&error);
    for (int i=0;i<diaryCount;++i) db.addDiaryEntry(QDate::currentDate().addDays(-i),QStringLiteral("导航体验记录 %1（示例）").arg(i+1),record.content,&error);
    AppDatabase::ClipboardItem item; item.title=QStringLiteral("验收文本（示例）");
    ClipboardContent::plainText(QStringLiteral("返回不会丢失草稿。"),&item,&error); db.saveClipboardItem(&item,&error);
}
static void prepareWindow(MainWindow &window) {
    window.findChild<QAbstractButton *>("closeToTrayEnabled")->setChecked(false); window.show();
    QTest::qWait(40);
}
using TestSupport::capture;
static bool answerLeave(QMessageBox::StandardButton choice, const std::function<void()> &action,
                        const QString &expectedPrompt = {}) {
    bool prompted = false;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box) return;
        auto *answer = box->button(choice);
        prompted = expectedPrompt.isEmpty() || (box->objectName() == expectedPrompt && answer);
        timer.stop();
        if (answer) answer->click(); else box->reject();
    });
    timer.start(10); action(); return prompted;
}
static void saveEditor(QDialog *dialog) {
    dialog->window()->activateWindow(); QTest::qWait(20);
    auto *focus=dialog->focusWidget(); if (!focus || !focus->isVisible()) focus=dialog;
    focus->setFocus(); QTest::keyClick(focus,Qt::Key_S,Qt::ControlModifier);
}
class WorkspaceNavigationTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        const auto path=qApp->applicationDirPath()+"/data";
        QVERIFY(QFileInfo(qApp->applicationDirPath()).fileName()=="workspace-navigation-test");
        QVERIFY(!QFileInfo::exists(path) || QDir(path).removeRecursively()); seed();
    }

    void appearanceControlsAlignAndPopupAppliesOnlyConfirmedChoice() {
        MainWindow window; prepareWindow(window);
        window.resize(window.minimumSize());
        const auto navigation = window.findChildren<QPushButton *>("navButton");
        auto *palette = window.findChild<QComboBox *>("appearancePalette");
        auto *layout = window.findChild<QComboBox *>("appearanceLayout");
        auto *motion = window.findChild<QComboBox *>("appearanceMotion");
        auto *manage = window.findChild<QPushButton *>("manageSkinsButton");
        auto *scroll = window.findChild<QScrollArea *>("settingsScroll");
        auto *sidebar = window.findChild<QWidget *>("sidebar");
        auto *sidebarToggle = window.findChild<QPushButton *>("sidebarToggle");
        auto *calendar = window.findChild<QCalendarWidget *>();
        QVERIFY(palette && layout && motion && manage && scroll && sidebar && sidebarToggle && calendar);
        auto *grid = calendar->findChild<QTableView *>();
        QVERIFY(grid);
        const int standardDateHeight = grid->rowHeight(grid->currentIndex().row());
        navigation.at(4)->click();
        window.activateWindow();
        QTRY_VERIFY(window.isActiveWindow());
        QCOMPARE(palette->accessibleName(), QStringLiteral("风格"));
        QCOMPARE(palette->count(), 2);
        QCOMPARE(palette->currentData().toString(), QStringLiteral("mist"));
        QCOMPARE(layout->currentData().toString(), QStringLiteral("standard"));
        QCOMPARE(motion->accessibleName(), QStringLiteral("界面动效"));
        QCOMPARE(motion->currentText(), QStringLiteral("跟随系统"));

        manage->setFocus();
        QTest::keyClick(manage, Qt::Key_Tab);
        QCOMPARE(QApplication::focusWidget(), palette);
        QTest::keyClick(palette, Qt::Key_Tab);
        QCOMPARE(QApplication::focusWidget(), layout);
        QTest::keyClick(layout, Qt::Key_Tab);
        QCOMPARE(QApplication::focusWidget(), motion);
        capture(window, "appearance-mist-standard");

        QTest::keyClick(palette, Qt::Key_Down, Qt::AltModifier);
        auto *options = palette->view();
        QTRY_VERIFY(options->isVisible());
        const QRect field(palette->mapToGlobal(QPoint()), palette->size());
        const QRect popup(options->mapToGlobal(QPoint()), options->size());
        QVERIFY(!field.intersects(popup));
        for (int row = 0; row < palette->count(); ++row)
            QVERIFY(options->viewport()->rect().contains(options->visualRect(palette->model()->index(row, 0))));
        QTest::keyClick(options, Qt::Key_End);
        QCOMPARE(options->currentIndex().row(), 1);
        QCOMPARE(palette->currentIndex(), 0);
        QTest::keyClick(options, Qt::Key_Escape);
        QCOMPARE(palette->currentIndex(), 0);
        QTest::keyClick(palette, Qt::Key_Down, Qt::AltModifier);
        QTRY_VERIFY(options->isVisible());
        QTest::keyClick(options, Qt::Key_End);
        QTest::keyClick(options, Qt::Key_Return);
        QTRY_VERIFY(!options->isVisible());
        QCOMPARE(UiTheme::paletteId(), QStringLiteral("paper"));
        QCOMPARE(UiTheme::layoutId(), QStringLiteral("standard"));
        QTest::keyClick(layout, Qt::Key_End);
        QCOMPARE(UiTheme::layoutId(), QStringLiteral("compact"));
        QCOMPARE(UiTheme::paletteId(), QStringLiteral("paper"));
        QVERIFY(!qApp->styleSheet().contains(QLatin1Char('@')));
        QTRY_VERIFY(hasNavigationMarker(*sidebar, *navigation.last()));
        const auto *settingsButton = navigation.last();
        const QImage gear = settingsButton->icon().pixmap(settingsButton->iconSize(), QIcon::Normal, QIcon::On).toImage();
        QVERIFY(!gear.isNull());
        QCOMPARE(gear.pixelColor(gear.rect().center()).alpha(), 0);
        bool hasAccent = false;
        for (int y = 0; y < gear.height(); ++y)
            for (int x = 0; x < gear.width(); ++x)
                hasAccent |= gear.pixelColor(x, y) == UiTheme::Accent;
        QVERIFY(hasAccent);
        capture(window, "appearance-paper-compact-minimum");

        const auto pageRect = [scroll](QWidget *control) {
            return QRect(control->mapTo(scroll->widget(), QPoint()), control->size());
        };
        const QRect paletteRect = pageRect(palette);
        const QRect layoutRect = pageRect(layout);
        const QRect motionRect = pageRect(motion);
        const QRect manageRect = pageRect(manage);
        QVERIFY(paletteRect.bottom() < layoutRect.top());
        QVERIFY(layoutRect.bottom() < motionRect.top());
        QCOMPARE(paletteRect.left(), layoutRect.left());
        QCOMPARE(paletteRect.right(), layoutRect.right());
        QCOMPARE(motionRect.left(), layoutRect.left());
        QCOMPARE(motionRect.right(), layoutRect.right());
        auto *preview = window.findChild<QWidget *>("appearancePreview");
        QVERIFY(preview && manageRect.bottom() < pageRect(preview).top());
        QVERIFY(pageRect(preview).right() < paletteRect.left());
        QVERIFY(pageRect(preview).top() <= paletteRect.top());
        QVERIFY(layoutRect.bottom() <= pageRect(preview).bottom());
        QCOMPARE(manageRect.right(), paletteRect.right());
        for (auto *choice : {palette, layout, motion}) {
            QVERIFY(scroll->viewport()->rect().contains(QRect(choice->mapTo(scroll->viewport(), QPoint()), choice->size())));
            QStyleOptionComboBox option;
            option.initFrom(choice);
            const QRect textRect = choice->style()->subControlRect(
                QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, choice);
            QVERIFY(textRect.width() >= choice->iconSize().width() + 4
                                         + choice->fontMetrics().horizontalAdvance(choice->currentText()));
        }
        for (const auto *name : {"autoStartEnabled", "closeToTrayEnabled", "globalShortcutEnabled", "automaticUpdatesEnabled"}) {
            auto *toggle = window.findChild<QAbstractButton *>(QLatin1String(name));
            QVERIFY(toggle && toggle->isCheckable() && !toggle->accessibleName().isEmpty());
            QCOMPARE(toggle->text(), toggle->isChecked() ? QStringLiteral("开启") : QStringLiteral("关闭"));
            QCOMPARE(pageRect(toggle).right(), paletteRect.right());
        }

        auto *appearance = window.findChild<QFrame *>("settingsAppearanceSection");
        auto *runtime = window.findChild<QFrame *>("settingsRuntimeSection");
        auto *updates = window.findChild<QWidget *>("updateWidget");
        QVERIFY(appearance && runtime && updates);
        QVERIFY(pageRect(runtime).top() > pageRect(appearance).bottom());
        QVERIFY(pageRect(updates).top() > pageRect(runtime).bottom());
        window.activateWindow();
        QTRY_VERIFY(window.isActiveWindow());
        layout->setFocus();
        QTRY_VERIFY(layout->hasFocus());
        const int minimumWindowSectionWidth = appearance->width();
        window.resize(1180, 760);
        QTRY_VERIFY(appearance->width() > minimumWindowSectionWidth);
        QCOMPARE(pageRect(runtime).left(), pageRect(appearance).left());
        QCOMPARE(pageRect(updates).right(), pageRect(appearance).right());
        QVERIFY(pageRect(runtime).top() > pageRect(appearance).bottom());
        QVERIFY(pageRect(updates).top() > pageRect(runtime).bottom());
        QVERIFY(pageRect(preview).right() < pageRect(palette).left());
        QCOMPARE(pageRect(manage).right(), pageRect(layout).right());
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QCOMPARE(QApplication::focusWidget(), layout);
        capture(window, "settings-wide");
        window.resize(window.minimumSize());
        QTRY_VERIFY(pageRect(runtime).top() > pageRect(appearance).bottom());
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);

        const auto expandedIcon = sidebarToggle->icon().pixmap(24, 24).toImage();
        const int expandedHeight = navigation.at(4)->height();
        sidebarToggle->click();
        QCoreApplication::processEvents();
        QCOMPARE(navigation.at(4)->height(), expandedHeight);
        QVERIFY(expandedIcon != sidebarToggle->icon().pixmap(24, 24).toImage());
        navigation.first()->click();
        QCoreApplication::processEvents();
        const int compactDateHeight = grid->rowHeight(grid->currentIndex().row());
        QVERIFY(compactDateHeight >= 36 && compactDateHeight < standardDateHeight);
        QCOMPARE(grid->indexAt(grid->visualRect(grid->currentIndex()).bottomRight()), grid->currentIndex());
    }

    void migratedAppearanceAndSidebarPreferencesRestore() {
        AppDatabase db;
        QVERIFY(db.open());
        QVERIFY(db.setSetting("appearance_preset", "precision"));
        {
            MainWindow window;
            QCOMPARE(UiTheme::paletteId(), QStringLiteral("mist"));
            QCOMPARE(UiTheme::layoutId(), QStringLiteral("compact"));
            auto *palette = window.findChild<QComboBox *>("appearancePalette");
            auto *layout = window.findChild<QComboBox *>("appearanceLayout");
            palette->setCurrentIndex(palette->findData("paper"));
            layout->setCurrentIndex(layout->findData("standard"));
            window.findChild<QPushButton *>("sidebarToggle")->click();
        }
        MainWindow restored;
        QVERIFY(restored.ready());
        QCOMPARE(UiTheme::paletteId(), QStringLiteral("paper"));
        QCOMPARE(UiTheme::layoutId(), QStringLiteral("standard"));
        QCOMPARE(restored.findChild<QWidget *>("sidebar")->width(), 72);
        auto *toggle = restored.findChild<QPushButton *>("sidebarToggle");
        QCOMPARE(toggle->toolTip(), QStringLiteral("展开侧边栏"));
        QCOMPARE(toggle->accessibleName(), toggle->toolTip());
    }

    void legacyAppearanceDefaults_data() {
        QTest::addColumn<QString>("legacy");
        QTest::addColumn<QString>("savedPalette");
        QTest::addColumn<QString>("savedLayout");
        QTest::addColumn<QString>("expectedPalette");
        QTest::addColumn<QString>("expectedLayout");
        QTest::newRow("paper") << QStringLiteral("paper") << QString() << QString()
                               << QStringLiteral("paper") << QStringLiteral("standard");
        QTest::newRow("unknown") << QStringLiteral("retired-preset") << QString() << QString()
                                 << QStringLiteral("mist") << QStringLiteral("standard");
        QTest::newRow("explicit-preferences") << QStringLiteral("precision") << QStringLiteral("paper")
                                             << QStringLiteral("standard") << QStringLiteral("paper") << QStringLiteral("standard");
        QTest::newRow("missing-installed-palette") << QStringLiteral("paper") << TestSkinFixture::Id << QStringLiteral("compact")
                                                    << QStringLiteral("mist") << QStringLiteral("compact");
    }

    void legacyAppearanceDefaults() {
        QFETCH(QString, legacy);
        QFETCH(QString, savedPalette);
        QFETCH(QString, savedLayout);
        QFETCH(QString, expectedPalette);
        QFETCH(QString, expectedLayout);
        AppDatabase db;
        QVERIFY(db.open());
        QVERIFY(db.setSetting("appearance_preset", legacy));
        if (!savedPalette.isNull()) QVERIFY(db.setSetting("appearance_palette", savedPalette));
        if (!savedLayout.isNull()) QVERIFY(db.setSetting("appearance_layout", savedLayout));
        MainWindow window;
        QCOMPARE(UiTheme::paletteId(), expectedPalette);
        QCOMPARE(UiTheme::layoutId(), expectedLayout);
        if ((savedPalette.isNull() ? legacy : savedPalette) != expectedPalette)
            QCOMPARE(db.setting("appearance_palette"), expectedPalette);
    }
    void appearanceSaveFailureRestoresSelection() {
        AppDatabase db;
        QVERIFY(db.open());
        MainWindow window;
        auto *palette = window.findChild<QComboBox *>("appearancePalette");
        auto *layout = window.findChild<QComboBox *>("appearanceLayout");
        QVERIFY(palette && layout);
        palette->setCurrentIndex(palette->findData("paper"));
        const QString connection = QStringLiteral("appearance-save-failure");
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE", connection);
            sql.setDatabaseName(db.databasePath());
            QVERIFY(sql.open());
            QSqlQuery query(sql);
            QVERIFY(query.exec("CREATE TRIGGER reject_layout BEFORE INSERT ON app_settings "
                               "WHEN NEW.key = 'appearance_layout' BEGIN SELECT RAISE(ABORT, 'test failure'); END"));
            QVERIFY(answerLeave(QMessageBox::Ok, [&] { layout->setCurrentIndex(layout->findData("compact")); }));
            QCOMPARE(layout->currentData().toString(), QStringLiteral("standard"));
            QCOMPARE(UiTheme::layoutId(), QStringLiteral("standard"));
            QCOMPARE(palette->currentData().toString(), QStringLiteral("paper"));
            QCOMPARE(db.setting("appearance_palette"), QStringLiteral("paper"));
            QVERIFY(query.exec("DROP TRIGGER reject_layout"));
        }
        QSqlDatabase::removeDatabase(connection);
        layout->setCurrentIndex(layout->findData("compact"));
        QCOMPARE(db.setting("appearance_layout"), QStringLiteral("compact"));
    }

    void motionPreferencePersistsAndRecoversFromWriteFailure() {
        const auto key = QStringLiteral("appearance_motion");
        AppDatabase db;
        QVERIFY(db.open());
        {
            MainWindow window;
            QVERIFY(window.ready());
            prepareWindow(window);
            window.findChildren<QPushButton *>("navButton").last()->click();
            auto *motion = window.findChild<QComboBox *>("appearanceMotion");
            QVERIFY(motion && motion->isVisible());
            QCOMPARE(UiMotion::instance().preference(), UiMotion::Preference::FollowSystem);
            if (QGuiApplication::platformName() == QStringLiteral("offscreen")) QVERIFY(!UiMotion::enabled());
            QTest::keyClick(motion, Qt::Key_End);
            QCOMPARE(motion->currentText(), QStringLiteral("关闭"));
            QCOMPARE(db.setting(key), QStringLiteral("off"));
            QCOMPARE(UiMotion::instance().preference(), UiMotion::Preference::Off);
        }

        UiMotion::instance().setPreference(UiMotion::Preference::FollowSystem);
        MainWindow restored;
        QVERIFY(restored.ready());
        prepareWindow(restored);
        restored.findChildren<QPushButton *>("navButton").last()->click();
        auto *motion = restored.findChild<QComboBox *>("appearanceMotion");
        auto *status = restored.findChild<QLabel *>("appearanceMotionStatus");
        QVERIFY(motion && motion->isVisible() && status && !status->isVisible());
        QCOMPARE(motion->currentData().toString(), QStringLiteral("off"));
        QCOMPARE(UiMotion::instance().preference(), UiMotion::Preference::Off);

        const auto connection = QStringLiteral("blocked-motion-settings");
        const auto cleanup = qScopeGuard([&] { QSqlDatabase::removeDatabase(connection); });
        auto sql = QSqlDatabase::addDatabase("QSQLITE", connection);
        sql.setDatabaseName(db.databasePath());
        QVERIFY(sql.open());
        QSqlQuery query(sql);
        QVERIFY(query.exec("CREATE TRIGGER reject_motion BEFORE INSERT ON app_settings "
                           "WHEN NEW.key = 'appearance_motion' BEGIN SELECT RAISE(ABORT, 'blocked motion'); END"));
        const int followSystem = motion->findData(QStringLiteral("system"));
        QVERIFY(followSystem >= 0);
        motion->setCurrentIndex(followSystem);
        QCOMPARE(motion->currentData().toString(), QStringLiteral("off"));
        QCOMPARE(db.setting(key), QStringLiteral("off"));
        QCOMPARE(UiMotion::instance().preference(), UiMotion::Preference::Off);
        QVERIFY(status->isVisible() && status->text().contains(QStringLiteral("无法保存界面动效设置")));

        QVERIFY(query.exec("DROP TRIGGER reject_motion"));
        motion->setCurrentIndex(followSystem);
        QCOMPARE(db.setting(key), QStringLiteral("system"));
        QCOMPARE(UiMotion::instance().preference(), UiMotion::Preference::FollowSystem);
        QVERIFY(!status->isVisible());
    }

    void skinLibraryKeepsItsWorkspaceAndDownloadsTheActivatedRow() {
        Network network;
        const QString targetId = QStringLiteral("sample-23");
        const QString targetName = QStringLiteral("配色样本 23");
        QJsonArray entries{TestSkinFixture::catalogEntry()};
        for (int number = 1; number <= 32; ++number) {
            const auto suffix = QStringLiteral("%1").arg(number, 2, 10, QLatin1Char('0'));
            entries.append(TestSkinFixture::catalogEntry(QStringLiteral("sample-") + suffix,
                                                         QStringLiteral("配色样本 ") + suffix));
        }
        network.responses[CatalogPath] = {TestSkinFixture::catalog(entries)};
        const auto targetManifest = skinAssetPath(targetId, "theme.json");
        network.responses[targetManifest] = {TestSkinFixture::json(TestSkinFixture::manifest(false, targetId, targetName))};

        MainWindow window(nullptr, nullptr, &network);
        prepareWindow(window);
        window.resize(window.minimumSize());
        const auto nav = window.findChildren<QPushButton *>("navButton");
        auto *settings = window.findChild<QWidget *>("settingsPage");
        auto *workspace = window.findChild<ContentWorkspace *>("settingsWorkspace");
        auto *manage = window.findChild<QPushButton *>("manageSkinsButton");
        QVERIFY(settings && workspace && manage);
        QCOMPARE(manage->text(), QStringLiteral("主题库"));
        nav.at(4)->click();
        QVERIFY(network.requests.isEmpty());
        manage->setFocus();
        QTest::keyClick(manage, Qt::Key_Space);
        QTRY_COMPARE(workspace->currentKey(), SkinLibraryKey);
        auto *page = workspace->content(SkinLibraryKey);
        QVERIFY(page && page->isVisible() && !page->isWindow() && !page->isModal());
        QCOMPARE(page->windowTitle(), QStringLiteral("主题库"));
        auto *list = page->findChild<QTreeWidget *>("skinManagerList");
        auto *search = page->findChild<QLineEdit *>("skinSearch");
        auto *refresh = page->findChild<QPushButton *>("skinCatalogRefresh");
        auto *back = page->parentWidget()->findChild<QPushButton *>("contentBack");
        auto *detach = page->parentWidget()->findChild<QPushButton *>("contentDetach");
        QVERIFY(list && search && refresh && back && detach);
        QVERIFY(refresh->text().isEmpty() && !refresh->icon().isNull());
        QCOMPARE(list->topLevelItemCount(), 2);
        auto *builtIn = list->topLevelItem(0);
        auto *extensions = list->topLevelItem(1);
        QCOMPARE(builtIn->text(0), QStringLiteral("内置主题"));
        QCOMPARE(builtIn->childCount(), 2);
        QCOMPARE(extensions->text(0), QStringLiteral("扩展主题"));
        QTRY_COMPARE(extensions->childCount(), entries.size());
        QCOMPARE(skinState(list, QStringLiteral("mist")), QStringLiteral("使用中"));
        auto *target = skinRow(list, targetId);
        QVERIFY(target);
        capture(window, "skin-library-many-minimum");

        list->setCurrentItem(target);
        list->scrollToItem(target, QAbstractItemView::PositionAtCenter);
        const int savedScroll = list->verticalScrollBar()->value();
        QVERIFY(savedScroll > 0);
        back->click();
        QTRY_VERIFY(settings->isVisible() && manage->hasFocus());
        manage->click();
        QCOMPARE(workspace->content(SkinLibraryKey), page);
        QCOMPARE(list->currentItem(), target);
        QCOMPARE(list->verticalScrollBar()->value(), savedScroll);
        nav.at(1)->click();
        QVERIFY(!page->isVisible());
        nav.at(4)->click();
        QVERIFY(page->isVisible());

        detach->click();
        QPointer<QDialog> host = window.findChild<QDialog *>("contentWindow");
        QVERIFY(host && host->isVisible() && host->isAncestorOf(page) && settings->isVisible());
        manage->click();
        QCOMPARE(window.findChildren<QDialog *>("skinManagerDialog").size(), 1);
        QCOMPARE(window.findChildren<QDialog *>("contentWindow").size(), 1);
        detach->click();
        QTRY_VERIFY(host.isNull());
        QCOMPARE(workspace->currentKey(), SkinLibraryKey);
        QCOMPARE(workspace->content(SkinLibraryKey), page);
        QCOMPARE(network.requests, QList<QUrl>{CatalogUrl});

        const auto visibleRows = [list] {
            int count = 0;
            for (int i = 0; i < list->topLevelItemCount(); ++i) {
                const auto *group = list->topLevelItem(i);
                if (group->isHidden()) continue;
                for (int j = 0; j < group->childCount(); ++j)
                    if (!group->child(j)->isHidden()) ++count;
            }
            return count;
        };
        QTest::keyClicks(search, "missing-skin");
        QCOMPARE(visibleRows(), 0);
        auto *empty = page->findChild<QLabel *>("emptyViewHint");
        QVERIFY(empty && empty->isVisible());
        QCOMPARE(empty->text(), QStringLiteral("没有匹配的主题"));
        search->selectAll();
        QTest::keyClicks(search, " 2 ");
        QVERIFY(visibleRows() > 1 && visibleRows() < entries.size());
        QVERIFY(!target->isHidden());
        auto *another = skinRow(list, QStringLiteral("sample-21"));
        QVERIFY(another && !another->isHidden());
        list->setCurrentItem(another);
        list->scrollToItem(target, QAbstractItemView::PositionAtCenter);
        const QPoint point = skinCellPoint(list, target);
        QVERIFY(list->viewport()->rect().contains(point));
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTRY_COMPARE(skinState(list, targetId), QStringLiteral("已安装，可启用"));
        QCOMPARE(skinState(list, QStringLiteral("sample-21")), QStringLiteral("可下载"));
        QCOMPARE(network.requests.size(), 2);
        QCOMPARE(network.requests.last().path(), targetManifest);
        search->clear();
        QCOMPARE(visibleRows(), entries.size() + 2);
        refresh->click();
        QTRY_VERIFY(refresh->isEnabled());
        QCOMPARE(network.requests.size(), 3);
        QCOMPARE(network.requests.last(), CatalogUrl);

        auto *update = window.findChild<UpdateWidget *>();
        auto *updateAction = window.findChild<QAction *>("viewUpdateAction");
        auto *scroll = window.findChild<QScrollArea *>("settingsScroll");
        QVERIFY(update && updateAction && scroll);
        auto *heading = update->findChild<QLabel *>("cardTitle");
        QVERIFY(heading);
        update->updateAvailableChanged(QStringLiteral("v99.0.0"));
        window.hide();
        updateAction->trigger();
        QTRY_VERIFY(window.isVisible() && settings->isVisible());
        QTRY_VERIFY(scroll->viewport()->rect().contains(QRect(heading->mapTo(scroll->viewport(), QPoint()), heading->size())));
        QCOMPARE(workspace->content(SkinLibraryKey), page);
    }

    void skinManagementDownloadsAndRemovesActiveSkinOffline() {
        AppDatabase db;
        QVERIFY(db.open());
        const QString id = TestSkinFixture::Id;
        const QDir skinDir(QFileInfo(db.databasePath()).dir().filePath(QStringLiteral("skins/") + id));
        const auto manifestPath = skinDir.filePath("theme.json");
        const auto imagePath = skinDir.filePath("sidebar.png");
        const auto imageAsset = skinAssetPath(id, "sidebar.png");
        Network network;
        network.responses[CatalogPath] = {TestSkinFixture::catalog()};
        network.responses[skinAssetPath(id, "theme.json")] = {TestSkinFixture::json(TestSkinFixture::manifest(true))};
        network.responses[imageAsset] = {{}, QNetworkReply::ConnectionRefusedError};
        {
            MainWindow window(nullptr, nullptr, &network);
            prepareWindow(window);
            window.findChildren<QPushButton *>("navButton").at(4)->click();
            auto *palette = window.findChild<QComboBox *>("appearancePalette");
            auto *layout = window.findChild<QComboBox *>("appearanceLayout");
            auto *manage = window.findChild<QPushButton *>("manageSkinsButton");
            auto *workspace = window.findChild<ContentWorkspace *>("settingsWorkspace");
            QVERIFY(palette && layout && manage && workspace);
            palette->setCurrentIndex(palette->findData("paper"));
            layout->setCurrentIndex(layout->findData("compact"));
            manage->click();
            auto *page = workspace->content(SkinLibraryKey);
            QVERIFY(page);
            auto *list = page->findChild<QTreeWidget *>("skinManagerList");
            auto *back = page->parentWidget()->findChild<QPushButton *>("contentBack");
            QVERIFY(list && back);
            QTRY_COMPARE(skinState(list, id), QStringLiteral("可下载"));

            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, skinCellPoint(list, skinRow(list, id)));
            QTRY_VERIFY(skinState(list, id).contains(QStringLiteral("重试")));
            QVERIFY(skinRow(list, id)->toolTip(SkinStatusColumn).contains(QStringLiteral("模拟网络连接失败")));
            QVERIFY(!QFileInfo::exists(manifestPath));
            network.responses[imageAsset] = {TestSkinFixture::sidebarImage()};
            network.delay = 60;
            list->setCurrentItem(skinRow(list, id));
            QTest::keyClick(list, Qt::Key_Space);
            QVERIFY(skinState(list, id).contains(QStringLiteral("正在下载")));
            back->click();
            QTRY_VERIFY(QFileInfo::exists(imagePath));
            QCOMPARE(UiTheme::paletteId(), QStringLiteral("paper"));
            manage->click();
            QCOMPARE(workspace->content(SkinLibraryKey), page);
            auto *installed = skinRow(list, id);
            QVERIFY(installed);
            QCOMPARE(installed->text(SkinStatusColumn), QStringLiteral("启用"));
            list->setCurrentItem(installed);
            QTest::keyClick(list, Qt::Key_Return);
            QCOMPARE(skinState(list, id), QStringLiteral("使用中"));
            QCOMPARE(UiTheme::paletteId(), id);
            QCOMPARE(palette->currentData().toString(), id);
            QCOMPARE(db.setting("appearance_palette"), id);
            const auto checkIcon = UiTheme::icon(QStringLiteral("check")).pixmap(24, 24);
            QVERIFY(!checkIcon.isNull());
            QCOMPARE(installed->icon(SkinStatusColumn).pixmap(24, 24).toImage(), checkIcon.toImage());
            QVERIFY(!answerLeave(QMessageBox::Cancel, [&] {
                QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, skinCellPoint(list, installed));
                QTest::keyClick(list, Qt::Key_Return);
            }));
            QCOMPARE(network.requests.size(), 5);
            back->click();
            QCOMPARE(qApp->palette().color(QPalette::Link), QColor(TestSkinFixture::Accent));
            auto *sidebar = window.findChild<QWidget *>("sidebar");
            auto *sidebarToggle = window.findChild<QPushButton *>("sidebarToggle");
            QVERIFY(sidebar && sidebarToggle);
            QCOMPARE(sidebar->width(), 168);
            QVERIFY(hasSidebarArtwork(*sidebar));
            capture(window, "skin-settings-installed");
            sidebarToggle->click();
            QTRY_COMPARE(sidebar->width(), 72);
            QTRY_VERIFY(!hasSidebarArtwork(*sidebar));
            sidebarToggle->click();
            QTRY_COMPARE(sidebar->width(), 168);
            QTRY_VERIFY(hasSidebarArtwork(*sidebar));
        }

        network.requests.clear();
        network.delay = 250;
        network.responses[CatalogPath] = {{}, QNetworkReply::ConnectionRefusedError, 0};
        MainWindow restored(nullptr, nullptr, &network);
        prepareWindow(restored);
        restored.findChildren<QPushButton *>("navButton").at(4)->click();
        auto *palette = restored.findChild<QComboBox *>("appearancePalette");
        auto *workspace = restored.findChild<ContentWorkspace *>("settingsWorkspace");
        QVERIFY(palette && workspace && network.requests.isEmpty());
        QCOMPARE(palette->currentData().toString(), id);
        restored.findChild<QPushButton *>("manageSkinsButton")->click();
        auto *page = workspace->content(SkinLibraryKey);
        QVERIFY(page);
        auto *list = page->findChild<QTreeWidget *>("skinManagerList");
        auto *status = page->findChild<QLabel *>("skinManagerStatus");
        auto *refresh = page->findChild<QPushButton *>("skinCatalogRefresh");
        QVERIFY(list && status && refresh);
        QTRY_COMPARE(refresh->toolTip(), QStringLiteral("重试目录"));
        QCOMPARE(skinState(list, id), QStringLiteral("使用中"));
        capture(restored, "skin-manager-offline");
        auto *installed = skinRow(list, id);
        QVERIFY(installed);
        QVERIFY(answerLeave(QMessageBox::Cancel, [&] {
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                              skinCellPoint(list, installed, SkinRemovalColumn));
        }, SkinRemovalPrompt));
        const auto requestRemoval = [&] {
            list->setCurrentItem(skinRow(list, id));
            QTest::keyClick(list, Qt::Key_Delete);
        };

        const QString connection = QStringLiteral("skin-removal-save-failure");
        const auto cleanup = qScopeGuard([&] { QSqlDatabase::removeDatabase(connection); });
        auto sql = QSqlDatabase::addDatabase("QSQLITE", connection);
        sql.setDatabaseName(db.databasePath());
        QVERIFY(sql.open());
        QSqlQuery query(sql);
        auto *paper = skinRow(list, QStringLiteral("paper"));
        QVERIFY(paper && !paper->icon(0).isNull() && paper->icon(SkinRemovalColumn).isNull());
        auto *mist = skinRow(list, QStringLiteral("mist"));
        QVERIFY(mist && !mist->icon(0).isNull() && mist->icon(SkinRemovalColumn).isNull());
        QVERIFY(query.exec("CREATE TRIGGER reject_theme_activation BEFORE INSERT ON app_settings "
                           "WHEN NEW.key = 'appearance_palette' AND NEW.value = 'paper' "
                           "BEGIN SELECT RAISE(ABORT, 'test failure'); END"));
        const auto enablePaper = [&] {
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, skinCellPoint(list, paper));
        };
        enablePaper();
        QVERIFY(status->text().contains(QStringLiteral("无法启用主题")));
        QCOMPARE(db.setting("appearance_palette"), id);
        QCOMPARE(palette->currentData().toString(), id);
        QCOMPARE(skinState(list, id), QStringLiteral("使用中"));
        QVERIFY(query.exec("DROP TRIGGER reject_theme_activation"));

        enablePaper();
        QCOMPARE(db.setting("appearance_palette"), QStringLiteral("paper"));
        QCOMPARE(palette->currentData().toString(), QStringLiteral("paper"));
        QCOMPARE(skinState(list, QStringLiteral("paper")), QStringLiteral("使用中"));
        QCOMPARE(UiTheme::layoutId(), QStringLiteral("compact"));
        QVERIFY(status->isHidden());
        list->setCurrentItem(installed);
        QTest::keyClick(list, Qt::Key_Return);
        QCOMPARE(skinState(list, id), QStringLiteral("使用中"));
        QCOMPARE(network.requests.size(), 1);

        QVERIFY(query.exec("CREATE TRIGGER reject_skin_fallback BEFORE INSERT ON app_settings "
                           "WHEN NEW.key = 'appearance_palette' AND NEW.value = 'mist' "
                           "BEGIN SELECT RAISE(ABORT, 'test failure'); END"));
        network.responses[CatalogPath].pause = true;
        refresh->click();
        QVERIFY(!refresh->isEnabled());
        QVERIFY(answerLeave(QMessageBox::Yes, requestRemoval, SkinRemovalPrompt));
        QVERIFY(status->text().contains(QStringLiteral("无法删除主题")));
        QCOMPARE(db.setting("appearance_palette"), id);
        QCOMPARE(skinState(list, id), QStringLiteral("使用中"));
        QVERIFY(QFileInfo::exists(manifestPath) && QFileInfo::exists(imagePath));
        QVERIFY(query.exec("DROP TRIGGER reject_skin_fallback"));

        QVERIFY(answerLeave(QMessageBox::Yes, requestRemoval, SkinRemovalPrompt));
        QVERIFY(!skinRow(list, id));
        QCOMPARE(skinState(list, QStringLiteral("mist")), QStringLiteral("使用中"));
        QCOMPARE(palette->currentData().toString(), QStringLiteral("mist"));
        QCOMPARE(db.setting("appearance_palette"), QStringLiteral("mist"));
        QCOMPARE(UiTheme::layoutId(), QStringLiteral("compact"));
        QVERIFY(!QFileInfo::exists(manifestPath) && !QFileInfo::exists(imagePath));
        QCOMPARE(network.requests.size(), 2);
        QTest::keyClick(page, Qt::Key_Escape);
        QVERIFY(!workspace->content(SkinLibraryKey));
    }

    void themeDensityPreservesWorkspaceState() {
        AppDatabase db;
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));
        const int projectId = db.projects().first().id;
        for (const auto &name : {QStringLiteral("展开文件夹"), QStringLiteral("收起文件夹")}) {
            int folderId = 0;
            QVERIFY2(db.addRecordFolder(projectId, name, &folderId, &error), qPrintable(error));
            QVERIFY2(db.addWorkRecord(projectId, name + QStringLiteral("中的记录"),
                                     QStringLiteral("保留记录内容。"), folderId, &error), qPrintable(error));
        }
        // Create the views under a saved compact preference, then cycle their styles.
        AppDatabase::Reminder done;
        done.title = QStringLiteral("已完成的提醒");
        done.remindAt = QDateTime::currentDateTime();
        done.completed = true;
        QVERIFY2(db.addReminder(done, nullptr, &error), qPrintable(error));
        QVERIFY(TestSkinFixture::writeInstalled(qApp->applicationDirPath() + "/data/skins"));
        QVERIFY(db.setSetting("appearance_palette", TestSkinFixture::Id));
        QVERIFY(db.setSetting("appearance_layout", "compact"));
        MainWindow window; prepareWindow(window);
        QVERIFY(window.ready());
        auto *palette = window.findChild<QComboBox *>("appearancePalette");
        auto *layout = window.findChild<QComboBox *>("appearanceLayout");
        auto *projects = window.findChild<ProjectsPage *>();
        auto *calendar = window.findChild<CalendarPage *>();
        QVERIFY(palette && layout && projects && calendar);
        auto *projectList = projects->findChild<QListWidget *>("projectList");
        auto *workList = projects->findChild<QTreeWidget *>("workRecordList");
        auto *diaryList = calendar->findChild<QListWidget *>("diaryList");
        auto *reminders = calendar->findChild<QListWidget *>("reminderList");
        QVERIFY(reminders);
        const auto completed = reminders->findItems(done.title, Qt::MatchContains);
        QCOMPARE(completed.size(), 1);
        QVERIFY(projectList && workList && diaryList);
        QCOMPARE(workList->topLevelItemCount(), 3);
        auto *expanded = workList->topLevelItem(0);
        auto *collapsed = workList->topLevelItem(1);
        QCOMPARE(expanded->childCount(), 1);
        QCOMPARE(collapsed->childCount(), 1);
        expanded->setExpanded(true);
        collapsed->setExpanded(false);
        auto *selectedRecord = workList->topLevelItem(2);
        workList->setCurrentItem(selectedRecord);
        diaryList->setCurrentRow(0);
        button(calendar, QStringLiteral("日记集合"))->click();
        auto *collectionList = window.findChild<QListWidget *>("diaryCollectionList");
        QVERIFY(collectionList);
        collectionList->setCurrentRow(3);
        auto *selectedProject = projectList->currentItem();
        const QColor customColor(UiTheme::Muted);
        selectedProject->setForeground(customColor);
        auto *selectedDiary = diaryList->currentItem();
        auto *selectedCollection = collectionList->currentItem();
        const auto folderIndex = workList->model()->index(0, 0);
        struct Row {
            QAbstractItemView *view;
            QPersistentModelIndex index;
            int normalHeight;
        };
        const QList<Row> rows {
            {projectList, projectList->model()->index(0, 0), 44},
            {workList, folderIndex, 44},
            {workList, workList->model()->index(0, 0, folderIndex), 44},
            {workList, workList->model()->index(2, 0), 44},
            {diaryList, diaryList->model()->index(0, 0), 42},
            {collectionList, collectionList->model()->index(3, 0), 48}
        };
        auto *indexPane = projects->findChild<QWidget *>("workspaceIndex");
        QVERIFY(indexPane);
        auto *navButton = window.findChildren<QPushButton *>("navButton").at(1);
        int normalButtonHeight = 0, normalSpacing = 0;
        QMargins normalMargins;
        const QList<QPair<QString, QString>> appearances {
            {TestSkinFixture::Id, "compact"}, {TestSkinFixture::Id, "standard"}, {"paper", "standard"}, {"paper", "compact"}
        };
        for (const auto &[paletteId, layoutId] : appearances) {
            const auto previousLayout = layout->currentData().toString();
            palette->setCurrentIndex(palette->findData(paletteId));
            QCOMPARE(layout->currentData().toString(), previousLayout);
            QCOMPARE(UiTheme::layoutId(), previousLayout);
            layout->setCurrentIndex(layout->findData(layoutId));
            QCOMPARE(UiTheme::paletteId(), paletteId);
            QCoreApplication::processEvents();
            QCOMPARE(completed.first()->foreground().color(), QColor(UiTheme::Muted));
            QCOMPARE(selectedProject->foreground().color(), customColor);
            for (const auto &row : rows) {
                QVERIFY(row.index.isValid());
                const int height = row.view->visualRect(row.index).height();
                if (layoutId == QStringLiteral("compact"))
                    QVERIFY(height >= 24 && height < row.normalHeight);
                else
                    QCOMPARE(height, row.normalHeight);
            }
            if (layoutId == QStringLiteral("standard") && normalButtonHeight == 0) {
                normalButtonHeight = navButton->height();
                normalMargins = indexPane->layout()->contentsMargins();
                normalSpacing = indexPane->layout()->spacing();
            } else if (layoutId == QStringLiteral("standard")) {
                QCOMPARE(navButton->height(), normalButtonHeight);
                QCOMPARE(indexPane->layout()->contentsMargins(), normalMargins);
                QCOMPARE(indexPane->layout()->spacing(), normalSpacing);
            } else if (normalButtonHeight > 0) {
                QVERIFY(navButton->height() < normalButtonHeight);
                QVERIFY(indexPane->layout()->contentsMargins().top() < normalMargins.top());
                QVERIFY(indexPane->layout()->spacing() < normalSpacing);
            }
            QCOMPARE(projectList->currentItem(), selectedProject);
            QCOMPARE(workList->currentItem(), selectedRecord);
            QCOMPARE(diaryList->currentItem(), selectedDiary);
            QCOMPARE(collectionList->currentItem(), selectedCollection);
            QVERIFY(expanded->isExpanded());
            QVERIFY(!collapsed->isExpanded());
        }
    }
    void returnDetachAttachPreservesDraftAndUndo() {
        MainWindow window; prepareWindow(window);
        window.findChildren<QPushButton*>("navButton").at(1)->click();
        auto *projects=window.findChild<ProjectsPage*>(); auto *workspace=ContentWorkspace::find(projects);
        auto *tree=projects->findChild<QTreeWidget*>(); QVERIFY(tree); tree->setCurrentItem(tree->topLevelItem(0));
        capture(window,"projects-clean");
        window.resize(980,640); QTest::qWait(30); capture(window,"projects-clean-compact"); window.resize(1180,760);
        QTest::mouseClick(tree->viewport(),Qt::LeftButton,Qt::NoModifier,tree->visualItemRect(tree->currentItem()).center());
        QTest::mouseDClick(tree->viewport(),Qt::LeftButton,Qt::NoModifier,tree->visualItemRect(tree->currentItem()).center());
        auto *record=window.findChild<CollaborationRecordDialog*>(); QVERIFY(record); QVERIFY(!record->isWindow());
        const auto key=workspace->currentKey(); QVERIFY(!key.isEmpty());
        auto *title=record->findChild<QLineEdit*>("workRecordTitle"); title->setFocus(); title->end(false);
        title->insert(QStringLiteral(" · 未保存")); const QString draft=title->text(); QVERIFY(title->isUndoAvailable());
        QVERIFY(answerLeave(QMessageBox::Cancel, [&] { workspace->back(); }));
        QVERIFY(record->isVisible()); QCOMPARE(title->text(),draft);
        QVERIFY(answerLeave(QMessageBox::Save, [&] { workspace->back(); }));
        QVERIFY(projects->isVisible());
        window.findChildren<QPushButton*>("navButton").at(0)->click();
        window.findChildren<QPushButton*>("navButton").at(1)->click();
        tree->setFocus(); QTest::keyClick(tree,Qt::Key_Return);
        QCOMPARE(window.findChildren<CollaborationRecordDialog*>().size(),1); QCOMPARE(title->text(),draft);
        button(workspace,QStringLiteral("独立打开"))->click();
        auto *host=window.findChild<QDialog*>("contentWindow"); QVERIFY(host); QVERIFY(host->isVisible());
        QCOMPARE(host->findChild<CollaborationRecordDialog*>(),record);
        window.findChildren<QPushButton*>("navButton").at(0)->click();
        window.showMinimized();
        button(host,QStringLiteral("收回主窗口"))->click();
        QVERIFY(!window.isMinimized());
        QCOMPARE(workspace->currentKey(),key); QVERIFY(record->isVisible()); QCOMPARE(title->text(),draft);
        QVERIFY(title->isUndoAvailable()); title->undo(); QVERIFY(title->text()!=draft); title->redo(); QCOMPARE(title->text(),draft);
        title->insert(QStringLiteral(" · 新草稿"));
        const auto pending = title->text();
        capture(window,"record-draft");
        bool prompted=false;
        QTimer::singleShot(0,[&]{auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); if(box){prompted=true;box->button(QMessageBox::Cancel)->click();}});
        QVERIFY(!workspace->closeAll()); QVERIFY(prompted); QCOMPARE(title->text(),pending);
        window.activateWindow(); QTest::qWait(30); title->setFocus();
        QTest::keyClick(title, Qt::Key_S, Qt::ControlModifier); QVERIFY(record->isVisible());
        QVERIFY(workspace->closeAll()); QVERIFY(projects->isVisible());
    }
    void titleOnlyCollaborationRecords() {
        AppDatabase db; QString error; QVERIFY2(db.open(&error), qPrintable(error));
        const int projectId = db.projects().first().id;
        int folderId = 0;
        QVERIFY2(db.addRecordFolder(projectId, QStringLiteral("仅标题记录"), &folderId, &error), qPrintable(error));
        const int initialCount = db.workRecords(projectId).size();
        AppDatabase::WorkRecord draft; draft.projectId = projectId; draft.folderId = folderId;
        CollaborationRecordDialog editor(&db, draft); editor.show(); QTest::qWait(30);
        auto *title = editor.findChild<QLineEdit *>("workRecordTitle");
        auto *status = editor.findChild<QLabel *>("recordSaveStatus");
        auto *summary = editor.findChild<HeadingTextEdit *>("recordSummaryEditor");
        auto *body = editor.findChild<HeadingTextEdit *>("recordBodyEditor");
        QCOMPARE(status->text(), QStringLiteral("待填写标题"));
        QVERIFY(status->toolTip().contains(QStringLiteral("摘要和正文可留空")));
        title->setText(QStringLiteral(" \t　")); saveEditor(&editor);
        QCOMPARE(db.workRecords(projectId).size(), initialCount);
        QVERIFY(!editor.hasSaved()); QVERIFY(title->hasFocus());
        title->setText(QStringLiteral("  先记录一个标题  "));
        QCOMPARE(status->text(), QStringLiteral("未保存 · Ctrl+S"));
        saveEditor(&editor);
        QVERIFY(editor.hasSaved()); QVERIFY(editor.record().id > 0);
        QCOMPARE(editor.record().title, QStringLiteral("先记录一个标题"));
        QCOMPARE(editor.record().folderId, folderId);
        QVERIFY(editor.record().summary.isEmpty()); QVERIFY(editor.record().content.isEmpty());
        QCOMPARE(status->text(), QStringLiteral("已保存"));
        const int recordId = editor.record().id;
        const auto createdAt = editor.record().createdAt;
        const auto updatedAt = editor.record().updatedAt;
        saveEditor(&editor);
        QCOMPARE(editor.record().updatedAt, updatedAt);
        QCOMPARE(db.workRecords(projectId).size(), initialCount + 1);
        QVERIFY(editor.confirmLeave()); editor.hide();

        AppDatabase reopened; QVERIFY(reopened.open());
        AppDatabase::WorkRecord persisted;
        for (const auto &record : reopened.workRecords(projectId)) if (record.id == recordId) persisted = record;
        QCOMPARE(persisted.id, recordId); QCOMPARE(persisted.title, QStringLiteral("先记录一个标题"));
        QVERIFY(persisted.summary.isEmpty()); QVERIFY(persisted.content.isEmpty());
        QCOMPARE(persisted.folderId, folderId); QCOMPARE(persisted.createdAt, createdAt);
        CollaborationRecordDialog editAgain(&reopened, persisted); editAgain.show(); QTest::qWait(30);
        title = editAgain.findChild<QLineEdit *>("workRecordTitle");
        status = editAgain.findChild<QLabel *>("recordSaveStatus");
        summary = editAgain.findChild<HeadingTextEdit *>("recordSummaryEditor");
        body = editAgain.findChild<HeadingTextEdit *>("recordBodyEditor");
        QCOMPARE(status->text(), QStringLiteral("已保存"));
        summary->setPlainText(QStringLiteral("稍后补充摘要")); saveEditor(&editAgain);
        QCOMPARE(editAgain.record().summary, QStringLiteral("稍后补充摘要"));
        QVERIFY(editAgain.record().content.isEmpty());
        body->setPlainText(QStringLiteral("稍后补充正文")); saveEditor(&editAgain);
        QCOMPARE(editAgain.record().content, QStringLiteral("稍后补充正文"));
        summary->setPlainText(QStringLiteral("")); body->setPlainText(QStringLiteral("")); saveEditor(&editAgain);
        QVERIFY(editAgain.record().summary.isEmpty()); QVERIFY(editAgain.record().content.isEmpty());
        QCOMPARE(editAgain.record().id, recordId); QCOMPARE(editAgain.record().createdAt, createdAt);
        title->setText(QStringLiteral("　 \t")); saveEditor(&editAgain);
        QCOMPARE(editAgain.record().title, persisted.title); QCOMPARE(status->text(), QStringLiteral("待填写标题"));
        title->setText(QStringLiteral("离开时保存的标题"));
        bool canLeave = false;
        QVERIFY(answerLeave(QMessageBox::Save, [&] { canLeave = editAgain.confirmLeave(); }));
        QVERIFY(canLeave); QCOMPARE(editAgain.record().title, QStringLiteral("离开时保存的标题"));
        editAgain.hide();

        MainWindow window; prepareWindow(window);
        window.findChildren<QPushButton *>("navButton").at(1)->click();
        auto *projects = window.findChild<ProjectsPage *>();
        auto *tree = projects->findChild<QTreeWidget *>("workRecordList");
        QTreeWidgetItem *savedItem = nullptr;
        for (auto *item : tree->findItems(QStringLiteral("离开时保存的标题"), Qt::MatchExactly | Qt::MatchRecursive)) savedItem = item;
        QVERIFY(savedItem); tree->setCurrentItem(savedItem); tree->itemDoubleClicked(savedItem, 0);
        auto *record = window.findChild<CollaborationRecordDialog *>(); QVERIFY(record);
        QCOMPARE(record->findChild<QLabel *>("recordSaveStatus")->text(), QStringLiteral("已保存"));
        capture(window, "v3.6.2-title-only-saved");
        window.resize(980, 640); QCoreApplication::processEvents(); capture(window, "v3.6.2-title-only-saved-compact");
        QVERIFY(ContentWorkspace::find(projects)->closeAll());
    }
    void collaborationLeaveGuards() {
        MainWindow window; prepareWindow(window);
        const auto nav = window.findChildren<QPushButton *>("navButton");
        auto *stack = window.findChild<QStackedWidget *>("pageStack");
        auto *sidebar = window.findChild<QWidget *>("sidebar");
        QVERIFY(stack && sidebar);
        const auto *calendar = stack->currentWidget();
        nav.at(1)->click();
        auto *projects = window.findChild<ProjectsPage *>();
        QVERIFY(projects && projects->isVisible());
        auto *workspace = ContentWorkspace::find(projects);
        QVERIFY(workspace);
        QCOMPARE(stack->currentWidget(), workspace);
        if (QGuiApplication::platformName() == QStringLiteral("offscreen"))
            QVERIFY(!stack->findChild<QWidget *>("pageTransition", Qt::FindDirectChildrenOnly));
        QTRY_VERIFY(hasNavigationMarker(*sidebar, *nav.at(1)));
        auto *tree = projects->findChild<QTreeWidget *>("workRecordList");
        tree->setCurrentItem(tree->topLevelItem(0)); tree->itemDoubleClicked(tree->currentItem(), 0);
        auto *record = qobject_cast<CollaborationRecordDialog *>(workspace->content(workspace->currentKey()));
        QVERIFY(record);
        auto *title = record->findChild<QLineEdit *>("workRecordTitle");
        const auto verifyFonts = [&] {
            auto *summary = record->findChild<HeadingTextEdit *>("recordSummaryEditor");
            auto *body = record->findChild<HeadingTextEdit *>("recordBodyEditor");
            auto *preview = summary->findChild<QTextEdit *>("headingPreview");
            QCOMPARE(preview->font().pointSizeF(), 24.0);
            QCOMPARE(preview->document()->find(QStringLiteral("返回时")).charFormat().fontPointSize(), 24.0);
            QCOMPARE(summary->findChild<QPlainTextEdit *>("headingSource")->font().pointSizeF(), 24.0);
            // Body has not been shown yet; its font is restored when selected.
            auto *sections = record->findChild<QTabBar *>("recordSectionTabs");
            sections->setCurrentIndex(1); QCoreApplication::processEvents();
            QCOMPARE(body->findChild<QTextEdit *>("headingPreview")->font().pointSizeF(), 20.0);
            sections->setCurrentIndex(0);
        };
        verifyFonts();
        const auto original = title->text();
        title->setText(original + " unsaved");
        for (int target : {0, 2, 3, 4}) {
            QSignalSpy rejectedSelection(nav.at(target), &QPushButton::toggled);
            QVERIFY(answerLeave(QMessageBox::Cancel, [&] { nav.at(target)->click(); }));
            QCOMPARE(rejectedSelection.size(), 0);
            QVERIFY(record->isVisible()); QVERIFY(nav.at(1)->isChecked()); QVERIFY(!nav.at(target)->isChecked());
            QCOMPARE(title->text(), original + " unsaved");
        }
        QCOMPARE(stack->currentWidget(), workspace);
        QVERIFY(hasNavigationMarker(*sidebar, *nav.at(1)));
        // Both the visible back button and its keyboard shortcut use the guard.
        QVERIFY(answerLeave(QMessageBox::Cancel, [&] { button(workspace, QStringLiteral("返回"))->click(); }));
        window.activateWindow(); QTest::qWait(30); title->setFocus();
        QVERIFY(answerLeave(QMessageBox::Discard, [&] { QTest::keyClick(title, Qt::Key_Left, Qt::AltModifier); }));
        QVERIFY(projects->isVisible()); QCOMPARE(title->text(), original);
        QVERIFY(workspace->activate(record->property("contentKey").toString()));
        title->setText(original + " saved on switch");
        QVERIFY(answerLeave(QMessageBox::Save, [&] { nav.at(0)->click(); }));
        QCOMPARE(stack->currentWidget(), calendar);
        QVERIFY(nav.first()->isChecked() && !nav.at(1)->isChecked());
        QTRY_VERIFY(hasNavigationMarker(*sidebar, *nav.first()));
        QVERIFY(!hasNavigationMarker(*sidebar, *nav.at(1)));
        nav.at(1)->click();
        QVERIFY(record->isVisible()); QCOMPARE(record->record().title, title->text());
        QVERIFY(!title->isReadOnly());

        // Failed saves block navigation and preserve all draft fields.
        title->setText(original + " save failure");
        const QString connection = "leave-guard-failure";
        {
            AppDatabase db; QVERIFY(db.open());
            auto sql = QSqlDatabase::addDatabase("QSQLITE", connection);
            sql.setDatabaseName(db.databasePath()); QVERIFY(sql.open());
            QSqlQuery query(sql);
            QVERIFY(query.exec("CREATE TRIGGER reject_leave_save BEFORE UPDATE ON project_work_records BEGIN SELECT RAISE(ABORT,'injected'); END"));
            QVERIFY(answerLeave(QMessageBox::Save, [&] { workspace->back(); }));
            QVERIFY(record->isVisible());
            QVERIFY(answerLeave(QMessageBox::Save, [&] { nav.at(3)->click(); }));
            QVERIFY(record->isVisible()); QVERIFY(nav.at(1)->isChecked());
            QCOMPARE(title->text(), original + " save failure");
            QVERIFY(query.exec("DROP TRIGGER reject_leave_save"));
        }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(answerLeave(QMessageBox::Discard, [&] { nav.at(0)->click(); })); nav.at(1)->click();
        QCOMPARE(title->text(), record->record().title);

        // Ctrl+S works while embedded, including in the title, without leaving edit mode.
        title->setText(original + " keyboard save"); window.activateWindow(); QTest::qWait(30); title->setFocus();
        QTest::keyClick(title, Qt::Key_S, Qt::ControlModifier);
        QCOMPARE(record->record().title, title->text()); QVERIFY(!title->isReadOnly());
        const auto aligned = [&] {
            auto *tabs=record->findChild<QTabBar *>("recordSectionTabs");
            auto *summary=record->findChild<HeadingTextEdit *>("recordSummaryEditor");
            QCOMPARE(tabs->mapTo(record,tabs->tabRect(1).topRight()).x(),summary->mapTo(record,summary->rect().topRight()).x());
        };
        aligned(); capture(window, "v3.5.6-record-summary");
        window.resize(980,640); QCoreApplication::processEvents(); aligned(); capture(window, "v3.5.6-record-summary-compact");
        title->setText(original + " close draft");
        QVERIFY(answerLeave(QMessageBox::Cancel, [&] { window.close(); })); QVERIFY(window.isVisible());
        window.findChild<QAbstractButton *>("closeToTrayEnabled")->setChecked(true);
        auto *tray = window.findChild<QSystemTrayIcon *>(); tray->show(); QVERIFY(tray->isVisible());
        QVERIFY(answerLeave(QMessageBox::Cancel, [&] { window.close(); })); QVERIFY(window.isVisible());
        QVERIFY(answerLeave(QMessageBox::Discard, [&] { window.close(); })); QVERIFY(!window.isVisible());
        window.showMainWindow(); QCOMPARE(title->text(), record->record().title);
        title->setText(original + " close draft");
        QVERIFY(answerLeave(QMessageBox::Cancel, [&] { button(workspace, QStringLiteral("关闭"))->click(); }));
        QVERIFY(record->isVisible());

        // Detach/attach moves the same editor; only closing it asks to save.
        button(workspace, QStringLiteral("独立打开"))->click();
        auto *host = window.findChild<QDialog *>("contentWindow"); QVERIFY(host && host->isVisible());
        verifyFonts();
        QVERIFY(answerLeave(QMessageBox::Cancel, [&] { host->close(); })); QVERIFY(host->isVisible());
        QVERIFY(answerLeave(QMessageBox::Save, [&] { host->close(); })); QVERIFY(!host->isVisible());
        QVERIFY(workspace->closeAll());
    }
    void gitDetailsStayInTheSameSurface() {
        MainWindow window; prepareWindow(window); window.findChildren<QPushButton*>("navButton").at(1)->click();
        auto *page=window.findChild<ProjectsPage*>(); auto *workspace=ContentWorkspace::find(page);
        QTRY_VERIFY(page->findChild<QPushButton*>("gitHistoryButton")->isEnabled());
        capture(window,"v3.6.3-project-git-entry");
        window.resize(980,640);capture(window,"v3.6.3-project-git-entry-compact");window.resize(1180,760);
        page->findChild<QPushButton*>("gitHistoryButton")->click();
        auto *history=window.findChild<GitHistoryWindow*>(); QVERIFY(history); QVERIFY(!history->isWindow());
        auto *table=history->findChild<QTableView*>("gitCommits");
        QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount()>2,15000);
        table->setCurrentIndex(table->model()->index(2,1)); const auto selected=table->currentIndex();
        capture(window,"git-history"); window.resize(980,640); QTest::qWait(30); capture(window,"git-history-compact");
        table->setFocus(); QTest::keyClick(table,Qt::Key_Return);
        auto *detail=window.findChild<GitCommitDialog*>(); QVERIFY(detail); QVERIFY(!detail->isWindow());
        QTRY_VERIFY_WITH_TIMEOUT(detail->findChild<QListWidget*>("gitFiles")->count()>0,15000);
        QTRY_VERIFY_WITH_TIMEOUT(!detail->findChild<QPlainTextEdit*>("gitDiff")->toPlainText().contains(QStringLiteral("正在读取")),15000);
        capture(window,"git-detail-compact"); workspace->back(); QVERIFY(history->isVisible()); QCOMPARE(table->currentIndex(),selected);
        button(workspace,QStringLiteral("独立打开"))->click(); auto *host=window.findChild<QDialog*>("contentWindow"); QVERIFY(host);
        host->activateWindow(); QTest::qWait(50); table->setFocus(); QTest::keyClick(table,Qt::Key_Return); QTRY_VERIFY(detail->isVisible());
        QCOMPARE(window.findChildren<QDialog*>("contentWindow").size(),1);
        button(host,QStringLiteral("返回"))->click(); QVERIFY(history->isVisible());
        button(host,QStringLiteral("收回主窗口"))->click(); QVERIFY(history->isVisible());
        QPointer<GitCommitDialog> guard=detail; QVERIFY(workspace->closeAll());
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete); QVERIFY(!guard);
    }
    void mainBackDoesNotNavigateDetachedContent() {
        auto *root=new QWidget;
        ContentWorkspace workspace(root); workspace.resize(980,640); workspace.show();
        auto *detached=new QDialog; detached->setWindowTitle(QStringLiteral("独立内容"));
        workspace.present("detached",detached);
        button(&workspace,QStringLiteral("独立打开"))->click();
        auto *host=workspace.findChild<QDialog*>("contentWindow"); QVERIFY(host && host->isVisible());
        QVERIFY(root->isVisible()); QVERIFY(workspace.currentKey().isEmpty());
        workspace.activateWindow(); root->setFocus(); QTest::qWait(40);
        QTest::keyClick(root,Qt::Key_Left,Qt::AltModifier); QTest::qWait(20);
        QVERIFY(root->isVisible()); QVERIFY(workspace.currentKey().isEmpty());
        QVERIFY(host->isVisible()); QVERIFY(host->isAncestorOf(detached));
        auto *parent=new QDialog; workspace.present("parent",parent);
        auto *child=new QDialog; workspace.present("child",child,parent);
        workspace.activateWindow(); child->setFocus(); QTest::qWait(40);
        QTest::keyClick(child,Qt::Key_Left,Qt::AltModifier); QTest::qWait(20);
        QCOMPARE(workspace.currentKey(),QStringLiteral("parent"));
        QVERIFY(parent->isVisible()); QVERIFY(host->isVisible()); QVERIFY(host->isAncestorOf(detached));
        workspace.back(); QVERIFY(root->isVisible()); QVERIFY(host->isVisible());
        QVERIFY(workspace.closeAll());
    }
    void diaryCollectionAndSavedNewEntry() {
        MainWindow window; prepareWindow(window); auto *page=window.findChild<CalendarPage*>();
        capture(window,"calendar-clean");
        window.resize(980,640); QTest::qWait(30); capture(window,"calendar-clean-compact"); window.resize(1180,760);
        auto *workspace=ContentWorkspace::find(page); auto *open=button(page,QStringLiteral("日记集合")); QVERIFY(open); open->click();
        auto *collection=workspace->content("diary/collection"); QVERIFY(collection); QVERIFY(!collection->isWindow());
        auto *list=collection->findChild<QListWidget*>("diaryCollectionList"); auto *search=collection->findChild<QLineEdit*>();
        search->setText(QStringLiteral("导航体验")); list->setCurrentRow(4); const int row=list->currentRow();
        capture(window,"diary-collection-clean");
        list->setFocus(); QTest::keyClick(list,Qt::Key_Return); auto *editor=workspace->content(workspace->currentKey());
        QVERIFY(editor && editor!=collection); auto *text=editor->findChild<HeadingTextEdit*>(); QVERIFY(!text->isPreviewMode());
        capture(window,"v3.5.6-diary"); window.resize(980,640); capture(window,"v3.5.6-diary-compact");
        workspace->back(); QCOMPARE(list->currentRow(),row); QCOMPARE(search->text(),QStringLiteral("导航体验"));
        workspace->back(); QVERIFY(page->isVisible());
        auto *add=button(page,QStringLiteral("添加日记")); QVERIFY(add); add->click();
        editor=workspace->content(workspace->currentKey()); QVERIFY(editor);
        editor->findChild<QLineEdit*>("diaryTitle")->setText(QStringLiteral("新日记草稿")); text=editor->findChild<HeadingTextEdit*>(); text->setPlainText(QStringLiteral("保存后原位阅读"));
        saveEditor(editor); QVERIFY(editor->isVisible()); QVERIFY(!text->isPreviewMode());
        const QString key=workspace->currentKey(); QVERIFY(!key.contains("new"));
        workspace->back(); QVERIFY(workspace->activate(key)); QCOMPARE(workspace->content(key),editor);
        QVERIFY(workspace->closeAll());
        auto *daily=page->findChild<QListWidget*>("diaryList"); daily->setCurrentRow(0); daily->setFocus();
        QTest::keyClick(daily,Qt::Key_Return); QVERIFY(!workspace->currentKey().isEmpty());
        QVERIFY(workspace->content(workspace->currentKey())->findChild<QLineEdit*>("diaryTitle"));
        QVERIFY(workspace->closeAll());
    }
    void reminderDraftSaveFailureAndRetry() {
        MainWindow window; prepareWindow(window); auto *page=window.findChild<CalendarPage*>();
        auto *workspace=ContentWorkspace::find(page); button(page,QStringLiteral("添加提醒"))->click();
        auto *editor=workspace->content(workspace->currentKey()); QVERIFY(editor);
        auto *title=editor->findChild<QLineEdit*>("reminderTitle"); title->setText(QStringLiteral("测试提醒草稿"));
        editor->findChild<QDateTimeEdit*>("reminderTime")->setDateTime(QDateTime::currentDateTime().addDays(7));
        QVERIFY(answerLeave(QMessageBox::Cancel,[&]{workspace->back();})); QVERIFY(editor->isVisible());
        const QString connection=QStringLiteral("navigation-failure");
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE",connection); db.setDatabaseName(qApp->applicationDirPath()+"/data/orchestrate.sqlite3"); QVERIFY(db.open());
            QSqlQuery query(db); QVERIFY(query.exec("CREATE TRIGGER reject_reminder BEFORE INSERT ON reminders BEGIN SELECT RAISE(ABORT,'test failure'); END"));
            saveEditor(editor);
            QVERIFY(editor->isVisible()); QCOMPARE(title->text(),QStringLiteral("测试提醒草稿"));
            QCOMPARE(editor->findChild<QLabel*>("reminderSaveStatus")->text(),QStringLiteral("保存失败"));
            QVERIFY(editor->findChild<QLabel*>("reminderSaveStatus")->toolTip().contains("test failure"));
            QVERIFY(answerLeave(QMessageBox::Save,[&]{workspace->back();})); QVERIFY(editor->isVisible());
            QVERIFY(query.exec("DROP TRIGGER reject_reminder"));
        }
        QSqlDatabase::removeDatabase(connection);
        saveEditor(editor);
        QVERIFY(!workspace->currentKey().contains("new")); QVERIFY(!editor->findChild<HeadingTextEdit*>()->isPreviewMode());
        const auto date=editor->findChild<QDateTimeEdit*>("reminderTime")->date();
        capture(window,"v3.5.6-reminder"); window.resize(980,640); capture(window,"v3.5.6-reminder-compact"); QVERIFY(workspace->closeAll());
        page->findChild<QCalendarWidget*>()->setSelectedDate(date);
        auto *list=page->findChild<QListWidget*>("reminderList"); list->setCurrentRow(0); list->setFocus();
        QTest::keyClick(list,Qt::Key_Enter); QVERIFY(!workspace->currentKey().isEmpty());
        QCOMPARE(workspace->content(workspace->currentKey())->findChild<QLineEdit*>("reminderTitle")->text(),QStringLiteral("测试提醒草稿"));
        QVERIFY(workspace->closeAll());
    }
    void collectionCloseProtectsHiddenDescendant() {
        MainWindow window; prepareWindow(window); auto *page=window.findChild<CalendarPage*>();
        auto *workspace=ContentWorkspace::find(page); button(page,QStringLiteral("日记集合"))->click();
        auto *collection=workspace->content("diary/collection"); auto *list=collection->findChild<QListWidget*>("diaryCollectionList");
        list->setCurrentRow(0);
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualItemRect(list->currentItem()).center());
        QTest::mouseDClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualItemRect(list->currentItem()).center());
        auto *editor=workspace->content(workspace->currentKey()); auto *text=editor->findChild<HeadingTextEdit*>();
        text->setPlainText(QStringLiteral("集合中的未保存日记"));
        QVERIFY(answerLeave(QMessageBox::Cancel,[&]{workspace->back();})); QVERIFY(editor->isVisible());
        QTimer::singleShot(0,[]{auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); if(box)box->button(QMessageBox::Cancel)->click();});
        QVERIFY(!workspace->closePage("diary/collection")); QVERIFY(editor->isVisible());
        saveEditor(editor);
        QVERIFY(workspace->closePage("diary/collection")); QVERIFY(page->isVisible());
    }
    void closingMainWindowProtectsDraft() {
        MainWindow window; prepareWindow(window);
        window.findChildren<QPushButton *>("navButton").at(3)->click();
        auto *page = window.findChild<ClipboardPage *>();
        page->findChild<QPushButton *>("clipboardNewItem")->click();
        auto *workspace = ContentWorkspace::find(page);
        auto *editor = workspace->content(workspace->currentKey());
        auto *body = editor->findChild<QPlainTextEdit *>("clipboardItemBody");
        body->setPlainText(QStringLiteral("保留草稿"));
        auto *toTray = window.findChild<QAbstractButton *>("closeToTrayEnabled");
        window.findChild<QSystemTrayIcon *>()->show();
        for (bool closeToTray : {false, true}) {
            toTray->setChecked(closeToTray);
            QVERIFY(answerLeave(QMessageBox::Cancel, [&]{ window.close(); }));
            QVERIFY(window.isVisible());
            QCOMPARE(body->toPlainText(), QStringLiteral("保留草稿"));
        }
        saveEditor(editor);
        toTray->setChecked(false);
        QVERIFY(window.close());
    }
    void unifiedDraftLeaveGuards_data() {
        QTest::addColumn<QString>("kind");
        for (const auto *name:{"diary","reminder","clipboard"}) QTest::newRow(name)<<QString::fromLatin1(name);
    }
    void unifiedDraftLeaveGuards() {
        QFETCH(QString,kind);
        MainWindow window; prepareWindow(window);
        const auto nav=window.findChildren<QPushButton *>("navButton");
        QWidget *page=kind=="clipboard" ? static_cast<QWidget *>(window.findChild<ClipboardPage *>()) : window.findChild<CalendarPage *>();
        const int module=kind=="clipboard" ? 3 : 0; nav.at(module)->click();
        auto *workspace=ContentWorkspace::find(page);
        button(page,kind=="clipboard" ? QStringLiteral("新建") : kind=="diary" ? QStringLiteral("添加日记") : QStringLiteral("添加提醒"))->click();
        auto *editor=workspace->content(workspace->currentKey()); auto *draft=dynamic_cast<DraftDialog *>(editor); QVERIFY(draft);
        auto *title=editor->findChild<QLineEdit *>(kind=="clipboard" ? "clipboardItemTitle" : kind+"Title"); QVERIFY(title);
        auto *text=editor->findChild<HeadingTextEdit *>();
        auto *plain=editor->findChild<QPlainTextEdit *>("clipboardItemBody");
        const auto setBody=[&](const QString &value){ if(text) text->setPlainText(value); else plain->setPlainText(value); };
        const auto body=[&]{return text ? text->toPlainText() : plain->toPlainText();};
        auto *status=editor->findChild<QLabel *>(kind=="clipboard" ? "clipboardEditorError" : kind+"SaveStatus"); QVERIFY(status);
        QVERIFY(!editor->findChild<QDialogButtonBox *>()); QVERIFY(!title->isReadOnly());
        if (text) {
            QVERIFY(!text->isPreviewMode()); QVERIFY(!text->findChild<QTabBar *>("headingModeTabs")->isVisible());
            QCOMPARE(text->findChild<QTextEdit *>("headingPreview")->font().pointSizeF(),20.0);
        }
        title->setText(QStringLiteral("统一编辑测试")); setBody(QStringLiteral("保存后继续编辑\n第二行正文"));
        QCOMPARE(status->text(),QStringLiteral("未保存 · Ctrl+S"));
        QVERIFY(status->mapTo(editor,QPoint(0,0)).y()< (text ? text->y() : plain->y()));
        saveEditor(editor); QVERIFY(editor->isVisible()); QVERIFY(!draft->dirty());
        QCOMPARE(status->text(),QStringLiteral("已保存"));
        const auto key=workspace->currentKey(); QVERIFY(!key.contains("new"));
        const auto savedBody=body(); const auto savedTitle=title->text();
        auto *time=editor->findChild<QDateTimeEdit *>("reminderTime");
        auto *repeat=editor->findChild<QComboBox *>("reminderRepeat");
        auto *project=editor->findChild<QComboBox *>("reminderProject");
        const auto savedTime=time ? time->dateTime() : QDateTime();
        const int savedRepeat=repeat ? repeat->currentIndex() : 0, savedProject=project ? project->currentIndex() : 0;
        title->setText(savedTitle+" draft"); setBody("changed body");
        if(time) { time->setDateTime(savedTime.addDays(2)); repeat->setCurrentIndex(1); project->setCurrentIndex(1); }
        QVERIFY(answerLeave(QMessageBox::Cancel,[&]{workspace->back();}));
        QVERIFY(answerLeave(QMessageBox::Cancel,[&]{nav.at(1)->click();})); QVERIFY(nav.at(module)->isChecked());
        QVERIFY(answerLeave(QMessageBox::Discard,[&]{workspace->back();})); QVERIFY(page->isVisible());
        QVERIFY(workspace->activate(key)); QCOMPARE(body(),savedBody); QCOMPARE(title->text(),savedTitle); QVERIFY(!draft->dirty());
        if(time) { QCOMPARE(time->dateTime(),savedTime); QCOMPARE(repeat->currentIndex(),savedRepeat); QCOMPARE(project->currentIndex(),savedProject); }
        setBody("saved while leaving");
        QVERIFY(answerLeave(QMessageBox::Save,[&]{nav.at(1)->click();})); QVERIFY(!draft->dirty()); nav.at(module)->click();
        QCOMPARE(workspace->currentKey(),key); QCOMPARE(body(),QString("saved while leaving"));
        setBody("detached draft"); button(workspace,QStringLiteral("独立打开"))->click();
        auto *host=window.findChild<QDialog *>("contentWindow"); QVERIFY(host && host->isVisible());
        QVERIFY(answerLeave(QMessageBox::Cancel,[&]{host->close();})); QCOMPARE(body(),QString("detached draft"));
        saveEditor(editor); QVERIFY(!draft->dirty()); QVERIFY(editor->isVisible());
        if(text) QCOMPARE(text->findChild<QTextEdit *>("headingPreview")->font().pointSizeF(),20.0);
        setBody("discard on detached close");
        QVERIFY(answerLeave(QMessageBox::Discard,[&]{host->close();})); QVERIFY(!host->isVisible());
        QVERIFY(workspace->closeAll());
    }
    void attachingProtectsOtherModuleDraft() {
        MainWindow window; prepareWindow(window); const auto nav=window.findChildren<QPushButton *>("navButton");
        auto *calendar=window.findChild<CalendarPage *>(); auto *cw=ContentWorkspace::find(calendar);
        button(calendar,QStringLiteral("添加日记"))->click();
        auto *diary=cw->content(cw->currentKey()); diary->findChild<QLineEdit *>("diaryTitle")->setText("diary draft");
        diary->findChild<HeadingTextEdit *>()->setPlainText("diary body");
        button(cw,QStringLiteral("独立打开"))->click(); auto *host=window.findChild<QDialog *>("contentWindow"); QVERIFY(host);
        nav.at(3)->click(); auto *clipboard=window.findChild<ClipboardPage *>(); auto *bw=ContentWorkspace::find(clipboard);
        clipboard->findChild<QPushButton *>("clipboardNewItem")->click();
        auto *clip=bw->content(bw->currentKey()); clip->findChild<QPlainTextEdit *>("clipboardItemBody")->setPlainText("clipboard draft");
        QVERIFY(answerLeave(QMessageBox::Cancel,[&]{button(host,QStringLiteral("收回主窗口"))->click();}));
        QVERIFY(host->isVisible()); QVERIFY(clip->isVisible()); QVERIFY(nav.at(3)->isChecked());
        QCOMPARE(clip->findChild<QPlainTextEdit *>("clipboardItemBody")->toPlainText(),QString("clipboard draft"));
        QVERIFY(answerLeave(QMessageBox::Discard,[&]{button(host,QStringLiteral("收回主窗口"))->click();}));
        QVERIFY(!host->isVisible()); QVERIFY(diary->isVisible()); QVERIFY(nav.at(0)->isChecked());
        QVERIFY(dynamic_cast<DraftDialog *>(diary)->dirty()); QVERIFY(!dynamic_cast<DraftDialog *>(clip)->dirty());
        saveEditor(diary); QVERIFY(cw->closeAll()); QVERIFY(bw->closeAll());
    }
    void repeatedContentLifecycle() {
        MainWindow window; prepareWindow(window);
        const auto nav=window.findChildren<QPushButton*>("navButton");
        auto *calendar=window.findChild<CalendarPage*>(); auto *cw=ContentWorkspace::find(calendar);
        auto *projects=window.findChild<ProjectsPage*>(); auto *pw=ContentWorkspace::find(projects);
        auto *clipboard=window.findChild<ClipboardPage*>(); auto *bw=ContentWorkspace::find(clipboard);
        qsizetype warmedWidgets=-1;
        const int cycles = TestSupport::repetitions(8, 40);
        for (int cycle=0;cycle<cycles;++cycle) {
            nav.at(0)->click(); button(calendar,QStringLiteral("日记集合"))->click();
            auto *collection=cw->content("diary/collection"); auto *list=collection->findChild<QListWidget*>("diaryCollectionList");
            list->setCurrentRow(cycle%list->count()); list->itemDoubleClicked(list->currentItem());
            QPointer<QDialog> diary=cw->content(cw->currentKey()); QVERIFY(diary && diary!=collection);
            cw->back(); button(cw,QStringLiteral("独立打开"))->click();
            auto *host=window.findChild<QDialog*>("contentWindow"); QVERIFY(host);
            list->itemDoubleClicked(list->currentItem());
            if (cycle%2) { button(host,QStringLiteral("收回主窗口"))->click(); QVERIFY(cw->closeAll()); }
            else { host->close(); }
            nav.at(1)->click(); auto *tree=projects->findChild<QTreeWidget*>("workRecordList");
            tree->setCurrentItem(tree->topLevelItem(0)); tree->itemDoubleClicked(tree->currentItem(),0);
            QVERIFY(pw->content(pw->currentKey()));
            button(pw,QStringLiteral("独立打开"))->click();
            host=window.findChild<QDialog*>("contentWindow");
            // Find only the live host; a previously closed one may await deferred deletion.
            for (auto *candidate:window.findChildren<QDialog*>("contentWindow")) if(candidate->isVisible())host=candidate;
            QVERIFY(host && host->isVisible()); host->close();
            QTRY_VERIFY(projects->findChild<QPushButton*>("gitHistoryButton")->isEnabled());
            projects->findChild<QPushButton*>("gitHistoryButton")->click();
            auto *history=qobject_cast<GitHistoryWindow*>(pw->content(pw->currentKey())); QVERIFY(history);
            if (cycle%4==0) {
                auto *table=history->findChild<QTableView*>("gitCommits");
                QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount()>0,15000);
                table->setCurrentIndex(table->model()->index(0,1)); table->doubleClicked(table->currentIndex());
                QVERIFY(pw->content(pw->currentKey())->findChild<QListWidget*>("gitFiles"));
            }
            QVERIFY(pw->closeAll()); QVERIFY(pw->closeAll());
            nav.at(3)->click(); clipboard->findChild<QPushButton*>("clipboardNewItem")->click();
            QPointer<QDialog> draft=bw->content(bw->currentKey()); QVERIFY(draft);
            button(bw,QStringLiteral("独立打开"))->click();
            for(auto *candidate:window.findChildren<QDialog*>("contentWindow")) if(candidate->isVisible())host=candidate;
            button(host,QStringLiteral("收回主窗口"))->click();
            draft->setFocus(); QTest::keyClick(draft,Qt::Key_Escape);
            QVERIFY(bw->currentKey().isEmpty());
            window.findChild<QAbstractButton *>("closeToTrayEnabled")->setChecked(true);
            auto *tray=window.findChild<QSystemTrayIcon*>();
            if(tray && tray->isVisible())QVERIFY(!window.close()); else window.hide();
            QVERIFY(!window.isVisible()); window.showMainWindow();
            window.showMinimized(); window.showMainWindow(); QVERIFY(!window.isMinimized());
            QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete); QTest::qWait(25);
            QVERIFY(!diary); QVERIFY(!draft);
            QCOMPARE(window.findChildren<QWidget*>("contentFrame").size(),0);
            QCOMPARE(window.findChildren<QDialog*>("contentWindow").size(),0);
            const auto count=QApplication::allWidgets().size();
            if(cycle==3)warmedWidgets=count;
            if(cycle>3)QVERIFY2(count<=warmedWidgets+2,"Content widgets accumulated between completed cycles");
        }
        window.findChild<QAbstractButton *>("closeToTrayEnabled")->setChecked(false); QVERIFY(window.close());
        qInfo()<<cycles<<"content cycles, detach/attach/host-close/Escape, Git cancellation and tray restore; no retained content widgets";
    }
};
static int runResourceProbe(QApplication &app) {
    seed(18);
    if(app.arguments().contains("--probe-listener")){
        if(QApplication::platformName()!="offscreen")qFatal("listener probe must use isolated offscreen clipboard");
        AppDatabase db;QString error;if(!db.open(&error))qFatal("probe db");db.setSetting("clipboard.monitor","true");
    }
    MainWindow window; prepareWindow(window);
    window.setWindowTitle(QStringLiteral("Orchestrate v3.4.0 隔离资源测试"));
    auto mark=[&](const QString &phase) {
        QSaveFile file(app.applicationDirPath()+"/probe-state.json");
        if(!file.open(QIODevice::WriteOnly))qFatal("probe state unavailable");
        file.write(QJsonDocument(QJsonObject{{"phase",phase},{"pid",double(app.applicationPid())},
            {"visible",window.isVisible()},{"minimized",window.isMinimized()},
            {"content_frames",window.findChildren<QWidget*>("contentFrame").size()},
            {"widgets",QApplication::allWidgets().size()}}).toJson()); file.commit();
    };
    auto hide=[&] {
        window.findChild<QAbstractButton *>("closeToTrayEnabled")->setChecked(true);
        auto *tray=window.findChild<QSystemTrayIcon*>();
        if(tray && tray->isVisible())window.close(); else window.hide();
        if(window.isVisible())qFatal("probe did not hide");
    };
    QTimer::singleShot(5000,&window,[&]{hide();mark("baseline_hidden");});
    QTimer::singleShot(50000,&window,[&]{
        mark("opening_contents"); window.showMainWindow();
        auto nav=window.findChildren<QPushButton*>("navButton"); nav.at(0)->click();
        auto *calendar=window.findChild<CalendarPage*>();auto *cw=ContentWorkspace::find(calendar);
        button(calendar,QStringLiteral("日记集合"))->click();
        auto *collection=cw->content("diary/collection");auto *list=collection->findChild<QListWidget*>("diaryCollectionList");
        for(int i=0;i<list->count();++i){list->setCurrentRow(i);list->itemDoubleClicked(list->currentItem());cw->back();}
        nav.at(1)->click();auto *projects=window.findChild<ProjectsPage*>();auto *pw=ContentWorkspace::find(projects);
        auto *tree=projects->findChild<QTreeWidget*>("workRecordList");tree->setCurrentItem(tree->topLevelItem(0));tree->itemDoubleClicked(tree->currentItem(),0);pw->back();
        QTRY_VERIFY(projects->findChild<QPushButton*>("gitHistoryButton")->isEnabled());
        projects->findChild<QPushButton*>("gitHistoryButton")->click();
        QTimer::singleShot(1800,&window,[&,projects]{
            if(auto *history=window.findChild<GitHistoryWindow*>()){
                auto *table=history->findChild<QTableView*>("gitCommits");
                if(table->model()->rowCount()>0){table->setCurrentIndex(table->model()->index(0,1));table->doubleClicked(table->currentIndex());}
            }
        });
        QTimer::singleShot(7000,&window,[&]{
            window.findChildren<QPushButton*>("navButton").at(3)->click();
            window.findChild<ClipboardPage*>()->findChild<QPushButton*>("clipboardNewItem")->click();
            for(int i=0;i<20;++i){window.hide();window.showMainWindow();window.showMinimized();window.showMainWindow();}
            hide();mark("retained_hidden");
        });
    });
    QTimer::singleShot(180000,&window,[&]{
        mark("closing_contents");window.showMainWindow();
        for(auto *workspace:window.findChildren<ContentWorkspace*>())if(!workspace->closeAll())qFatal("unexpected dirty probe");
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);hide();
        QTimer::singleShot(3000,&window,[&]{mark("released_hidden");});
    });
    QTimer::singleShot(305000,&window,[&]{mark("complete");app.quit();});
    return app.exec();
}
int main(int argc,char **argv) {
    QApplication app(argc,argv); app.setApplicationName("WorkspaceNavigationTest"); app.setOrganizationName("OrchestrateTests");
    app.setApplicationDisplayName(QStringLiteral(ORCHESTRATE_APP_NAME));
    QStandardPaths::setTestModeEnabled(true); app.setStyle("Fusion");
    for (const auto &font:{"msyh.ttc","msyhbd.ttc","segoeui.ttf","consola.ttf"}) QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/")+font);
    UiTheme::install(app); QDir().mkpath(app.applicationDirPath()+"/artifacts");
    if (QFileInfo(app.applicationDirPath()).fileName()!="workspace-navigation-test") return 2;
    if (app.arguments().contains("--resource-probe")) return runResourceProbe(app);
    if (app.arguments().contains("--ui-preview")) {
        seed(18); MainWindow window; prepareWindow(window); window.setWindowTitle(QStringLiteral("Orchestrate v3.7.0 · 隔离验收")); return app.exec();
    }
    app.setQuitOnLastWindowClosed(false); WorkspaceNavigationTest test; return QTest::qExec(&test,argc,argv);
}
#include "test_workspace_navigation.moc"
