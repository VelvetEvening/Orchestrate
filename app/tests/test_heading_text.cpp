#include "mainwindow.h"
#include "test_support.h"
#include "platform/fileaccess.h"
#include "pages/collaborationrecorddialog.h"
#include "update/updatewidget.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "platform/globalshortcut.h"
#include <QCheckBox>
#include <QToolButton>
#include <QCloseEvent>
#include <QLabel>
#include <QSystemTrayIcon>
#include "pages/calendarpage.h"
#include "pages/projectspage.h"
#include "pages/automationpage.h"
#include "pages/clipboardpage.h"
#include "widgets/headingtext.h"
#include "widgets/headingtextedit.h"
#include "widgets/persistenttextzoom.h"
#include "widgets/workspacecalendar.h"
#include "widgets/siblingorderview.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QAccessible>
#include <QAction>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QCalendarWidget>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include "ui/contentworkspace.h"
#include <QDateTimeEdit>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QTreeWidget>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QStackedWidget>
#include <QSplitter>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QWheelEvent>
#include <QWindow>
#include <QScreen>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <QtMath>
#include <functional>
#include <memory>
#include <stdexcept>

namespace {
class CalendarCellProbe : public WorkspaceCalendar
{
public:
    QImage renderDate(QDate date)
    {
        QImage img(80,70,QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        QPainter painter(&img);
        paintCell(&painter,QRect(0,0,80,70),date);
        return img;
    }
};
int countColor(const QImage &img, const QColor &color)
{
    int count=0;
    for(int y=0;y<img.height();++y)
        for(int x=0;x<img.width();++x)
            if(img.pixelColor(x,y)==color) ++count;
    return count;
}
int countBlendedColor(const QImage &img, const QColor &ink, const QColor &background)
{
    const int base[]{background.red(), background.green(), background.blue()};
    const int delta[]{ink.red() - base[0], ink.green() - base[1], ink.blue() - base[2]};
    int channel = 0;
    for (int i = 1; i < 3; ++i)
        if (qAbs(delta[i]) > qAbs(delta[channel])) channel = i;
    if (delta[channel] == 0) return 0;
    int count = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor color = img.pixelColor(x, y);
            const int pixel[]{color.red(), color.green(), color.blue()};
            const qreal coverage = qreal(pixel[channel] - base[channel]) / delta[channel];
            if (coverage < 0.4 || coverage > 1.01) continue;
            bool matches = true;
            for (int i = 0; i < 3; ++i)
                matches = matches && qAbs(pixel[i] - qRound(base[i] + coverage * delta[i])) <= 2;
            if (matches) ++count;
        }
    }
    return count;
}
int countReminderMark(const QImage &img)
{
    // A thin antialiased ring need not contain any fully opaque ink pixels.
    return countBlendedColor(img, QColor(UiTheme::ReminderMarker), QColor(UiTheme::IndexSurface))
        + countBlendedColor(img, QColor(UiTheme::ReminderMarker), QColor(UiTheme::Selection));
}
void require(bool ok, const QString &message)
{
    if (!ok) throw std::runtime_error(message.toStdString());
}
QString fixture(const QString &path)
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(path);
}
QPushButton *button(QWidget &root, const QString &text)
{
    for (auto *candidate : root.findChildren<QPushButton *>()) {
        if (candidate->text() == text && candidate->isVisible()) return candidate;
    }
    for (auto *candidate : root.findChildren<QPushButton *>()) {
        if (candidate->text() == text) return candidate;
    }
    throw std::runtime_error((QStringLiteral("Missing button: ") + text).toStdString());
}
using TestSupport::capture;
void saveDialog(QDialog &dialog)
{
    if (!qobject_cast<CollaborationRecordDialog *>(&dialog) && !dynamic_cast<DraftDialog *>(&dialog)) {
        button(dialog, QStringLiteral("保存"))->click();
        return;
    }
    dialog.window()->activateWindow(); QTest::qWait(20);
    auto *focus = dialog.focusWidget();
    if (!focus || !focus->isVisible()) focus = &dialog;
    focus->setFocus();
    QTest::keyClick(focus, Qt::Key_S, Qt::ControlModifier);
}
void interact(const QString &title, const std::function<void(QDialog &)> &handler,
              const std::function<void()> &action)
{
    QTimer timer;
    bool handled = false;
    QString error;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) {
            for (auto *widget:QApplication::allWidgets()) {
                auto *candidate=qobject_cast<QDialog*>(widget);
                if (candidate && candidate->isVisible() && candidate->windowTitle()==title) { dialog=candidate; break; }
            }
        }
        if (!dialog) return;
        if (dialog->windowTitle() != title || handled) {
            error = QStringLiteral("Unexpected dialog: ") + dialog->windowTitle();
            dialog->reject();
            return;
        }
        handled = true;
        timer.stop(); // Allow the handler to exercise a nested confirmation dialog.
        try {
            handler(*dialog);
            if (auto *draft=dynamic_cast<DraftDialog *> (dialog); draft && dialog->isVisible()
                && (!draft->dirty || !draft->dirty())) dialog->reject();
            // Hosted editors keep saved content open; finish the legacy one-shot interaction.
            if (!dialog->isWindow() && dialog->isVisible() && dialog->property("contentKey").isValid()
                && dialog->objectName()!=QStringLiteral("collaborationRecordDialog")) dialog->QDialog::reject();
            // Saving a collaboration record keeps it open and editable.
            if (dialog->objectName() == QStringLiteral("collaborationRecordDialog") && dialog->isVisible()
                && dialog->findChild<QLabel *>("recordSaveStatus")->text() == QStringLiteral("已保存")) dialog->reject();
        }
        catch (const std::exception &failure) {
            error = QString::fromUtf8(failure.what());
            dialog->QDialog::reject();
        }
    });
    timer.start(20);
    action();
    if (!handled) handled = QTest::qWaitFor([&] { return handled; }, 3000);
    require(handled, QStringLiteral("Expected dialog was not opened"));
    require(error.isEmpty(), error);
}
HeadingTextEdit *dialogEditor(QDialog &dialog)
{
    auto *editor = dialog.findChild<HeadingTextEdit *>("recordBodyEditor");
    if (!editor) editor = dialog.findChild<HeadingTextEdit *>();
    require(editor != nullptr, QStringLiteral("Heading editor missing from dialog"));
    return editor;
}
void interactMenu(const std::function<void(QMenu &)> &handler, const std::function<void()> &action)
{
    QTimer timer;
    QString error;
    bool handled = false;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) return;
        timer.stop();
        handled = true;
        try { handler(*menu); }
        catch (const std::exception &failure) { error = QString::fromUtf8(failure.what()); }
        menu->close();
    });
    timer.start(20);
    action();
    if (!handled) handled = QTest::qWaitFor([&] { return handled; }, 3000);
    require(handled && error.isEmpty(), QStringLiteral("Context menu failed: ") + error);
}
QAction *menuAction(QMenu &menu, const QString &text)
{
    for (auto *action : menu.actions()) if (action->text() == text) return action;
    throw std::runtime_error((QStringLiteral("Missing action: ") + text).toStdString());
}
void openProjectSettings(QWidget &window, QListWidgetItem *target = nullptr)
{
    auto *list = window.findChild<QListWidget *>(QStringLiteral("projectList"));
    require(list && list->currentItem(), QStringLiteral("Project selection missing"));
    if (!target) target = list->currentItem();
    interactMenu([&](QMenu &menu) {
        require(menu.actions().last()->text() == QStringLiteral("项目设置"), QStringLiteral("Settings must be last"));
        menuAction(menu, QStringLiteral("项目设置"))->trigger();
    }, [&] { list->customContextMenuRequested(list->visualItemRect(target).center()); });
}
void enterEdit(HeadingTextEdit *editor)
{
    for (auto *parent=editor->parentWidget(); parent; parent=parent->parentWidget()) {
        if (dynamic_cast<DraftDialog *>(parent)) {
            require(!editor->isPreviewMode(),QStringLiteral("Editor must open directly editable"));
            require(!editor->findChild<QTabBar *>("headingModeTabs")->isVisible(),QStringLiteral("Mode tabs still visible"));
            return;
        }
    }
    CollaborationRecordDialog *record=nullptr;
    for (auto *parent=editor->parentWidget(); parent; parent=parent->parentWidget())
        if ((record=qobject_cast<CollaborationRecordDialog*>(parent))) break;
    if (record) {
        record->findChild<QTabBar *>("recordSectionTabs")->setCurrentIndex(1);
        require(!editor->isPreviewMode(), QStringLiteral("Record must open directly editable"));
        return;
    }
    require(editor->isPreviewMode(),QStringLiteral("Existing record did not open in preview"));
    auto *preview=editor->findChild<QTextEdit *>(QStringLiteral("headingPreview"));
    require(preview && preview->isReadOnly(),QStringLiteral("Preview is editable"));
    const QString original=editor->toPlainText();
    QTest::keyClicks(preview,"unwanted edit");
    require(editor->toPlainText()==original,QStringLiteral("Typing changed preview content"));
    auto *tabs=editor->findChild<QTabBar *>(QStringLiteral("headingModeTabs"));
    require(tabs && tabs->tabRect(0).height()>=36,QStringLiteral("Mode tabs too small"));
    QTest::mouseClick(tabs,Qt::LeftButton,Qt::NoModifier,tabs->tabRect(1).center());
    require(!editor->isPreviewMode(),QStringLiteral("Edit tab did not activate"));
}
void sendWheel(QWidget *viewport, int angleDelta, Qt::KeyboardModifiers modifiers = Qt::ControlModifier,
               int pixelDelta = 0)
{
    const QPoint position = viewport->rect().center();
    QWheelEvent event(position, viewport->mapToGlobal(position), QPoint(0, pixelDelta), QPoint(0, angleDelta),
                      Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
    QApplication::sendEvent(viewport, &event);
    QCoreApplication::processEvents();
}
const QString sample = QStringLiteral("# 今日进展\n完成基础验证。\n## 下一阶段\n继续整理资料。\n### 注意事项\n保留原文 <b>不是粗体</b> 与 **星号**。");
}

class DirectoryUrlRecorder final : public QObject
{
    Q_OBJECT
public:
    QList<QUrl> urls;
public slots:
    void record(const QUrl &url) { urls.append(url); }
};

class HeadingTextTest final : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        for (const auto &suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm")}) {
            const QString path = fixture(QStringLiteral("data/orchestrate.sqlite3") + suffix);
            QVERIFY(!QFileInfo::exists(path) || QFile::remove(path));
        }
        database_ = std::make_unique<AppDatabase>();
        QString error;
        QVERIFY2(database_->open(&error), qPrintable(error));
    }
    void cleanup() { database_.reset(); }

    void repeatedWorkspaceLifecycle()
    {
        QVERIFY(database_->setSetting("global_shortcut_enabled", "false"));
        QVERIFY(database_->setSetting("clipboard.monitor", "false"));
        qsizetype warmedWidgetCount = -1;
        const int lifetimes = TestSupport::repetitions(3, 12);
        const int cycles = TestSupport::repetitions(4, 20);
        for (int generation = 0; generation < lifetimes; ++generation) {
            auto window = std::make_unique<MainWindow>();
            QPointer<QWidget> guard(window.get());
            window->show();
            const auto navigation = window->findChildren<QPushButton *>("navButton");
            auto *toggle = window->findChild<QPushButton *>("sidebarToggle");
            auto *settings = window->findChild<QPushButton *>("clipboardSettings");
            auto *listening = window->findChild<QAction *>("clipboardListening");
            QVERIFY(navigation.size() == 5 && toggle && settings && listening);
            for (int cycle = 0; cycle < cycles; ++cycle) {
                for (auto *button : navigation) {
                    button->click();
                    QCoreApplication::processEvents();
                    QVERIFY(button->isChecked());
                }
                navigation.at(3)->click();
                settings->menu()->popup(settings->mapToGlobal(QPoint(0, 0)));
                QCoreApplication::processEvents();
                settings->menu()->hide();
                if (QApplication::platformName() == "offscreen") {
                    listening->trigger();
                    listening->trigger();
                    QVERIFY(!listening->isChecked());
                }
                toggle->click(); toggle->click();
                window->hide(); window->show();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                QCoreApplication::processEvents();
            }
            window.reset();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
            QVERIFY(guard.isNull());
            const auto remaining = QApplication::allWidgets().size();
            if (warmedWidgetCount < 0) warmedWidgetCount = remaining;
            QVERIFY2(remaining <= warmedWidgetCount + 2, "Widgets accumulated after destroying the workspace");
        }
        qInfo() << lifetimes << "window lifetimes," << lifetimes * cycles * 5
                << "page changes; no widget accumulation";
    }

    void nativeCaptionRepaint()
    {
#ifdef Q_OS_WIN
        if (QApplication::platformName() != "windows") QSKIP("Requires the Windows platform");
        QVERIFY(database_->setSetting("global_shortcut_enabled", "false"));
        MainWindow window;
        window.showMainWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(300);
        const auto hwnd = reinterpret_cast<HWND>(window.winId());
        auto checkCaption = [&](const QString &stage) {
            QCoreApplication::processEvents();
            const auto frame = window.screen()->grabWindow(window.winId()).toImage();
            frame.save(fixture("artifacts/native-caption-" + stage + ".png"));
            const qreal scale = frame.devicePixelRatio();
            int matching = 0, total = 0;
            for (int y = qRound(8*scale); y < qRound(24*scale); ++y) {
                for (int x = qRound(80*scale); x < qRound((window.width()-180)*scale); ++x) {
                    ++total;
                    const QColor expected(UiTheme::Sidebar);
                    if (frame.pixelColor(x,y).rgb() == expected.rgb()) ++matching;
                }
            }
            qInfo() << stage << "clean header pixels" << matching << "/" << total
                    << "native style" << Qt::hex << GetWindowLongPtrW(hwnd,GWL_STYLE)
                    << "Qt flags" << window.windowHandle()->flags();
            return total > 0 && matching * 100 >= total * 99;
        };
        QVERIFY(checkCaption("initial"));
        for (bool active : {false, true}) {
            SendMessageW(hwnd, WM_NCACTIVATE, active, 0);
            QVERIFY2(checkCaption(active ? "active" : "inactive"), "System caption overwrote the integrated header");
        }
        RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
        QVERIFY(checkCaption("redraw"));
        window.showMaximized(); QTest::qWait(200);
        QVERIFY(checkCaption("maximized"));
        window.showNormal(); QTest::qWait(200);
        QVERIFY(checkCaption("restored"));
        window.showMinimized(); QTest::qWait(100);
        window.showMainWindow(); QTest::qWait(200);
        QVERIFY(checkCaption("from-minimized"));
        window.close(); QTest::qWait(100);
        window.showMainWindow(); QTest::qWait(200);
        QVERIFY(checkCaption("from-tray"));
#endif
    }

    void primaryKeyboardFocus()
    {
        int project = 0;
        QVERIFY(database_->addProject(QStringLiteral("焦点验收"), QString(), &project));
        QVERIFY(database_->setSetting("projects.last_selected", QString::number(project)));
        QVERIFY(database_->setSetting("global_shortcut_enabled", "false"));
        MainWindow window;
        window.show();
        const auto navigation = window.findChildren<QPushButton *>("navButton");
        for (int page : {1, 3}) {
            navigation.at(page)->click();
            QCoreApplication::processEvents();
            auto *target = page == 1 ? button(*window.findChild<ProjectsPage *>(), QStringLiteral("添加记录"))
                : window.findChild<QPushButton *>("clipboardNewItem");
            QVERIFY(target && target->isEnabled() && target->isVisible());
            target->setFocus();
            QTest::keyClick(target, Qt::Key_Tab, Qt::ShiftModifier);
            QVERIFY(QApplication::focusWidget() != target);
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
            QCOMPARE(QApplication::focusWidget(), target);
            QCoreApplication::processEvents();
            auto frame = target->grab().toImage();
            QCOMPARE(frame.pixelColor(0, frame.height()/2), QColor(UiTheme::Accent));
            capture(window, page == 1 ? QStringLiteral("seamless-primary-focus-record") : QStringLiteral("seamless-primary-focus-new"));
            target->setEnabled(false);
            QCoreApplication::processEvents();
            frame = target->grab().toImage();
            QCOMPARE(frame.pixelColor(0, frame.height()/2), QColor(UiTheme::Border));
            target->setEnabled(true);
        }
    }

    void unifiedHeaderNavigation()
    {
        QVERIFY(database_->setSetting("global_shortcut_enabled", "false"));
        MainWindow window;
        window.show();
        auto *toggle = window.findChild<QPushButton *>("sidebarToggle");
        auto *header = window.findChild<QWidget *>("windowHeader");
        auto *sidebar = window.findChild<QWidget *>("sidebar");
        const auto navigation = window.findChildren<QPushButton *>("navButton");
        QVERIFY(toggle && header && sidebar);
        QCOMPARE(navigation.size(), 5);
        QVERIFY(window.findChildren<QLabel *>("pageTitle").isEmpty());
        QVERIFY(header->findChildren<QLabel *>().isEmpty());
        QCOMPARE(header->findChildren<QPushButton *>().size(), 1);
        for (const QSize size : {QSize(1180,760), QSize(980,640)}) {
            window.resize(size);
            QCoreApplication::processEvents();
            const QPoint togglePosition = toggle->mapTo(&window, QPoint());
            for (int collapsed = 0; collapsed < 2; ++collapsed) {
                // Cover every page once. Projects and Settings exercise the layouts
                // with and without an index pane at the other widths and sidebar states.
                const QList<int> pages = size.width() == 1180 && !collapsed
                    ? QList<int>{0, 1, 2, 3, 4} : QList<int>{1, 4};
                for (int i : pages) {
                    QTest::mouseClick(navigation.at(i), Qt::LeftButton);
                    QCoreApplication::processEvents();
                    QVERIFY(navigation.at(i)->isChecked());
                    QCOMPARE(toggle->mapTo(&window, QPoint()), togglePosition);
                    QCOMPARE(sidebar->width(), collapsed ? 72 : 168);
                    QCOMPARE(navigation.first()->mapTo(sidebar, QPoint()).y(), 16);
                    QVERIFY(!toggle->icon().isNull());
                    auto *stack = window.findChild<QStackedWidget *>("pageStack");
                    auto *indexPane = stack->currentWidget()->findChild<QWidget *>("workspaceIndex");
                    QCOMPARE(sidebar->mapTo(&window, QPoint()).y(), header->mapTo(&window, QPoint()).y() + header->height());
                    QCOMPARE(stack->mapTo(&window, QPoint()).y(), sidebar->mapTo(&window, QPoint()).y());
                    QVERIFY(header->rect().contains(toggle->geometry()));
                    const auto headerImage = header->grab().toImage();
                    const qreal scale = headerImage.devicePixelRatio();
                    const int sampleY = qRound(header->height() / 2.0 * scale);
                    QCOMPARE(headerImage.pixelColor(qRound((sidebar->width()-8)*scale), sampleY), QColor(UiTheme::Sidebar));
                    if (indexPane) {
                        const int paneX = indexPane->mapTo(&window, QPoint()).x() - header->mapTo(&window, QPoint()).x();
                        QCOMPARE(headerImage.pixelColor(qRound((paneX+8)*scale), sampleY), QColor(UiTheme::Sidebar));
                        QCOMPARE(headerImage.pixelColor(qRound((paneX+indexPane->width()+8)*scale), sampleY), QColor(UiTheme::Sidebar));
                    }
                    QCOMPARE(headerImage.pixelColor(qRound((header->width()-8)*scale), sampleY), QColor(UiTheme::Sidebar));
                    capture(window, QStringLiteral("header-%1-%2-%3")
                        .arg(i).arg(size.width()).arg(collapsed ? "collapsed" : "expanded"));
                }
                QTest::mouseClick(toggle, Qt::LeftButton);
            }
        }
        QTest::mouseClick(navigation.at(1), Qt::LeftButton);
        auto *projectsSplit = window.findChild<ProjectsPage *>()->findChild<QSplitter *>("workspaceSplit");
        QVERIFY(projectsSplit);
        const auto originalSizes = projectsSplit->sizes();
        projectsSplit->setSizes({330, 400});
        QCoreApplication::processEvents();
        auto *projectPane = window.findChild<ProjectsPage *>()->findChild<QWidget *>("workspaceIndex");
        const auto splitHeader = header->grab().toImage();
        const qreal splitScale = splitHeader.devicePixelRatio();
        const int boundary = projectPane->mapTo(&window, QPoint()).x() - header->mapTo(&window, QPoint()).x() + projectPane->width();
        QCOMPARE(splitHeader.pixelColor(qRound((boundary-8)*splitScale), qRound(header->height()/2.0*splitScale)), QColor(UiTheme::Sidebar));
        QCOMPARE(splitHeader.pixelColor(qRound((boundary+8)*splitScale), qRound(header->height()/2.0*splitScale)), QColor(UiTheme::Sidebar));
        capture(window, QStringLiteral("seamless-project-splitter"));
        projectsSplit->setSizes(originalSizes);
        QTest::mouseClick(navigation.at(3), Qt::LeftButton);
        toggle->setFocus();
        QTest::keyClick(toggle, Qt::Key_Tab);
        QCOMPARE(QApplication::focusWidget(), navigation.at(3));
        QTest::mouseClick(navigation.first(), Qt::LeftButton);
        toggle->setFocus();
        QTest::keyClick(toggle, Qt::Key_Space);
        QCOMPARE(sidebar->width(), 72);
        QCOMPARE(toggle->accessibleName(), QStringLiteral("展开侧边栏"));
        QTest::keyClick(toggle, Qt::Key_Tab);
        QCOMPARE(QApplication::focusWidget(), navigation.first());
    }


    void lightRunOutputAndFailure()
    {
        QString error;
        AppDatabase::AutomationTool tool;
        tool.name = QStringLiteral("界面验收工具（合成数据）");
        tool.externalId = QStringLiteral("light-review");
        tool.targetType = QStringLiteral("windows-local");
        tool.registrationPath = fixture(QStringLiteral("light-review-tool.json"));
        tool.statePath = fixture(QStringLiteral("light-review-state.json"));
        tool.workingDirectory = QCoreApplication::applicationDirPath();
        QFile state(tool.statePath);
        QVERIFY(state.open(QIODevice::WriteOnly));
        state.write(QJsonDocument(QJsonObject{
            {QStringLiteral("schema"), QStringLiteral("orchestrate-state/v1")},
            {QStringLiteral("tool_id"), tool.externalId},
            {QStringLiteral("result"), QStringLiteral("success")},
            {QStringLiteral("summary"), QStringLiteral("只读验收工具已就绪，不修改系统设置。")},
            {QStringLiteral("updated_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
            {QStringLiteral("current_date"), QDate::currentDate().toString(Qt::ISODate)}
        }).toJson());
        state.close();
        AppDatabase::ToolCommand success, failure;
        const QString scriptPath = fixture(QStringLiteral("preview-output.ps1"));
        QFile script(scriptPath);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(QByteArray::fromHex("efbbbf"));
        script.write(QStringLiteral("[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)\n"
            "Write-Output '界面验收工具已就绪。'\n"
            "Write-Output '中文输出显示正常，任务已完成。'\n"
            "Write-Output '本次仅输出合成文本，没有修改系统设置。'\n").toUtf8());
        script.close();
        success.name = QStringLiteral("显示示例文本（只读）");
        success.executable = QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
        success.arguments = {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                             QStringLiteral("-File"), scriptPath};
        failure.name = QStringLiteral("启动失败反馈示例");
        failure.executable = fixture(QStringLiteral("missing-review-command.exe"));
        int id = 0;
        QVERIFY2(database_->saveAutomationTool(tool, {success, failure}, &id, &error), qPrintable(error));
        MainWindow window;
        window.show();
        button(window, QStringLiteral("工具"))->click();
        auto *page = window.findChild<AutomationPage *>();
        auto *tree = page->findChild<QTreeWidget *>(QStringLiteral("toolTree"));
        QVERIFY(tree && tree->topLevelItemCount());
        QTreeWidgetItem *target = nullptr;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            for (int j = 0; j < tree->topLevelItem(i)->childCount(); ++j)
                if (tree->topLevelItem(i)->child(j)->data(0, Qt::UserRole).toInt() == id)
                    target = tree->topLevelItem(i)->child(j);
        QVERIFY(target);
        tree->setCurrentItem(target->parent());
        auto *emptyMessage = page->findChild<QLabel *>("emptyStateMessage");
        QVERIFY(emptyMessage && emptyMessage->isVisible() && !emptyMessage->text().isEmpty());
        capture(window, QStringLiteral("v3.7.0-tools-group"));
        window.resize(980,640);
        capture(window, QStringLiteral("v3.7.0-tools-group-compact"));
        window.resize(1180,760);
        tree->setCurrentItem(target);
        auto *commands = page->findChild<QListWidget *>(QStringLiteral("commandList"));
        QCOMPARE(commands->count(), 2);
        QTRY_COMPARE(page->findChild<QLabel *>(QStringLiteral("stateSummary"))->text(),
                     QStringLiteral("只读验收工具已就绪，不修改系统设置。"));
        capture(window, QStringLiteral("automation-no-periodic-desktop"));
        window.resize(980,640);
        QCoreApplication::processEvents();
        capture(window, QStringLiteral("automation-no-periodic-compact"));
        window.resize(1180,760);
        commands->itemActivated(commands->item(0));
        QTRY_VERIFY(!page->hasRunningCommands());
        auto *output = page->findChild<QPlainTextEdit *>(QStringLiteral("runOutput"));
        auto *panel = page->findChild<QFrame *>(QStringLiteral("runPanel"));
        QVERIFY(output && panel && panel->isVisible());
        QVERIFY(output->toPlainText().contains(QStringLiteral("中文输出显示正常，任务已完成。")));
        QVERIFY(!output->toPlainText().contains(QChar::ReplacementCharacter));
        QVERIFY(countColor(panel->grab().toImage(), QColor(UiTheme::Background)) > 200);
        auto *detailScroll = page->findChild<QScrollArea *>(QStringLiteral("detailsScroll"));
        QTRY_COMPARE(page->findChild<QLabel *>(QStringLiteral("stateSummary"))->text(),
                     QStringLiteral("只读验收工具已就绪，不修改系统设置。"));
        detailScroll->ensureWidgetVisible(panel);
        capture(window, QStringLiteral("automation-light-output"));
        commands->itemActivated(commands->item(1));
        QTRY_VERIFY(!page->hasRunningCommands());
        QVERIFY(output->toPlainText().contains(QStringLiteral("missing-review-command")));
        window.resize(980, 640);
        QApplication::processEvents();
        QCOMPARE(detailScroll->widget()->width(), detailScroll->viewport()->width());
        QCOMPARE(detailScroll->horizontalScrollBar()->maximum(), 0);
        detailScroll->verticalScrollBar()->setValue(0);
        capture(window, QStringLiteral("automation-light-header-compact"));
        detailScroll->ensureWidgetVisible(panel);
        capture(window, QStringLiteral("automation-light-failure-compact"));
    }

    void siblingOrderStorage()
    {
        const auto ids = [](const auto &rows) { QList<int> result; for (const auto &row : rows) result << row.id; return result; };
        int p1=0,p2=0,p3=0,f1=0,f2=0;
        QVERIFY(database_->addProject("First", "", &p1));
        QVERIFY(database_->addProject("Second", "", &p2));
        const auto before = database_->projects();
        QVERIFY(database_->reorderProjects({p1,p2}));
        QCOMPARE(ids(database_->projects()), (QList<int>{p1,p2}));
        QString error;
        QVERIFY(!database_->reorderProjects({p1,p1}, &error)); QVERIFY(!error.isEmpty());
        QVERIFY(!database_->reorderProjects({p1}));
        QVERIFY(!database_->reorderProjects({p1,99999}));
        QCOMPARE(ids(database_->projects()), (QList<int>{p1,p2}));
        QCOMPARE(database_->projects().first().updatedAt, before.last().updatedAt);
        QVERIFY(database_->updateProject(p2,"Renamed",""));
        QCOMPARE(ids(database_->projects()), (QList<int>{p1,p2}));
        QVERIFY(database_->addProject("New", "", &p3));
        QCOMPARE(ids(database_->projects()), (QList<int>{p1,p2,p3}));
        QVERIFY(database_->addRecordFolder(p1,"Alpha",&f1));
        QVERIFY(database_->addRecordFolder(p1,"Beta",&f2));
        QVERIFY(database_->reorderRecordFolders(p1,false,{f2,f1}));
        QCOMPARE(ids(database_->recordFolders(p1)), (QList<int>{f2,f1}));
        QVERIFY(database_->setRecordFolderStarred(f1,true));
        QCOMPARE(ids(database_->recordFolders(p1)), (QList<int>{f1,f2}));
        QVERIFY(!database_->reorderRecordFolders(p1,false,{f2,f1}));
        AppDatabase::WorkRecord a,b,c;
        a.projectId=b.projectId=c.projectId=p1; a.folderId=b.folderId=f2;
        a.title="A"; b.title="B"; c.title="Root"; a.content=sample; a.summary="Keep summary";
        QVERIFY(database_->saveWorkRecord(&a)); QVERIFY(database_->saveWorkRecord(&b)); QVERIFY(database_->saveWorkRecord(&c));
        QVERIFY(database_->reorderWorkRecords(p1,f2,{a.id,b.id}));
        QVERIFY(!database_->reorderWorkRecords(p1,f2,{a.id,c.id}));
        QVERIFY(!database_->reorderWorkRecords(p2,f2,{a.id,b.id}));
        const auto rows=database_->workRecords(p1);
        auto kept=std::find_if(rows.begin(),rows.end(),[&](const auto &r){return r.id==a.id;});
        QCOMPARE(kept->content,a.content); QCOMPARE(kept->summary,a.summary); QCOMPARE(kept->updatedAt,a.updatedAt);
        {
            AppDatabase reopened; QVERIFY(reopened.open());
            QCOMPARE(ids(reopened.projects()), (QList<int>{p1,p2,p3}));
            QCOMPARE(ids(reopened.recordFolders(p1)), (QList<int>{f1,f2}));
        }
        QVERIFY(database_->deleteProject(p3));
        QCOMPARE(ids(database_->projects()), (QList<int>{p1,p2}));
    }

    void siblingOrderProjectUi()
    {
        int project=0,other=0,alpha=0,beta=0,star=0;
        QVERIFY(database_->addProject("排序项目", "", &project));
        QVERIFY(database_->addProject("另一项目", "", &other));
        QVERIFY(database_->addRecordFolder(project,"Alpha",&alpha));
        QVERIFY(database_->addRecordFolder(project,"Beta",&beta));
        QVERIFY(database_->addRecordFolder(project,"重点",&star));
        QVERIFY(database_->setRecordFolderStarred(star,true));
        for (const auto &title : {"A","B","C"}) QVERIFY(database_->addWorkRecord(project,title,sample,alpha));
        QVERIFY(database_->setSetting("projects.last_selected",QString::number(project)));
        MainWindow window; window.resize(1180,780); window.show();
        button(window,QStringLiteral("项目"))->click();
        auto *page=window.findChild<ProjectsPage *>();
        auto *tree=page->findChild<QTreeWidget *>("workRecordList");
        auto *list=page->findChild<QListWidget *>("projectList");
        auto *folder=tree->topLevelItem(1); folder->setExpanded(true);
        auto *first=folder->child(0); const int firstId=first->data(0,Qt::UserRole).toInt();
        tree->setCurrentItem(first);
        auto *accessible=QAccessible::queryAccessibleInterface(tree);
        for(int i=0;i<accessible->childCount();++i) accessible->child(i);
        QTest::keyClick(tree,Qt::Key_Down,Qt::ControlModifier);
        QCOMPARE(folder->child(1),first); QCOMPARE(tree->currentItem(),first);
        for(int i=0;i<accessible->childCount();++i) QCOMPARE(accessible->indexOfChild(accessible->child(i)),i);
        const auto ordered=database_->workRecords(project);
        QCOMPARE(ordered.at(1).id,firstId);
        auto *search=page->findChild<QLineEdit *>("recordSearch");
        search->setText("A");
        QTest::keyClick(tree,Qt::Key_Down,Qt::ControlModifier);
        QCOMPARE(folder->child(1),first);
        search->clear();
        tree->setCurrentItem(folder);
        QTest::keyClick(tree,Qt::Key_Up,Qt::ControlModifier); // Cannot cross the starred boundary.
        QCOMPARE(tree->topLevelItem(1),folder);
        QTest::keyClick(tree,Qt::Key_Down,Qt::ControlModifier);
        QCOMPARE(tree->topLevelItem(2),folder); QVERIFY(folder->isExpanded());
        QCOMPARE(database_->recordFolders(project).at(2).id,alpha);
        auto *selected=list->currentItem(); const int selectedId=selected->data(Qt::UserRole).toInt();
        QTest::keyClick(list,Qt::Key_Up,Qt::ControlModifier);
        QCOMPARE(list->currentItem(),selected); QCOMPARE(list->row(selected),0);
        QCOMPARE(database_->projects().first().id,selectedId);
        capture(window,"sibling-order-projects");
        window.resize(980,640); capture(window,"sibling-order-projects-compact");
        {
            AppDatabase reopened; QVERIFY(reopened.open()); ProjectsPage restored(&reopened);
            auto *restoredTree=restored.findChild<QTreeWidget *>("workRecordList");
            QCOMPARE(restoredTree->topLevelItem(2)->data(0,Qt::UserRole).toInt(),alpha);
            QCOMPARE(restoredTree->topLevelItem(2)->child(1)->data(0,Qt::UserRole).toInt(),firstId);
        }
        // Failure must leave both persistent order and visible selection unchanged.
        const QString connection="ordering-write-failure";
        {
            auto sql=QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(database_->databasePath()); QVERIFY(sql.open());
            QSqlQuery query(sql);
            QVERIFY(query.exec("CREATE TRIGGER fail_order BEFORE INSERT ON app_settings WHEN NEW.key LIKE 'order.%' BEGIN SELECT RAISE(ABORT,'ordering blocked'); END"));
            tree->setCurrentItem(first);
            interact(QStringLiteral("数据操作失败"),[&](QDialog &dialog){
                auto *message=qobject_cast<QMessageBox *>(&dialog); require(message && message->text().contains("ordering blocked"),"Failure hidden");
                message->button(QMessageBox::Ok)->click();
            },[&]{QTest::keyClick(tree,Qt::Key_Down,Qt::ControlModifier);});
            QCOMPARE(folder->child(1),first); QCOMPARE(tree->currentItem(),first);
            QCOMPARE(database_->workRecords(project).at(1).id,firstId);
            QVERIFY(query.exec("DROP TRIGGER fail_order"));
        }
        QSqlDatabase::removeDatabase(connection);
    }

    void siblingOrderToolsUi()
    {
        int group=0, other=0;
        QVERIFY(database_->addToolGroup("Tools A",&group)); QVERIFY(database_->addToolGroup("Tools B",&other));
        QList<int> toolIds;
        for(const QString &name : {QStringLiteral("Alpha"),QStringLiteral("Beta"),QStringLiteral("Gamma")}) {
            AppDatabase::AutomationTool tool; tool.name=name; tool.externalId=name; tool.groupId=group;
            tool.targetType="windows-local"; tool.registrationPath=fixture(name+".json"); tool.statePath=fixture(name+"-state.json");
            int id=0; QVERIFY(database_->addAutomationTool(tool,&id)); toolIds<<id;
        }
        AutomationPage page(database_.get()); page.resize(1000,700); page.show();
        auto *tree=page.findChild<QTreeWidget *>("toolTree");
        QTreeWidgetItem *folder=nullptr;
        for(int i=0;i<tree->topLevelItemCount();++i) if(tree->topLevelItem(i)->data(0,Qt::UserRole).toInt()==group) folder=tree->topLevelItem(i);
        QVERIFY(folder); folder->setExpanded(true);
        auto *first=folder->child(0); tree->setCurrentItem(first);
        QTest::keyClick(tree,Qt::Key_Down,Qt::ControlModifier);
        QCOMPARE(folder->child(1),first); QCOMPARE(tree->currentItem(),first);
        QCOMPARE(database_->automationTools(group).at(1).id,toolIds.first());
        auto tool=database_->automationTools(group).at(1); tool.name="Renamed";
        QVERIFY(database_->updateAutomationTool(tool));
        QCOMPARE(database_->automationTools(group).at(1).id,toolIds.first());
        QVERIFY(!database_->reorderTools(other,toolIds));
        QVERIFY(database_->setToolGroup(toolIds.last(),other));
        QVERIFY(!database_->reorderTools(group,{toolIds[1],toolIds[0],toolIds[2]})); // Stale list.
        const auto saved=database_->automationTools(group);
        QCOMPARE(saved.size(),2); QCOMPARE(saved.first().id,toolIds[1]);
        {
            AppDatabase reopened; QVERIFY(reopened.open());
            QCOMPARE(reopened.automationTools(group).first().id,toolIds[1]);
            QCOMPARE(reopened.automationTools(other).first().id,toolIds[2]);
        }
        tree->setCurrentItem(folder); QTest::keyClick(tree,Qt::Key_Down,Qt::ControlModifier);
        QVERIFY(folder->isExpanded()); QCOMPARE(database_->toolGroups().last().id,group);
        capture(page,"sibling-order-tools");
        auto *orderedTree=static_cast<SiblingOrderTree *>(tree);
        auto groupItem=[&](int id) -> QTreeWidgetItem * {
            for(int i=0;i<tree->topLevelItemCount();++i)
                if(tree->topLevelItem(i)->data(0,Qt::UserRole).toInt()==id) return tree->topLevelItem(i);
            return nullptr;
        };
        tree->setCurrentItem(first);
        auto *destination=groupItem(other); QVERIFY(destination); destination->setExpanded(false);
        QVERIFY(orderedTree->dropSelectionAt(tree->visualItemRect(destination).center()));
        QCOMPARE(tree->currentItem()->data(0,Qt::UserRole).toInt(),toolIds.first());
        QCOMPARE(tree->currentItem()->parent(),groupItem(other)); QVERIFY(groupItem(other)->isExpanded());
        QCOMPARE(database_->automationTools(other).size(),2);
        QVERIFY(orderedTree->dropSelectionAt(tree->visualItemRect(groupItem(0)).center()));
        QCOMPARE(tree->currentItem()->parent(),groupItem(0));
        AppDatabase reopened; QVERIFY(reopened.open());
        bool ungrouped=false;
        for(const auto &entry:reopened.automationTools()) if(entry.id==toolIds.first()) ungrouped=entry.groupId==0;
        QVERIFY(ungrouped);
        tree->setCurrentItem(groupItem(group));
        QVERIFY(!orderedTree->canTransferSelectionTo(groupItem(other)));
    }

    void virtualToolGroupOrder()
    {
        int group=0,other=0;
        QVERIFY(database_->addToolGroup("A",&group));
        QVERIFY(database_->addToolGroup("B",&other));
        QVERIFY(database_->setSetting("automation.ungrouped_name",QStringLiteral("自定义未分组")));
        AppDatabase::AutomationTool tool; tool.name="Ungrouped tool"; tool.externalId="ungrouped-test";
        tool.targetType="windows-local"; tool.registrationPath=fixture("ungrouped.json"); tool.statePath=fixture("ungrouped-state.json");
        int toolId=0; QVERIFY(database_->addAutomationTool(tool,&toolId));
        AutomationPage page(database_.get()); page.resize(1000,700); page.show();
        auto *tree=page.findChild<QTreeWidget *>("toolTree");
        auto *ungrouped=tree->topLevelItem(tree->topLevelItemCount()-1);
        QCOMPARE(ungrouped->data(0,Qt::UserRole).toInt(),0);
        QCOMPARE(ungrouped->text(0),QStringLiteral("自定义未分组"));
        QCOMPARE(ungrouped->childCount(),1);
        ungrouped->setExpanded(true); tree->setCurrentItem(ungrouped);
        for(int i=1;i<tree->topLevelItemCount();++i) QTest::keyClick(tree,Qt::Key_Up,Qt::ControlModifier);
        QCOMPARE(tree->topLevelItem(0),ungrouped); QCOMPARE(tree->currentItem(),ungrouped);
        QVERIFY(ungrouped->isExpanded()); QCOMPARE(ungrouped->child(0)->data(0,Qt::UserRole).toInt(),toolId);
        auto saved=database_->toolGroups(nullptr,true);
        QCOMPARE(saved.first().id,0); QCOMPARE(saved.size(),tree->topLevelItemCount());
        QList<int> ids; for(const auto &g:saved) ids<<g.id;
        auto missing=ids; missing.removeAll(0); QVERIFY(!database_->reorderToolGroups(missing));
        auto duplicate=ids; duplicate<<0; QVERIFY(!database_->reorderToolGroups(duplicate));
        QVERIFY(!database_->reorderProjects({0}));
        QCOMPARE(database_->toolGroups().size(),ids.size()-1);
        auto *accessible=QAccessible::queryAccessibleInterface(tree);
        for(int i=0;i<accessible->childCount();++i) QCOMPARE(accessible->indexOfChild(accessible->child(i)),i);
        QTest::keyClick(tree,Qt::Key_Down,Qt::ControlModifier);
        QCOMPARE(tree->topLevelItem(1),ungrouped);
        QCOMPARE(database_->toolGroups(nullptr,true).at(1).id,0);
        {
            AppDatabase reopened; QVERIFY(reopened.open());
            AutomationPage restored(&reopened);
            auto *restoredTree=restored.findChild<QTreeWidget *>("toolTree");
            auto *restoredGroup=restoredTree->topLevelItem(1);
            QCOMPARE(restoredGroup->data(0,Qt::UserRole).toInt(),0);
            QCOMPARE(restoredGroup->text(0),QStringLiteral("自定义未分组"));
            QCOMPARE(restoredGroup->child(0)->data(0,Qt::UserRole).toInt(),toolId);
        }
        int newId=0; QVERIFY(database_->addToolGroup("New",&newId));
        QCOMPARE(database_->toolGroups(nullptr,true).last().id,newId);
        capture(page,"sibling-order-virtual-group");
    }

    void treeAccessibilityAfterFolding_data()
    {
        QTest::addColumn<bool>("automation");
        QTest::addColumn<bool>("active");
        QTest::newRow("tools-inactive") << true << false;
        QTest::newRow("tools-active") << true << true;
        QTest::newRow("records-inactive") << false << false;
        QTest::newRow("records-active") << false << true;
    }

    void treeAccessibilityAfterFolding()
    {
        QFETCH(bool, automation);
        QFETCH(bool, active);
        const bool previousActive = QAccessible::isActive();
        QAccessible::setActive(active);
        const auto restoreAccessibility = qScopeGuard([previousActive] { QAccessible::setActive(previousActive); });
        std::unique_ptr<QWidget> page;
        if (automation) page = std::make_unique<AutomationPage>(database_.get());
        else page = std::make_unique<ProjectsPage>(database_.get());
        auto *tree = page->findChild<QTreeWidget *>(automation ? "toolTree" : "workRecordList");
        QVERIFY(tree);
        tree->clear();
        for (const QString &name : {QStringLiteral("system"), QStringLiteral("queue"), QStringLiteral("ungrouped")}) {
            auto *group = new QTreeWidgetItem(tree, {name, "1"});
            new QTreeWidgetItem(group, {name + " tool", "ok"});
        }
        tree->expandAll();
        page->resize(1000, 700); page->show();
        QCoreApplication::processEvents();
        auto *accessible = QAccessible::queryAccessibleInterface(tree);
        QVERIFY(accessible);
        const auto verify = [tree, accessible] {
            QStringList expected;
            const auto collect = [tree, &expected](const auto &self, QTreeWidgetItem *item) -> void {
                if (item->isHidden()) return;
                for (int col = 0; col < tree->columnCount(); ++col) expected.append(item->text(col));
                if (item->isExpanded()) for (int j = 0; j < item->childCount(); ++j) self(self, item->child(j));
            };
            for (int i = 0; i < tree->topLevelItemCount(); ++i) collect(collect, tree->topLevelItem(i));
            QStringList actual;
            QSet<QAccessible::Id> ids;
            for (int i = 0; i < accessible->childCount(); ++i) {
                auto *child = accessible->child(i);
                QVERIFY(child);
                QCOMPARE(accessible->indexOfChild(child), i);
                QVERIFY(!ids.contains(QAccessible::uniqueId(child)));
                ids.insert(QAccessible::uniqueId(child));
                if (child->role() == QAccessible::TreeItem) actual.append(child->text(QAccessible::Name));
            }
            QCOMPARE(actual, expected);
        };
        verify(); // Prime every accessible cell before changing visible rows.
        connect(tree, &QTreeWidget::itemCollapsed, tree, verify);
        connect(tree, &QTreeWidget::itemExpanded, tree, verify);
        const int expandedCount = accessible->childCount();
        for (int cycle = 0; cycle < 20; ++cycle) {
            for (int group : {1, 0, 2}) {
                tree->topLevelItem(group)->setExpanded(false);
                verify(); // Must already be correct before processing queued UIA requests.
                QCoreApplication::processEvents();
                verify();
            }
            QCOMPARE(accessible->childCount(), expandedCount - 3 * tree->columnCount());
            for (int group : {2, 0, 1}) {
                tree->topLevelItem(group)->setExpanded(true);
                verify();
            }
            QCOMPARE(accessible->childCount(), expandedCount);
        }
    }

    void treeAccessibilityAfterSearch()
    {
        int project = 0;
        QVERIFY(database_->addProject("Accessibility", "", &project));
        for (const QString &name : {QStringLiteral("Alpha"), QStringLiteral("Beta")}) {
            int folder = 0;
            QVERIFY(database_->addRecordFolder(project, name, &folder));
            AppDatabase::WorkRecord record;
            record.projectId = project; record.folderId = folder;
            record.title = name + " record"; record.content = "body";
            QVERIFY(database_->saveWorkRecord(&record));
        }
        ProjectsPage page(database_.get()); page.resize(1000, 700); page.show();
        QCoreApplication::processEvents();
        auto *tree = page.findChild<QTreeWidget *>("workRecordList");
        auto *search = page.findChild<QLineEdit *>("recordSearch");
        auto *accessible = QAccessible::queryAccessibleInterface(tree);
        QVERIFY(accessible);
        for (const QString &query : {QStringLiteral("Alpha"), QStringLiteral("Beta"), QStringLiteral(""), QStringLiteral("missing")}) {
            for (int i = 0; i < accessible->childCount(); ++i) accessible->child(i);
            search->setText(query);
            for (int i = 0; i < accessible->childCount(); ++i) {
                auto *child = accessible->child(i);
                QVERIFY(child);
                QCOMPARE(accessible->indexOfChild(child), i);
                if (child->role() == QAccessible::TreeItem && !query.isEmpty())
                    QVERIFY(child->text(QAccessible::Name).contains(query));
            }
        }
    }

    void collaborationDirectEditingAndSelectionCopy()
    {
        int project = 0;
        QVERIFY(database_->addProject(QStringLiteral("提示词资料库"), QString(), &project));
        AppDatabase::WorkRecord record;
        record.projectId = project;
        record.title = QStringLiteral("项目初始化与约束检查");
        record.summary = QStringLiteral("## 使用场景\n用于接手项目时梳理目标、目录和验证方式。\n\n## 关键要求\n先阅读现有约定，保留用户的改动。\n确认运行入口，列出完成标准。\n");
        for (int i = 0; i < 45; ++i) record.summary += QStringLiteral("要点 %1：记录需要保留的约束。\n").arg(i + 1);
        record.content = QStringLiteral("## 完整提示词\r\n  原始缩进与 CRLF\r\n");
        for (int i = 0; i < 500; ++i) record.content += QStringLiteral("第 %1 行：保持原文 $() <html> **文本**。\r\n").arg(i + 1);
        QVERIFY(database_->saveWorkRecord(&record));
        MainWindow window;
        CollaborationRecordDialog dialog(database_.get(), record, &window);
        dialog.show(); QTest::qWait(20);
        auto *tabs = dialog.findChild<QTabBar *>("recordSectionTabs");
        auto *summary = dialog.findChild<HeadingTextEdit *>("recordSummaryEditor");
        auto *body = dialog.findChild<HeadingTextEdit *>("recordBodyEditor");
        auto *title = dialog.findChild<QLineEdit *>("workRecordTitle");
        QCOMPARE(tabs->count(), 2); QCOMPARE(tabs->tabText(0), QStringLiteral("摘要")); QCOMPARE(tabs->currentIndex(), 0);
        QVERIFY(!summary->isPreviewMode()); QVERIFY(!title->isReadOnly());
        QVERIFY(!dialog.findChild<QPushButton *>("editRecord"));
        QVERIFY(!dialog.findChild<QPushButton *>("copyRecordBody"));
        QVERIFY(!dialog.findChild<QPushButton *>("cancelRecordEdit"));
        QVERIFY(!dialog.findChild<QPushButton *>("saveRecord"));
        for (auto *mode : dialog.findChildren<QTabBar *>("headingModeTabs")) QVERIFY(!mode->isVisible());
        auto *summaryPreview = summary->findChild<QTextEdit *>("headingPreview");
        auto *bodyPreview = body->findChild<QTextEdit *>("headingPreview");
        QVERIFY(bodyPreview->toPlainText().isEmpty());
        QCOMPARE(summaryPreview->document()->firstBlock().text(), QStringLiteral("使用场景"));
        QCOMPARE(summaryPreview->verticalScrollBar()->value(), 0);
        capture(dialog, QStringLiteral("collaboration-summary-reading"));
        summaryPreview->verticalScrollBar()->setValue(summaryPreview->verticalScrollBar()->maximum() / 2);
        const int summaryScroll = summaryPreview->verticalScrollBar()->value();
        tabs->setCurrentIndex(1); QCoreApplication::processEvents();
        QVERIFY(!body->isPreviewMode()); QVERIFY(bodyPreview->toPlainText().contains(QStringLiteral("第 500 行")));
        QCOMPARE(bodyPreview->document()->firstBlock().text(), QStringLiteral("完整提示词"));
        QCOMPARE(bodyPreview->verticalScrollBar()->value(), 0);
        bodyPreview->selectAll(); bodyPreview->copy();
        QCOMPARE(QApplication::clipboard()->text(), body->toPlainText());
        auto cursor = bodyPreview->textCursor(); cursor.clearSelection(); bodyPreview->setTextCursor(cursor);
        bodyPreview->verticalScrollBar()->setValue(bodyPreview->verticalScrollBar()->maximum() / 2);
        const int bodyScroll = bodyPreview->verticalScrollBar()->value();
        tabs->setCurrentIndex(0); QCOMPARE(summaryPreview->verticalScrollBar()->value(), summaryScroll);
        tabs->setCurrentIndex(1); QCOMPARE(bodyPreview->verticalScrollBar()->value(), bodyScroll);
        tabs->setCurrentIndex(0); summary->setFocus();
        QVERIFY(summary->findChild<QTextEdit *>("headingPreview")->hasFocus());
        QCOMPARE(summary->findChild<QPlainTextEdit *>("headingSource")->accessibleName(), QStringLiteral("摘要原文"));
        title->setText(QStringLiteral("更新后的项目初始化提示词"));
        QCOMPARE(dialog.findChild<QLabel *>("recordSaveStatus")->text(), QStringLiteral("未保存 · Ctrl+S"));
        auto *summarySource = summary->findChild<QPlainTextEdit *>("headingSource");
        summarySource->moveCursor(QTextCursor::End); summarySource->insertPlainText(QStringLiteral("\n新增摘要要点"));
        const auto newSummary = summary->toPlainText();
        tabs->setCurrentIndex(1); QVERIFY(!body->isPreviewMode());
        tabs->setCurrentIndex(0); QVERIFY(summarySource->document()->isUndoAvailable());
        QCOMPARE(summary->toPlainText(), newSummary);
        capture(dialog, QStringLiteral("collaboration-summary-editing"));
        saveDialog(dialog);
        QVERIFY(dialog.isVisible()); QVERIFY(dialog.hasSaved()); QVERIFY(!title->isReadOnly());
        auto stored = database_->workRecords(project).first();
        QCOMPARE(stored.content, record.content); // Summary-only edit preserves exact CRLF body.
        QCOMPARE(stored.summary, newSummary); QCOMPARE(stored.createdAt, record.createdAt);
        tabs->setCurrentIndex(1);
        auto *bodySource = body->findChild<QPlainTextEdit *>("headingSource");
        bodySource->moveCursor(QTextCursor::End); bodySource->insertPlainText(QStringLiteral("追加的正文"));
        const auto newBody = body->toPlainText();
        tabs->setCurrentIndex(0); tabs->setCurrentIndex(1);
        QCOMPARE(body->toPlainText(), newBody); QVERIFY(bodySource->document()->isUndoAvailable());
        saveDialog(dialog);
        QCOMPARE(database_->workRecords(project).first().content, newBody);
        bodyPreview->verticalScrollBar()->setValue(bodyPreview->verticalScrollBar()->maximum() / 2);
        const int savedBodyScroll = bodyPreview->verticalScrollBar()->value();
        title->setText(QStringLiteral("只取消标题"));
        interact(QStringLiteral("未保存的修改"), [&](QDialog &prompt) { button(prompt, QStringLiteral("放弃修改"))->click(); }, [&] { QVERIFY(dialog.confirmLeave()); });
        QCOMPARE(bodyPreview->verticalScrollBar()->value(), savedBodyScroll);
        QVERIFY(bodySource->document()->isUndoAvailable());
        bodySource->insertPlainText(QStringLiteral("不保存的修改")); title->setText(QStringLiteral("不保存标题"));
        tabs->setCurrentIndex(0); summarySource->insertPlainText(QStringLiteral("不保存摘要"));
        interact(QStringLiteral("未保存的修改"), [&](QDialog &prompt) { button(prompt, QStringLiteral("放弃修改"))->click(); }, [&] { QVERIFY(dialog.confirmLeave()); });
        QCOMPARE(body->toPlainText(), newBody); QCOMPARE(summary->toPlainText(), newSummary);
        QCOMPARE(title->text(), stored.title);
        dialog.resize(560, 400); capture(dialog, QStringLiteral("collaboration-summary-compact"));
        dialog.close(); QVERIFY(!dialog.isVisible());
        CollaborationRecordDialog reopened(database_.get(), database_->workRecords(project).first());
        QCOMPARE(reopened.findChild<QTabBar *>("recordSectionTabs")->currentIndex(), 0);
    }

    void collaborationSummaryEditingPreservesIdentity()
    {
        int project = 0;
        QVERIFY(database_->addProject(QStringLiteral("新建流程"), QString(), &project));
        AppDatabase::WorkRecord record; record.projectId = project;
        // Title-only creation/validation and repeated saves belong to the
        // navigation suite. This case covers editing from the summary tab.
        record.title = QStringLiteral("摘要编辑");
        QVERIFY(database_->saveWorkRecord(&record));
        MainWindow window;
        CollaborationRecordDialog dialog(database_.get(), record, &window);
        dialog.show(); QTest::qWait(20);
        auto *tabs = dialog.findChild<QTabBar *>("recordSectionTabs");
        QCOMPARE(tabs->currentIndex(), 0);
        const int titleOnlyId = dialog.record().id;
        tabs->setCurrentIndex(1); dialogEditor(dialog)->setPlainText(sample);
        saveDialog(dialog);
        QCOMPARE(dialog.record().id, titleOnlyId);
        QVERIFY(dialog.isVisible()); QVERIFY(dialog.record().id > 0);
        const int id = dialog.record().id;
        QCOMPARE(database_->workRecords(project).size(), 1);
        tabs->setCurrentIndex(0); QVERIFY(!dialog.findChild<QWidget *>("emptyRecordSummary"));
        capture(dialog, QStringLiteral("collaboration-empty-summary"));
        auto *summary = dialog.findChild<HeadingTextEdit *>("recordSummaryEditor");
        QVERIFY(!summary->isPreviewMode());
        summary->setPlainText(QStringLiteral("手写摘要：用于建立工作区。"));
        auto *source = summary->findChild<QTextEdit *>("headingPreview");
        source->setFocus(); QTest::keyClick(source, Qt::Key_S, Qt::ControlModifier);
        QVERIFY(!summary->isPreviewMode()); QCOMPARE(dialog.findChild<QLabel *>("recordSaveStatus")->text(), QStringLiteral("已保存"));
        QCOMPARE(dialog.record().id, id); QCOMPARE(database_->workRecords(project).size(), 1);
        QCOMPARE(database_->workRecords(project).first().summary, summary->toPlainText());
        dialog.close();
    }

    void collaborationFontDefaultsAndZoomPersistence()
    {
        int project = 0; QVERIFY(database_->addProject("Font sizes", "", &project));
        QVERIFY(database_->setSetting("text_zoom.work_record", "22"));
        QVERIFY(database_->setSetting("text_zoom.work_record_summary", "22"));
        AppDatabase::WorkRecord record; record.projectId = project;
        record.title = QStringLiteral("内存挑战");
        record.summary = QStringLiteral("摘要可以直接编辑。\n\n记录关键要点、适用场景和重要约束。");
        record.content = QStringLiteral("正文可以直接编辑，Ctrl+S 保存。\n\n标题和切换按钮位于同一行。\n\n支持 Markdown、公式和表格。");
        QVERIFY(database_->saveWorkRecord(&record));
        MainWindow window;
        CollaborationRecordDialog dialog(database_.get(), record, &window); dialog.show(); QTest::qWait(20);
        auto *summary = dialog.findChild<HeadingTextEdit *>("recordSummaryEditor");
        auto *body = dialog.findChild<HeadingTextEdit *>("recordBodyEditor");
        QCOMPARE(summary->findChild<QTextEdit *>("headingPreview")->font().pointSizeF(), 24.0);
        QCOMPARE(body->findChild<QPlainTextEdit *>("headingSource")->font().pointSizeF(), 20.0);
        capture(dialog, "v3.5.5-summary-desktop");
        dialog.findChild<QTabBar *>("recordSectionTabs")->setCurrentIndex(1);
        QCoreApplication::processEvents(); capture(dialog, "v3.5.5-body-desktop");
        dialog.resize(560,400); capture(dialog, "v3.5.5-body-compact");
        dialog.findChild<QTabBar *>("recordSectionTabs")->setCurrentIndex(0);
        capture(dialog, "v3.5.5-summary-compact");
        summary->setTextPointSize(25); body->setTextPointSize(21);
        dialog.close();
        CollaborationRecordDialog reopened(database_.get(), record);
        QCOMPARE(reopened.findChild<HeadingTextEdit *>("recordSummaryEditor")->findChild<QPlainTextEdit *>("headingSource")->font().pointSizeF(), 25.0);
        QCOMPARE(reopened.findChild<HeadingTextEdit *>("recordBodyEditor")->findChild<QPlainTextEdit *>("headingSource")->font().pointSizeF(), 21.0);
        QCOMPARE(database_->setting("text_zoom.work_record"), QStringLiteral("22"));
        QCOMPARE(database_->setting("text_zoom.work_record_summary"), QStringLiteral("22"));
    }

    void collaborationCloseProtectionAndAtomicSaveFailure()
    {
        int project = 0;
        QVERIFY(database_->addProject(QStringLiteral("关闭与保存"), QString(), &project));
        AppDatabase::WorkRecord record; record.projectId = project;
        record.title = QStringLiteral("原始标题"); record.summary = QStringLiteral("原始摘要"); record.content = sample;
        QVERIFY(database_->saveWorkRecord(&record));
        CollaborationRecordDialog dialog(database_.get(), record);
        dialog.show(); QTest::qWait(20);
        auto *summary = dialog.findChild<HeadingTextEdit *>("recordSummaryEditor");
        summary->setPlainText(QStringLiteral("待保存摘要"));
        interact(QStringLiteral("未保存的修改"), [&](QDialog &prompt) { button(prompt, QStringLiteral("继续编辑"))->click(); }, [&] { dialog.close(); });
        QVERIFY(dialog.isVisible()); QCOMPARE(summary->toPlainText(), QStringLiteral("待保存摘要"));
        const QString connection = QStringLiteral("collaboration-save-failure");
        {
            auto sql = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            sql.setDatabaseName(database_->databasePath()); QVERIFY(sql.open());
            QSqlQuery query(sql);
            QVERIFY(query.exec(QStringLiteral("CREATE TRIGGER reject_record_save BEFORE UPDATE ON project_work_records BEGIN SELECT RAISE(ABORT,'injected failure'); END")));
            saveDialog(dialog);
            QVERIFY(dialog.findChild<QLabel *>("recordSaveStatus")->text().contains(QStringLiteral("保存失败")));
            QVERIFY(dialog.findChild<QLabel *>("recordSaveStatus")->toolTip().contains(QStringLiteral("injected failure")));
            QCOMPARE(database_->workRecords(project).first().summary, record.summary);
            QCOMPARE(database_->workRecords(project).first().updatedAt, record.updatedAt);
            interact(QStringLiteral("未保存的修改"), [&](QDialog &prompt) { button(prompt, QStringLiteral("保存"))->click(); }, [&] { dialog.close(); });
            QVERIFY(dialog.isVisible());
            QVERIFY(query.exec(QStringLiteral("DROP TRIGGER reject_record_save")));
        }
        QSqlDatabase::removeDatabase(connection);
        interact(QStringLiteral("未保存的修改"), [&](QDialog &prompt) { button(prompt, QStringLiteral("放弃修改"))->click(); }, [&] { dialog.close(); });
        QVERIFY(!dialog.isVisible()); QCOMPARE(database_->workRecords(project).first().summary, record.summary);
        CollaborationRecordDialog saveOnClose(database_.get(), record);
        saveOnClose.show();
        saveOnClose.findChild<HeadingTextEdit *>("recordSummaryEditor")->setPlainText(QStringLiteral("关闭时保存"));
        interact(QStringLiteral("未保存的修改"), [&](QDialog &prompt) { button(prompt, QStringLiteral("保存"))->click(); }, [&] { QTest::keyClick(&saveOnClose, Qt::Key_Escape); });
        QVERIFY(!saveOnClose.isVisible()); QCOMPARE(database_->workRecords(project).first().summary, QStringLiteral("关闭时保存"));
    }

    void collaborationSummarySearchIncludesFolderAndRootRecords()
    {
        int project = 0, folder = 0;
        QVERIFY(database_->addProject(QStringLiteral("摘要搜索"), QString(), &project));
        QVERIFY(database_->addRecordFolder(project, QStringLiteral("初始化"), &folder));
        AppDatabase::WorkRecord nested; nested.projectId = project; nested.folderId = folder;
        nested.title = QStringLiteral("常规任务"); nested.content = sample; nested.summary = QStringLiteral("唯一关键词：执行约束");
        QVERIFY(database_->saveWorkRecord(&nested));
        auto root = nested; root.id = 0; root.folderId = 0; root.title = QStringLiteral("独立记录");
        QVERIFY(database_->saveWorkRecord(&root));
        auto unrelated = root; unrelated.id = 0; unrelated.title = QStringLiteral("无关记录"); unrelated.summary = QStringLiteral("其他内容");
        QVERIFY(database_->saveWorkRecord(&unrelated));
        ProjectsPage page(database_.get()); page.show();
        auto *tree = page.findChild<QTreeWidget *>("workRecordList");
        auto *search = page.findChild<QLineEdit *>("recordSearch");
        search->setText(QStringLiteral("执行约束"));
        int visible = 0;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto *item = tree->topLevelItem(i);
            if (!item->isHidden()) ++visible;
            if (item->text(0) == QStringLiteral("初始化")) QVERIFY(item->isExpanded() && !item->child(0)->isHidden());
            if (item->text(0) == QStringLiteral("无关记录")) QVERIFY(item->isHidden());
        }
        QCOMPARE(visible, 2);
        search->clear();
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->childCount()) QVERIFY(!tree->topLevelItem(i)->isExpanded());
    }

    void updateNoticeOpensSettings()
    {
        const QString cache = fixture(QStringLiteral("data/update-check.json"));
        const auto cleanup = qScopeGuard([&] { QFile::remove(cache); });
        const QString tag = QStringLiteral("v2.3.0");
        const QString asset = QStringLiteral("Orchestrate-v2.3.0-windows-x64.zip");
        const QString notes = QStringLiteral("更新说明");
        QJsonObject release {{"tag_name", tag}, {"body", notes}, {"assets", QJsonArray{QJsonObject{
            {"name", asset}, {"browser_download_url", "https://github.com/VelvetEvening/Orchestrate/releases/download/v2.3.0/" + asset},
            {"size", 18000000}, {"digest", "sha256:" + QString(64, 'a')}}}}};
        QFile file(cache); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"automatic_enabled", false}, {"release", release}}).toJson()); file.close();
        MainWindow window;
        auto *update = window.findChild<UpdateWidget *>(); QVERIFY(update);
        auto *settings = button(window, QStringLiteral("设置 · 有更新"));
        QVERIFY(settings->toolTip().contains(tag));
        auto *action = window.findChild<QAction *>("viewUpdateAction"); QVERIFY(action); QVERIFY(action->isVisible());
        action->trigger();
        QTRY_VERIFY(update->isVisible());
        capture(window, QStringLiteral("settings-update-available"));
        window.findChild<QPushButton *>("sidebarToggle")->click();
        QVERIFY(settings->text().isEmpty()); QVERIFY(settings->toolTip().contains(tag));
        QVERIFY(settings->accessibleName().contains(tag));
        window.findChildren<QPushButton *>("navButton").first()->click();
        capture(window, QStringLiteral("settings-update-collapsed-indicator"));
        update->updateAvailableChanged(QString());
        QVERIFY(!action->isVisible());
        window.findChild<QPushButton *>("sidebarToggle")->click();
        QCOMPARE(settings->text(), QStringLiteral("设置"));
    }

    void autoStartSettingsPersistence()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("login.ini");
        const QString approvalPath = directory.filePath("approval.ini");
        const QString startupEntry = AutoStart::entryNameForExecutable(QStringLiteral("C:/应用 目录/Orchestrate.exe"));
        const auto backend = [&] {
            return std::make_unique<AutoStart>(std::make_unique<QSettings>(path, QSettings::IniFormat),
                std::make_unique<QSettings>(approvalPath, QSettings::IniFormat), QStringLiteral("C:/应用 目录/Orchestrate.exe"));
        };
        {
            MainWindow window(nullptr, backend());
            window.show();
            button(window, QStringLiteral("设置"))->click();
            auto *check = window.findChild<QCheckBox *>(QStringLiteral("autoStartCheck"));
            auto *status = window.findChild<QLabel *>(QStringLiteral("autoStartStatus"));
            QVERIFY(check && check->isEnabled() && !check->isChecked());
            QTest::mouseClick(check, Qt::LeftButton, Qt::NoModifier, QPoint(10, check->height() / 2));
            QVERIFY(check->isChecked());
            QVERIFY(status->text().contains(QStringLiteral("已开启")));
            capture(window, QStringLiteral("settings-autostart"));
            auto *updateCheck = window.findChild<QPushButton *>(QStringLiteral("checkUpdatesButton"));
            QVERIFY(updateCheck && updateCheck->isVisible());
            QVERIFY(window.findChild<QLabel *>(QStringLiteral("updateStatus")));
            window.resize(980, 640);
            QCoreApplication::processEvents();
            auto *retry = button(window, QStringLiteral("重试注册 Alt+X"));
            QVERIFY(retry->mapTo(&window, retry->rect().bottomRight()).y() < window.height());
            capture(window, QStringLiteral("settings-autostart-compact"));
        }
        MainWindow restored(nullptr, backend());
        auto *check = restored.findChild<QCheckBox *>(QStringLiteral("autoStartCheck"));
        QVERIFY(check->isChecked());
        check->setChecked(false);
        QSettings actual(path, QSettings::IniFormat);
        actual.sync();
        QVERIFY(!actual.contains(startupEntry));
        // Changes from Windows settings are picked up on re-entering the page.
        actual.setValue(startupEntry, QStringLiteral("\"C:\\old\\Orchestrate.exe\""));
        actual.sync();
        button(restored, QStringLiteral("设置"))->click();
        QVERIFY(check->isChecked());
        QVERIFY(restored.findChild<QLabel *>(QStringLiteral("autoStartStatus"))->text().contains(QStringLiteral("其他位置")));
        QSettings approval(approvalPath, QSettings::IniFormat);
        approval.setValue(startupEntry, QByteArray::fromHex("030000000000000000000000"));
        approval.sync();
        restored.show();
        button(restored, QStringLiteral("设置"))->click();
        QVERIFY(restored.findChild<QLabel *>(QStringLiteral("autoStartStatus"))->text().contains(QStringLiteral("已被 Windows 禁用")));
        QVERIFY(button(restored, QStringLiteral("打开 Windows 启动应用设置"))->isVisible());
        capture(restored, QStringLiteral("settings-autostart-disabled"));
    }

    void autoStartSettingsWriteFailure()
    {
        QTemporaryDir directory;
        QFile blocker(directory.filePath("file"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        auto startup = std::make_unique<AutoStart>(
            std::make_unique<QSettings>(blocker.fileName() + "/login.ini", QSettings::IniFormat),
            nullptr, QStringLiteral("C:/Orchestrate.exe"));
        MainWindow window(nullptr, std::move(startup));
        auto *check = window.findChild<QCheckBox *>(QStringLiteral("autoStartCheck"));
        check->setChecked(true);
        QVERIFY(!check->isChecked());
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("autoStartStatus"))->text().contains(QStringLiteral("保存开机启动设置失败")));
    }

    void calendarFocusIndependentRendering()
    {
        MainWindow window;
        auto *calendar = window.findChild<QCalendarWidget *>();
        QVERIFY(calendar);
        QVERIFY(!calendar->isGridVisible());
        auto bluePixels = [calendar] {
            const QImage img = calendar->grab().toImage();
            int count = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (img.pixelColor(x, y) == QColor(UiTheme::Selection)) ++count;
            return count;
        };
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(bluePixels() > 400);
        // Deliberately supply a gray inactive selection, as the native style does.
        auto palette = calendar->palette();
        palette.setColor(QPalette::Inactive, QPalette::Highlight, QColor("#eeeeee"));
        palette.setColor(QPalette::Inactive, QPalette::HighlightedText, Qt::black);
        calendar->setPalette(palette);
        window.setWindowState(window.windowState() & ~Qt::WindowActive);
        calendar->clearFocus();
        QEvent deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(&window, &deactivate);
        QVERIFY(bluePixels() > 400);
        capture(window, QStringLiteral("calendar-inactive"));
        window.activateWindow();
        calendar->setFocus();
        auto *dateView = calendar->findChild<QAbstractItemView *>(QStringLiteral("qt_calendar_calendarview"));
        QVERIFY(dateView);
        const QDate nextDate = calendar->selectedDate().addDays(1);
        QTest::keyClick(dateView, Qt::Key_Right);
        QCOMPARE(calendar->selectedDate(), nextDate);
        QVERIFY(bluePixels() > 400);
        calendar->showNextMonth();
        calendar->showPreviousMonth();
        // Right crosses into the following month/year when today is its last day.
        QCOMPARE(calendar->monthShown(), nextDate.month());
        QCOMPARE(calendar->yearShown(), nextDate.year());
        for (auto *label : window.findChildren<QLabel *>()) {
            QVERIFY(label->objectName() != QStringLiteral("pageSubtitle"));
            QVERIFY(label->objectName() != QStringLiteral("headingHelp"));
            QVERIFY(!label->text().contains(QStringLiteral("内容保存在本机 SQLite")));
        }
        button(window, QStringLiteral("设置"))->click();
        capture(window, QStringLiteral("settings-clean"));
        button(window, QStringLiteral("项目"))->click();
        capture(window, QStringLiteral("projects-clean"));
    }

    void calendarColorsAndSilentTray()
    {
        MainWindow window;
        window.show();
        auto *calendar = window.findChild<QCalendarWidget *>();
        QVERIFY(calendar);
        // Actual selected pixels are tested above; native palettes no longer
        // determine the selected cell's appearance.
        QCOMPARE(calendar->dateTextFormat(QDate::currentDate()).background().color(), QColor(UiTheme::Selection));
        capture(window, QStringLiteral("calendar-today-selected"));
        calendar->setSelectedDate(QDate::currentDate().addDays(QDate::currentDate().day() == 1 ? 1 : -1));
        capture(window, QStringLiteral("calendar-today-unselected"));
        auto *tray = window.findChild<QSystemTrayIcon *>();
        auto *closeCheck = window.findChild<QCheckBox *>(QStringLiteral("closeToTrayCheck"));
        auto *shortcut = window.findChild<GlobalShortcut *>();
        QVERIFY(tray && closeCheck && shortcut);
        tray->show();
        closeCheck->setChecked(true);
        for (int i = 0; i < 3; ++i) {
            QCloseEvent close;
            QApplication::sendEvent(&window, &close);
            QVERIFY(!close.isAccepted());
            QVERIFY(!window.isVisible());
            QVERIFY(!window.mainWindowVisible());
            QVERIFY(QApplication::activeModalWidget() == nullptr);
            // Exercise signal-to-window wiring, without sending system keystrokes.
            QVERIFY(QMetaObject::invokeMethod(shortcut, "activated", Qt::DirectConnection));
            QVERIFY(window.isVisible());
            QVERIFY(!window.isMinimized());
            QVERIFY(window.mainWindowVisible());
        }
        window.showMinimized();
        QVERIFY(QMetaObject::invokeMethod(shortcut, "activated", Qt::DirectConnection));
        QVERIFY(!window.isMinimized());
        closeCheck->setChecked(false);
        QCloseEvent close;
        QApplication::sendEvent(&window, &close);
        QVERIFY(close.isAccepted());
    }

    void shortcutPreferenceAndIsolation()
    {
        {
            MainWindow window;
            auto *check = window.findChild<QCheckBox *>(QStringLiteral("globalShortcutCheck"));
            auto *shortcut = window.findChild<GlobalShortcut *>();
            auto *label = window.findChild<QLabel *>(QStringLiteral("globalShortcutStatus"));
            QVERIFY(check && shortcut && label);
            QVERIFY(check->isChecked());
            QVERIFY(!shortcut->isRegistered());
            QVERIFY(label->text().contains(QStringLiteral("测试环境")));
            QSignalSpy spy(shortcut, &GlobalShortcut::activated);
            qintptr result = 123;
            QVERIFY(!shortcut->nativeEventFilter("windows_dispatcher_MSG", nullptr, &result));
            QCOMPARE(spy.count(), 0);
            QCOMPARE(result, qintptr(123));
            check->setChecked(false);
            QVERIFY(label->text().contains(QStringLiteral("已禁用")));
            QVERIFY(!button(window, QStringLiteral("重试注册 Alt+X"))->isEnabled());
        }
        {
            MainWindow window;
            auto *check = window.findChild<QCheckBox *>(QStringLiteral("globalShortcutCheck"));
            QVERIFY(check && !check->isChecked());
            check->setChecked(true);
            QVERIFY(button(window, QStringLiteral("重试注册 Alt+X"))->isEnabled());
            button(window, QStringLiteral("重试注册 Alt+X"))->click();
            QVERIFY(!window.findChild<GlobalShortcut *>()->isRegistered());
        }
        MainWindow restored;
        QVERIFY(restored.findChild<QCheckBox *>(QStringLiteral("globalShortcutCheck"))->isChecked());
    }

    void rules_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("rendered");
        QTest::addColumn<int>("level");
        QTest::newRow("h1") << QStringLiteral("# 标题") << QStringLiteral("标题") << 1;
        QTest::newRow("h2") << QStringLiteral("## 标题") << QStringLiteral("标题") << 2;
        QTest::newRow("h3") << QStringLiteral("### 标题") << QStringLiteral("标题") << 3;
        QTest::newRow("spaces") << QStringLiteral("##   标题  ") << QStringLiteral("标题  ") << 2;
        for (const auto &literal : {QStringLiteral("#### 四级不支持"), QStringLiteral("#无空格"),
                                    QStringLiteral("#\t制表符"), QStringLiteral(" # 缩进不是标题"),
                                    QStringLiteral("正文 # 符号"), QStringLiteral("#"), QStringLiteral("#   "),
                                    QStringLiteral("**粗体** [链接](https://example.com) ![图](file:///x)"),
                                    QStringLiteral("<h1>HTML</h1> & <img src='file:///x'>"),
                                    QStringLiteral("- 列表"), QStringLiteral("旧记录：普通文本")}) {
            QTest::newRow(qPrintable(literal)) << literal << literal << 0;
        }
        QTest::newRow("closing-hashes-literal") << QStringLiteral("# 标题 ###") << QStringLiteral("标题 ###") << 1;
    }
    void rules()
    {
        QFETCH(QString, source);
        QFETCH(QString, rendered);
        QFETCH(int, level);
        QTextDocument document;
        HeadingText::render(document, source);
        QCOMPARE(document.toPlainText(), rendered);
        QCOMPARE(document.firstBlock().blockFormat().headingLevel(), level);
        for (auto it = document.firstBlock().begin(); !it.atEnd(); ++it) {
            QVERIFY(!it.fragment().charFormat().isAnchor());
            QVERIFY(!it.fragment().charFormat().isImageFormat());
        }
        QVERIFY(document.firstBlock().textList() == nullptr);
    }
    void formattingAndWhitespace()
    {
        QTextDocument document;
        QFont base = QApplication::font();
        base.setPointSizeF(10);
        document.setDefaultFont(base);
        HeadingText::render(document, QStringLiteral("# A\r\n## B\r\n### C\r\nbody\r\n\r\n  spaces  \r\n"));
        QCOMPARE(document.toPlainText(), QStringLiteral("A\nB\nC\nbody\n\n  spaces  \n"));
        QCOMPARE(document.blockCount(), 7);
        qreal previous = 100;
        for (int i = 0; i < 3; ++i) {
            const auto block = document.findBlockByNumber(i);
            QCOMPARE(block.blockFormat().headingLevel(), i + 1);
            const auto format = block.begin().fragment().charFormat();
            QCOMPARE(format.fontWeight(), int(QFont::Bold));
            QVERIFY(format.fontPointSize() < previous);
            previous = format.fontPointSize();
        }
        QCOMPARE(document.findBlockByNumber(3).blockFormat().headingLevel(), 0);
        QCOMPARE(document.findBlockByNumber(3).begin().fragment().charFormat().fontPointSize(), 10.0);
        HeadingText::render(document, QStringLiteral("plain"));
        QCOMPARE(document.firstBlock().blockFormat().headingLevel(), 0);
        QCOMPARE(document.firstBlock().begin().fragment().charFormat().fontWeight(), base.weight());
    }
    void editorSwitchUndoAndPaste()
    {
        HeadingTextEdit editor;
        editor.resize(600, 400);
        editor.show();
        auto *source = editor.findChild<QPlainTextEdit *>(QStringLiteral("headingSource"));
        auto *preview = editor.findChild<QTextEdit *>(QStringLiteral("headingPreview"));
        QVERIFY(source && preview);
        const QString original = QStringLiteral("\n  原文\n") + sample + QStringLiteral("\n\n");
        editor.setPlainText(original);
        source->moveCursor(QTextCursor::End);
        source->insertPlainText(QStringLiteral("补充"));
        const int position = source->textCursor().position();
        for (int i = 0; i < 3; ++i) {
            editor.setPreviewMode(true);
            QCOMPARE(editor.toPlainText(), original + QStringLiteral("补充"));
            QVERIFY(!preview->toPlainText().contains(QStringLiteral("# 今日")));
            QVERIFY(preview->toPlainText().contains(QStringLiteral("<b>不是粗体</b>")));
            editor.setPreviewMode(false);
            QCOMPARE(source->textCursor().position(), position);
        }
        QVERIFY(source->document()->isUndoAvailable());
        source->undo();
        QCOMPARE(editor.toPlainText(), original);
        source->redo();
        QCOMPARE(editor.toPlainText(), original + QStringLiteral("补充"));
        auto *mime = new QMimeData;
        mime->setText(QStringLiteral("<b>粘贴原文</b>"));
        mime->setHtml(QStringLiteral("<b>粘贴原文</b>"));
        QApplication::clipboard()->setMimeData(mime);
        source->selectAll();
        source->paste();
        QCOMPARE(editor.toPlainText(), QStringLiteral("<b>粘贴原文</b>"));
        editor.setPreviewMode(true);
        editor.setPlainText(QStringLiteral("## 切换记录后"));
        QCOMPARE(preview->toPlainText(), QStringLiteral("切换记录后"));
        editor.clear();
        QVERIFY(!editor.isPreviewMode());
        QVERIFY(editor.toPlainText().isEmpty());
    }
    void previewTabIsLeftAndEditTabIsRight()
    {
        HeadingTextEdit editor;
        editor.resize(600, 400);
        editor.show();
        editor.setPlainText(sample);
        auto *tabs = editor.findChild<QTabBar *>();
        auto *source = editor.findChild<QPlainTextEdit *>();
        auto *preview = editor.findChild<QTextEdit *>();
        QCOMPARE(tabs->tabText(0), QStringLiteral("预览"));
        QCOMPARE(tabs->tabText(1), QStringLiteral("编辑"));
        QVERIFY(!editor.isPreviewMode());
        QCOMPARE(tabs->currentIndex(), 1);
        QVERIFY(preview->isVisible());
        QVERIFY(!preview->isReadOnly());
        QSignalSpy changes(&editor, &HeadingTextEdit::textChanged);
        QSignalSpy modes(&editor, &HeadingTextEdit::previewModeChanged);
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(0).center());
        QVERIFY(editor.isPreviewMode());
        QVERIFY(preview->isVisible());
        QVERIFY(preview->isReadOnly());
        QCOMPARE(editor.focusProxy(), preview);
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(1).center());
        QVERIFY(!editor.isPreviewMode());
        QVERIFY(preview->isVisible());
        QVERIFY(!preview->isReadOnly());
        QCOMPARE(editor.focusProxy(), preview);
        editor.setSourceMode(true);
        QVERIFY(source->isVisible());
        editor.setSourceMode(false);
        QCOMPARE(editor.toPlainText(), sample);
        QCOMPARE(changes.count(), 0);
        QCOMPARE(modes.count(), 2);
        QCOMPARE(modes.at(0).at(0).toBool(), true);
        QCOMPARE(modes.at(1).at(0).toBool(), false);
        editor.setPreviewMode(true);
        QCOMPARE(tabs->currentIndex(), 0);
        editor.clear();
        QCOMPARE(tabs->currentIndex(), 1);
    }
    void editorZoomPersistsAcrossReopen()
    {
        const QString key = QStringLiteral("text_zoom.diary");
        {
            HeadingTextEdit editor;
            bindPersistentTextZoom(&editor, database_.get(), key);
            editor.setPlainText(sample);
            QSignalSpy changes(&editor, &HeadingTextEdit::textChanged);
            editor.setTextPointSize(19.5);
            QCOMPARE(database_->setting(key).toDouble(), 19.5);
            QCOMPARE(changes.count(), 0);
            QCOMPARE(editor.toPlainText(), sample);
        }
        database_.reset();
        database_ = std::make_unique<AppDatabase>();
        QString error;
        QVERIFY2(database_->open(&error), qPrintable(error));
        HeadingTextEdit reopened;
        QSignalSpy sizeChanges(&reopened, &HeadingTextEdit::textPointSizeChanged);
        bindPersistentTextZoom(&reopened, database_.get(), key);
        reopened.setPlainText(sample);
        QCOMPARE(reopened.findChild<QPlainTextEdit *>()->font().pointSizeF(), 19.5);
        reopened.setPreviewMode(true);
        QCOMPARE(reopened.findChild<QTextEdit *>()->document()->defaultFont().pointSizeF(), 19.5);
        reopened.setTextPointSize(21);
        QCOMPARE(database_->setting(key).toDouble(), 21.0);
        QVERIFY(database_->setting(QStringLiteral("text_zoom.reminder")).isEmpty());
        QCOMPARE(sizeChanges.count(), 2);
    }
    void editorZoomInvalidPreference()
    {
        for (const auto &value : {"garbage", "nan", "inf", "0", "-1", "73"}) {
            QVERIFY(database_->setSetting(QStringLiteral("text_zoom.diary"), QString::fromLatin1(value)));
            HeadingTextEdit editor;
            const QFont original = editor.findChild<QPlainTextEdit *>()->font();
            bindPersistentTextZoom(&editor, database_.get(), QStringLiteral("text_zoom.diary"));
            QCOMPARE(editor.findChild<QPlainTextEdit *>()->font(), original);
        }
    }
    void editorWheelZoomPreservesState_data()
    {
        QTest::addColumn<bool>("pixelFont");
        QTest::newRow("point-font") << false;
        QTest::newRow("pixel-font") << true;
    }
    void editorWheelZoomPreservesState()
    {
        QFETCH(bool, pixelFont);
        HeadingTextEdit editor, other;
        QFont font = editor.font();
        if (pixelFont) font.setPixelSize(20);
        else font.setPointSizeF(12);
        editor.setFont(font);
        editor.resize(600, 400);
        editor.show();
        editor.setPlainText(sample);
        auto *source = editor.findChild<QPlainTextEdit *>();
        auto *preview = editor.findChild<QTextEdit *>();
        auto *tabs = editor.findChild<QTabBar *>();
        const QFont tabsFont = tabs->font();
        const QFont otherFont = other.findChild<QPlainTextEdit *>()->font();
        editor.setPreviewMode(true);
        const qreal initialSize = preview->document()->defaultFont().pointSizeF();
        editor.setPreviewMode(false);
        source->moveCursor(QTextCursor::End);
        source->insertPlainText(QStringLiteral("补充"));
        QTextCursor selection = source->textCursor();
        selection.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor, 2);
        source->setTextCursor(selection);
        const int position = selection.position(), anchor = selection.anchor();
        const bool modified = source->document()->isModified();
        QSignalSpy changes(&editor, &HeadingTextEdit::textChanged);

        sendWheel(source->viewport(), 120);
        QCOMPARE(source->font().pointSizeF(), initialSize + 1);
        QCOMPARE(editor.toPlainText(), sample + QStringLiteral("补充"));
        QCOMPARE(source->textCursor().position(), position);
        QCOMPARE(source->textCursor().anchor(), anchor);
        QCOMPARE(source->document()->isModified(), modified);
        editor.setPreviewMode(true);
        selection = preview->textCursor();
        selection.setPosition(0);
        selection.setPosition(4, QTextCursor::KeepAnchor);
        preview->setTextCursor(selection);
        const QString selected = preview->textCursor().selectedText();
        sendWheel(preview->viewport(), 120);
        QCOMPARE(preview->textCursor().selectedText(), selected);
        QCOMPARE(source->font().pointSizeF(), initialSize + 2);
        QCOMPARE(preview->document()->defaultFont().pointSizeF(), initialSize + 2);
        const qreal scales[] = {1.6, 1.35, 1.15};
        for (int level = 0; level < 3; ++level) {
            const auto format = preview->document()->findBlockByNumber(level * 2).begin().fragment().charFormat();
            QCOMPARE(format.fontPointSize(), (initialSize + 2) * scales[level]);
        }
        QCOMPARE(changes.count(), 0);
        QCOMPARE(tabs->font(), tabsFont);
        QCOMPARE(other.findChild<QPlainTextEdit *>()->font(), otherFont);
        editor.setPreviewMode(false);
        QCOMPARE(source->textCursor().position(), position);
        QCOMPARE(source->textCursor().anchor(), anchor);
        source->undo();
        QCOMPARE(editor.toPlainText(), sample);
        source->redo();
        QCOMPARE(editor.toPlainText(), sample + QStringLiteral("补充"));
    }
    void editorWheelZoomScrollingAndLimits()
    {
        HeadingTextEdit editor;
        editor.resize(500, 320);
        editor.show();
        editor.setPlainText(sample.repeated(30));
        auto *source = editor.findChild<QPlainTextEdit *>();
        auto *preview = editor.findChild<QTextEdit *>();
        const qreal initialSize = source->font().pointSizeF();
        sendWheel(source->viewport(), -120, Qt::NoModifier);
        QVERIFY(source->verticalScrollBar()->value() > 0);
        QCOMPARE(source->font().pointSizeF(), initialSize);
        sendWheel(source->viewport(), 60);
        QCOMPARE(source->font().pointSizeF(), initialSize + 0.5);
        sendWheel(source->viewport(), 0, Qt::ControlModifier, -20);
        QCOMPARE(source->font().pointSizeF(), initialSize);
        editor.setPreviewMode(true);
        auto *scroll = preview->verticalScrollBar();
        scroll->setValue(scroll->maximum() / 2);
        const auto top = preview->cursorForPosition(QPoint(0, 0));
        const int topPosition = top.position();
        const int topOffset = preview->cursorRect(top).top();
        sendWheel(preview->viewport(), 120);
        QTextCursor restored(preview->document());
        restored.setPosition(topPosition);
        QVERIFY(qAbs(preview->cursorRect(restored).top() - topOffset) <= 1);
        const int beforeScroll = scroll->value();
        sendWheel(preview->viewport(), -120, Qt::NoModifier);
        QVERIFY(scroll->value() > beforeScroll);
        QCOMPARE(source->font().pointSizeF(), initialSize + 1);
        sendWheel(preview->viewport(), 12000);
        QCOMPARE(source->font().pointSizeF(), 72.0);
        sendWheel(preview->viewport(), -12000);
        QCOMPARE(source->font().pointSizeF(), 6.0);
        sendWheel(preview->viewport(), 120);
        QCOMPARE(source->font().pointSizeF(), 7.0);
        QCOMPARE(editor.toPlainText(), sample.repeated(30));
        QVERIFY(!source->document()->isModified());
    }
    void listWrappingAndSelection()
    {
        QListWidget list;
        HeadingText::configureList(&list);
        list.resize(600, 240);
        list.show();
        // QStyledItemDelegate's display text normalizes line breaks. Verify the
        // custom delegate measures the original model text, not that substitute.
        const QString multiline = QStringLiteral("\n# 标题\n正文\n## 子标题\n其他正文");
        list.addItem(multiline);
        QTest::qWait(20);
        QStyleOptionViewItem option;
        option.initFrom(&list);
        QTextDocument expected;
        expected.setDefaultFont(list.font());
        HeadingText::render(expected, multiline);
        expected.setTextWidth(list.viewport()->width() - 20);
        QCOMPARE(list.itemDelegate()->sizeHint(option, list.model()->index(0, 0)).height(),
                 qCeil(expected.size().height()) + 16);
        list.clear();
        const QString longText = QStringLiteral("# 长记录\n") + QString(600, QChar(0x4e2d));
        list.addItem(longText);
        list.addItem(QStringLiteral("## 第二条"));
        list.resize(600, 240);
        list.show();
        QTest::qWait(30);
        const int wideHeight = list.sizeHintForRow(0);
        list.resize(260, 240);
        QTest::qWait(30);
        QVERIFY(list.sizeHintForRow(0) > wideHeight);
        QVERIFY(list.verticalScrollBar()->maximum() > 0);
        QCOMPARE(list.verticalScrollMode(), QAbstractItemView::ScrollPerPixel);
        list.setCurrentRow(1);
        list.scrollToItem(list.item(1));
        QTest::qWait(20);
        QVERIFY(list.viewport()->rect().intersects(list.visualItemRect(list.item(1))));
        QCOMPARE(list.item(0)->text(), longText);
        QCOMPARE(list.currentRow(), 1);
        capture(list, QStringLiteral("list-narrow"));
    }
    void diaryRoundTrip()
    {
        MainWindow window;
        window.show();
        auto *page = window.findChild<CalendarPage *>();
        QVERIFY(page);
        const QString raw = QStringLiteral("\n") + sample + QStringLiteral("\n  ");
        interact(QStringLiteral("添加日记"), [&](QDialog &dialog) {
            auto *editor = dialogEditor(dialog);
            require(!editor->isPreviewMode(),QStringLiteral("New diary must start in edit"));
            editor->setPlainText(raw);
            saveDialog(dialog);
            require(dialog.findChild<QLabel *>("diarySaveStatus")->text()==QStringLiteral("待填写简介"),QStringLiteral("Missing diary summary allowed"));
            dialog.findChild<QLineEdit *>(QStringLiteral("diaryTitle"))->setText(QStringLiteral("今天的进展"));
            capture(dialog, QStringLiteral("diary-preview"));
            saveDialog(dialog);
        }, [&] { button(*page, QStringLiteral("添加日记"))->click(); });
        auto entries = database_->diaryEntries(QDate::currentDate());
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().content, raw);
        QCOMPARE(entries.first().title,QStringLiteral("今天的进展"));
        const int id = entries.first().id;
        auto *list = page->findChild<QListWidget *>(QStringLiteral("diaryList"));
        QVERIFY(list);
        QCOMPARE(list->item(0)->text(),QStringLiteral("今天的进展"));
        list->setCurrentRow(0);
        capture(window, QStringLiteral("calendar-headings"));
        interact(QStringLiteral("日记"), [&](QDialog &dialog) {
            auto *editor = dialogEditor(dialog);
            require(editor->toPlainText() == raw, QStringLiteral("Stored markers/whitespace lost"));
            enterEdit(editor);
            editor->setPlainText(QStringLiteral("# 不保存"));
            QTimer::singleShot(0,[]{ auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); if(box) box->button(QMessageBox::Discard)->click(); });
            dialog.reject();
        }, [&] { list->itemDoubleClicked(list->item(0)); });
        QCOMPARE(database_->diaryEntries(QDate::currentDate()).first().content, raw);
        interact(QStringLiteral("日记"), [&](QDialog &dialog) {
            enterEdit(dialogEditor(dialog));
            dialogEditor(dialog)->setPlainText(QStringLiteral("### 修改后\n仍是文本"));
            saveDialog(dialog);
        }, [&] { list->itemDoubleClicked(list->item(0)); });
        entries = database_->diaryEntries(QDate::currentDate());
        QCOMPARE(entries.first().id, id);
        QCOMPARE(entries.first().content, QStringLiteral("### 修改后\n仍是文本"));
        AppDatabase reopened;
        QString error;
        QVERIFY2(reopened.open(&error), qPrintable(error));
        QCOMPARE(reopened.diaryEntries(QDate::currentDate()).first().content, entries.first().content);
    }
    void diaryMigrationAndCollection()
    {
        const QDate oldDate=QDate::currentDate().addDays(-5);
        const QDate recentDate=QDate::currentDate().addDays(-1);
        const QString raw=QStringLiteral("\n  旧日记简介\n## 完整正文\n内容\n");
        QString error;
        QVERIFY(database_->addDiaryEntry(oldDate,raw,&error));
        const auto before=database_->diaryEntries(oldDate).first();
        const QString connection=QStringLiteral("diary-migration-fixture");
        {
            auto sql=QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),connection);
            sql.setDatabaseName(database_->databasePath());
            QVERIFY(sql.open());
            QSqlQuery query(sql);
            QVERIFY2(query.exec(QStringLiteral("ALTER TABLE diary_entries DROP COLUMN title")),qPrintable(query.lastError().text()));
            QVERIFY(query.exec(QStringLiteral("ALTER TABLE automation_tools DROP COLUMN customized")));
            QVERIFY(query.exec(QStringLiteral("ALTER TABLE automation_tools DROP COLUMN environment")));
            QVERIFY(query.exec(QStringLiteral("ALTER TABLE tool_commands DROP COLUMN enabled")));
            QVERIFY(query.exec(QStringLiteral("ALTER TABLE tool_commands DROP COLUMN position")));
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 0")));
        }
        QSqlDatabase::removeDatabase(connection);
        AppDatabase migrated;
        QVERIFY2(migrated.open(&error),qPrintable(error));
        const auto after=migrated.diaryEntry(before.id);
        QCOMPARE(after.content,before.content);
        QCOMPARE(after.date,before.date);
        QCOMPARE(after.createdAt,before.createdAt);
        QCOMPARE(after.updatedAt,before.updatedAt);
        QVERIFY(after.title.isEmpty());
        QVERIFY(migrated.addDiaryEntry(recentDate,QStringLiteral("上午学习"),sample,&error));
        const int earlier=migrated.diaryEntries(recentDate).first().id;
        QVERIFY(migrated.addDiaryEntry(recentDate,QStringLiteral("晚间总结"),sample,&error));
        const int later=migrated.diaryEntries(recentDate).first().id;
        QVERIFY(later>earlier);
        QVERIFY(migrated.updateDiaryEntry(later,QStringLiteral("兼容旧接口的正文"),&error));
        QCOMPARE(migrated.diaryEntry(later).title,QStringLiteral("晚间总结"));
        const auto all=migrated.allDiaryEntries();
        QCOMPARE(all.size(),3);
        QCOMPARE(all.at(0).id,later);
        QCOMPARE(all.at(1).id,earlier);
        QCOMPARE(all.at(2).id,before.id);
        MainWindow window;
        window.show();
        auto *page=window.findChild<CalendarPage *>();
        button(*page,QStringLiteral("日记集合"))->click();
        auto *collection=window.findChild<QDialog *>(QStringLiteral("diaryCollection"));
        QVERIFY(collection && collection->isVisible());
        auto *list=collection->findChild<QListWidget *>(QStringLiteral("diaryCollectionList"));
        auto *search=collection->findChild<QLineEdit *>(QStringLiteral("diaryCollectionSearch"));
        QVERIFY(list && search);
        QCOMPARE(list->count(),3);
        QCOMPARE(list->item(0)->data(Qt::UserRole).toInt(),later);
        QVERIFY(list->item(2)->text().contains(QStringLiteral("旧日记简介")));
        QVERIFY(!list->item(2)->text().contains(QStringLiteral("完整正文")));
        capture(*collection,QStringLiteral("diary-collection"));
        search->setText(QStringLiteral("晚间"));
        QVERIFY(!list->item(0)->isHidden());
        QVERIFY(list->item(1)->isHidden() && list->item(2)->isHidden());
        list->setCurrentRow(0);
        interact(QStringLiteral("日记"),[&](QDialog &dialog) {
            auto *title=dialog.findChild<QLineEdit *>(QStringLiteral("diaryTitle"));
            require(!title->isReadOnly(),QStringLiteral("Diary summary locked"));
            enterEdit(dialogEditor(dialog));
            title->setText(QStringLiteral("完成阶段整理"));
            dialogEditor(dialog)->setPlainText(sample);
            saveDialog(dialog);
        },[&]{list->setFocus(); QTest::keyClick(list,Qt::Key_Return);});
        QCOMPARE(migrated.diaryEntry(later).title,QStringLiteral("完成阶段整理"));
        QVERIFY(list->item(0)->isHidden());
        search->setText(oldDate.toString(Qt::ISODate));
        QVERIFY(!list->item(2)->isHidden());
        search->clear();
        collection->close();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        button(*page,QStringLiteral("日记集合"))->click();
        collection=window.findChild<QDialog *>(QStringLiteral("diaryCollection"));
        list=collection->findChild<QListWidget *>(QStringLiteral("diaryCollectionList"));
        QCOMPARE(window.findChildren<QDialog *>(QStringLiteral("diaryCollection")).size(),1);
        QCOMPARE(list->item(0)->data(Qt::UserRole).toInt(),later);
        collection->close();
        page->findChild<QCalendarWidget *>()->setSelectedDate(oldDate);
        auto *dayList=page->findChild<QListWidget *>(QStringLiteral("diaryList"));
        QCOMPARE(dayList->item(0)->text(),QStringLiteral("旧日记简介"));
    }

    void calendarRecordSymbolsAndFutureDates_data()
    {
        QTest::addColumn<QString>("preset");
        QTest::newRow("mist") << QStringLiteral("mist");
        QTest::newRow("paper") << QStringLiteral("paper");
        QTest::newRow("precision") << QStringLiteral("precision");
    }
    void calendarRecordSymbolsAndFutureDates()
    {
        QFETCH(QString, preset);
        UiTheme::apply(*qApp, preset);
        const auto restoreTheme = qScopeGuard([] { UiTheme::apply(*qApp, QStringLiteral("mist")); });
        CalendarCellProbe probe;
        const QDate today=QDate::currentDate();
        probe.setCurrentPage(today.year(),today.month());
        const QDate future=today.addDays(1);
        probe.setCurrentPage(future.year(),future.month());
        probe.setSelectedDate(future.addDays(future.day()==1 ? 1 : -1));
        probe.setCurrentPage(future.year(),future.month());
        probe.setRecordDates({future},{future});
        const QImage both=probe.renderDate(future);
        QVERIFY(countColor(both,QColor(UiTheme::DiaryMarker))>=12);
        QVERIFY(countReminderMark(both)>0);
        QVERIFY(countColor(both,QColor(UiTheme::IndexSurface))>200);
        QVERIFY(countColor(both,QColor(UiTheme::Muted))>0);
        // The diary is solid; the reminder ring leaves the index surface visible.
        const int markerY = preset == QStringLiteral("precision") ? 46 : 50;
        QCOMPARE(both.pixelColor(33,markerY),QColor(UiTheme::DiaryMarker));
        QCOMPARE(both.pixelColor(45,markerY),QColor(UiTheme::IndexSurface));
        QVERIFY(both.pixelColor(47,markerY)!=QColor(UiTheme::IndexSurface));
        probe.setSelectedDate(future);
        const auto selected=probe.renderDate(future);
        QVERIFY(countColor(selected,QColor(UiTheme::DiaryMarker))>=12);
        QVERIFY(countReminderMark(selected)>0);
        const QDate outside=QDate(future.year(),future.month(),1).addMonths(1);
        const auto outsideImage=probe.renderDate(outside);
        QCOMPARE(outsideImage.pixelColor(0,0),QColor(UiTheme::IndexSurface));
        QVERIFY(countColor(outsideImage,QColor(UiTheme::Disabled))>0);
        probe.setCurrentPage(today.year(),today.month());
        const auto past=probe.renderDate(today.addDays(-1));
        QCOMPARE(past.pixelColor(0,0),QColor(UiTheme::IndexSurface));
        QVERIFY(countColor(past,QColor(UiTheme::Text))>0 || countColor(past,QColor(UiTheme::Disabled))>0);
    }

    void calendarMarkersRefreshAfterDiaryChanges()
    {
        MainWindow window;
        window.show();
        auto *page=window.findChild<CalendarPage *>();
        auto *calendar=page->findChild<QCalendarWidget *>();
        const QDate date=QDate::currentDate();
        interact(QStringLiteral("添加日记"),[&](QDialog &dialog) {
            dialog.findChild<QLineEdit *>(QStringLiteral("diaryTitle"))->setText(QStringLiteral("有双标记的一天"));
            dialogEditor(dialog)->setPlainText(sample);
            saveDialog(dialog);
        },[&]{button(*page,QStringLiteral("添加日记"))->click();});
        QCoreApplication::processEvents();
        QVERIFY(countColor(calendar->grab().toImage(),QColor(UiTheme::DiaryMarker))>=12);
        interact(QStringLiteral("添加提醒"),[&](QDialog &dialog) {
            dialog.findChild<QLineEdit *>(QStringLiteral("reminderTitle"))->setText(QStringLiteral("今天的提醒"));
            dialog.findChild<QDateTimeEdit *>(QStringLiteral("reminderTime"))->setDateTime(QDateTime(date,QTime(23,59)));
            saveDialog(dialog);
        },[&]{button(*page,QStringLiteral("添加提醒"))->click();});
        QCoreApplication::processEvents();
        const auto marked=calendar->grab().toImage();
        QVERIFY(countColor(marked,QColor(UiTheme::DiaryMarker))>=12);
        QVERIFY(countReminderMark(marked)>0);
        capture(window,QStringLiteral("calendar-record-markers"));
        window.resize(980,640);
        capture(window,QStringLiteral("calendar-simplified-compact"));
        button(*page,QStringLiteral("日记集合"))->click();
        auto *collection=window.findChild<QDialog *>(QStringLiteral("diaryCollection"));
        collection->close();
        auto *list=page->findChild<QListWidget *>(QStringLiteral("diaryList"));
        list->setCurrentRow(0);
        interact(QStringLiteral("删除日记"),[&](QDialog &dialog) {
            auto *box=qobject_cast<QMessageBox *>(&dialog);
            require(box,QStringLiteral("Delete confirmation missing"));
            box->button(QMessageBox::Yes)->click();
        },[&]{
            const auto buttons=page->findChildren<QPushButton *>();
            // Diary's action follows reminder's action in the widget hierarchy.
            QPushButton *remove=nullptr;
            for(auto *candidate:buttons) if(candidate->text()==QStringLiteral("删除")) remove=candidate;
            require(remove,QStringLiteral("Diary delete button missing"));
            remove->click();
        });
        QVERIFY(database_->diaryEntries(date).isEmpty());
        QCOMPARE(list->count(),0);
        QVERIFY(list->viewport()->findChild<QLabel *>("emptyViewHint")->isVisible());
        calendar->setSelectedDate(date.addDays(1));
        QVERIFY(page->findChild<QLabel *>("selectedDate")->accessibleName().contains(
            date.addDays(1).toString(QStringLiteral("yyyy 年 MM 月 dd 日"))));
        capture(window,QStringLiteral("v3.7.0-calendar-empty-compact"));
        calendar->setSelectedDate(date);
        QCoreApplication::processEvents();
        const auto removed=calendar->grab().toImage();
        QCOMPARE(countColor(removed,QColor(UiTheme::DiaryMarker)),0);
        QVERIFY(countReminderMark(removed)>0);
        button(*page,QStringLiteral("日记集合"))->click();
        collection=window.findChild<QDialog *>(QStringLiteral("diaryCollection"));
        QCOMPARE(collection->findChild<QListWidget *>(QStringLiteral("diaryCollectionList"))->count(),0);
    }

    void collaborationStorage()
    {
        int first=0, second=0, folder=0, other=0;
        QString error;
        QVERIFY(database_->addProject("First", "", &first));
        QVERIFY(database_->addProject("Second", "", &second));
        for (const auto &project : database_->projects()) QVERIFY(!project.completed);
        QVERIFY(database_->setProjectCompleted(first,true));
        QVERIFY(database_->updateProject(first,"Renamed","D:/project"));
        QVERIFY(database_->addRecordFolder(first,"  初始化工作区  ",&folder));
        QCOMPARE(database_->recordFolders(first).first().name,QStringLiteral("初始化工作区"));
        QVERIFY(!database_->addRecordFolder(first,"初始化工作区",nullptr,&error));
        QVERIFY(!database_->addRecordFolder(first," \t ",nullptr,&error));
        QVERIFY(database_->addRecordFolder(second,"初始化工作区",&other));
        QVERIFY(database_->addWorkRecord(first,"Prompt",sample,folder));
        const auto original = database_->workRecords(first).first();
        QCOMPARE(original.folderId,folder);
        QVERIFY(!database_->setWorkRecordFolder(original.id,other,&error));
        QVERIFY(!database_->addWorkRecord(first,"bad",sample,other,&error));
        QVERIFY(!database_->setWorkRecordFolder(original.id,999999,&error));
        QCOMPARE(database_->workRecords(first).size(),1);
        QCOMPARE(database_->workRecords(first).first().folderId,folder);
        QVERIFY(database_->setWorkRecordFolder(original.id,0));
        QCOMPARE(database_->workRecords(first).first().folderId,0);
        QVERIFY(database_->setWorkRecordFolder(original.id,folder));
        QVERIFY(database_->renameRecordFolder(folder,"工作区初始化"));
        {
            AppDatabase reopened;
            QVERIFY(reopened.open());
            bool found=false;
            for (const auto &project : reopened.projects()) if (project.id == first) {
                QVERIFY(project.completed);
                QCOMPARE(project.directory,QStringLiteral("D:/project"));
                found=true;
            }
            QVERIFY(found);
            QCOMPARE(reopened.workRecords(first).first().folderId,folder);
            QCOMPARE(reopened.recordFolders(first).first().name,QStringLiteral("工作区初始化"));
        }
        QVERIFY(database_->deleteRecordFolder(folder));
        const auto retained=database_->workRecords(first).first();
        QCOMPARE(retained.folderId,0);
        QCOMPARE(retained.content,original.content);
        QCOMPARE(retained.createdAt,original.createdAt);
        QCOMPARE(retained.updatedAt,original.updatedAt);
        QVERIFY(database_->setProjectCompleted(first,false));
        QVERIFY(database_->addRecordFolder(first,"Temporary",&folder));
        QVERIFY(database_->setWorkRecordFolder(retained.id,folder));
        QVERIFY(database_->deleteProject(first));
        QVERIFY(database_->recordFolders(first).isEmpty());
        QVERIFY(database_->workRecords(first).isEmpty());
        QCOMPARE(database_->recordFolders(second).size(),1);
    }

    void collaborationFoldersUi()
    {
        int project=0, second=0, optimization=0;
        QVERIFY(database_->addProject(QStringLiteral("AI 协作工作台"),"",&project));
        QVERIFY(database_->addProject(QStringLiteral("已完成的界面调研"),"",&second));
        QVERIFY(database_->setProjectCompleted(second,true));
        QVERIFY(database_->setSetting("projects.last_selected",QString::number(project)));
        QVERIFY(database_->addRecordFolder(project,QStringLiteral("界面优化"),&optimization));
        QVERIFY(database_->addWorkRecord(project,QStringLiteral("通用协作要求"),sample));
        MainWindow window;
        window.show();
        button(window,QStringLiteral("项目"))->click();
        auto *page=window.findChild<ProjectsPage *>();
        auto *tree=page->findChild<QTreeWidget *>("workRecordList");
        auto *projects=page->findChild<QListWidget *>("projectList");
        auto *search=page->findChild<QLineEdit *>("recordSearch");
        QVERIFY(tree && projects && search);
        auto *add = page->findChild<QPushButton *>("addWorkRecordButton");
        QVERIFY(add && add->isVisible());
        QCOMPARE(add->x(), tree->x());
        QVERIFY(add->y() > tree->geometry().bottom());
        QCOMPARE(search->width(), tree->width());
        auto folderByName=[&](const QString &name) -> QTreeWidgetItem * {
            for (int i=0;i<tree->topLevelItemCount();++i)
                if (tree->topLevelItem(i)->data(0,Qt::UserRole+1).toBool() && tree->topLevelItem(i)->text(0)==name) return tree->topLevelItem(i);
            return nullptr;
        };
        auto showMenu=[&] { tree->customContextMenuRequested(tree->visualItemRect(tree->currentItem()).center()); };
        auto *statusTabs=page->findChild<QTabBar *>("projectStatusTabs");
        QVERIFY(statusTabs && projects->currentItem()->icon().isNull());
        QVERIFY(projects->currentItem()->data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("进行中")));
        QVERIFY(!projects->currentItem()->text().contains(QStringLiteral("进行中")));
        interactMenu([&](QMenu &menu) {
            require(menuAction(menu,QStringLiteral("进行中"))->isChecked(),"Default status missing");
            capture(menu,QStringLiteral("project-status-menu"));
            menuAction(menu,QStringLiteral("已完成"))->trigger();
        }, [&]{ projects->customContextMenuRequested(projects->visualItemRect(projects->currentItem()).center()); });
        QCOMPARE(projects->count(),0);
        statusTabs->setCurrentIndex(1);
        QCOMPARE(projects->currentItem()->data(Qt::UserRole).toInt(),project);
        QVERIFY(projects->currentItem()->icon().isNull());
        QVERIFY(projects->currentItem()->data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("已完成")));
        capture(window,QStringLiteral("project-status-tabs"));
        interactMenu([&](QMenu &menu) { menuAction(menu,QStringLiteral("进行中"))->trigger(); },
            [&]{ projects->customContextMenuRequested(projects->visualItemRect(projects->currentItem()).center()); });
        statusTabs->setCurrentIndex(0);
        interact(QStringLiteral("新建文件夹"),[&](QDialog &dialog) {
            auto *name=dialog.findChild<QLineEdit *>("recordFolderName");
            require(!button(dialog,QStringLiteral("新建"))->isEnabled(),"Empty name accepted");
            name->setText(QStringLiteral("界面优化"));
            button(dialog,QStringLiteral("新建"))->click();
            require(dialog.isVisible() && dialog.findChild<QLabel *>("folderError")->isVisible(),"Duplicate folder accepted");
            name->setText(QStringLiteral("初始化工作区"));
            button(dialog,QStringLiteral("新建"))->click();
        },[&]{ button(window,QStringLiteral("新建文件夹"))->click(); });
        auto *initialization=folderByName(QStringLiteral("初始化工作区"));
        QVERIFY(initialization && initialization->isExpanded());
        const int folder=initialization->data(0,Qt::UserRole).toInt();
        interact(QStringLiteral("添加记录"),[&](QDialog &dialog) {
            dialog.findChild<QLineEdit *>("workRecordTitle")->setText(QStringLiteral("初始化检查清单"));
            dialogEditor(dialog)->setPlainText(sample);
            saveDialog(dialog);
        },[&]{ button(window,QStringLiteral("添加记录"))->click(); });
        initialization=folderByName(QStringLiteral("初始化工作区"));
        QCOMPARE(initialization->childCount(),1);
        QCOMPARE(database_->workRecords(project).first().folderId,folder);
        QCoreApplication::processEvents();
        const QRect folderRect=tree->visualItemRect(initialization);
        QTest::mouseClick(tree->viewport(),Qt::LeftButton,Qt::NoModifier,
            QPoint(folderRect.left()-tree->indentation()/2,folderRect.center().y()));
        QVERIFY(!initialization->isExpanded());
        tree->setCurrentItem(initialization);
        QTest::keyClick(tree,Qt::Key_Return);
        QVERIFY(initialization->isExpanded());
        tree->setCurrentItem(initialization->child(0));
        initialization->setExpanded(false);
        for (auto *action : page->findChildren<QPushButton *>())
            QVERIFY(action->text() != QStringLiteral("删除"));
        search->setText(QStringLiteral("初始化检查"));
        QVERIFY(initialization->isExpanded() && !initialization->isHidden());
        QVERIFY(folderByName(QStringLiteral("界面优化"))->isHidden());
        capture(window,QStringLiteral("collaboration-search"));
        search->clear();
        QVERIFY(!initialization->isExpanded());
        initialization->setExpanded(true);
        tree->setCurrentItem(initialization->child(0));
        auto *orderedTree=static_cast<SiblingOrderTree *>(tree);
        QVERIFY(orderedTree->dropSelectionAt(tree->visualItemRect(folderByName(QStringLiteral("界面优化"))).center()));
        QCOMPARE(folderByName(QStringLiteral("界面优化"))->childCount(),1);
        QCOMPARE(database_->workRecords(project).first().folderId,optimization);
        QVERIFY(tree->currentItem()->parent()->isExpanded());
        QVERIFY(orderedTree->dropSelectionAt(QPoint(tree->viewport()->width()/2,tree->viewport()->height()-5)));
        QVERIFY(!tree->currentItem()->parent()); QCOMPARE(database_->workRecords(project).first().folderId,0);
        QVERIFY(orderedTree->transferSelectionTo(folderByName(QStringLiteral("界面优化"))));
        tree->setCurrentItem(folderByName(QStringLiteral("初始化工作区")));
        interactMenu([&](QMenu &menu) {
            interact(QStringLiteral("重命名文件夹"),[&](QDialog &dialog) {
                dialog.findChild<QLineEdit *>("recordFolderName")->setText(QStringLiteral("工作区初始化"));
                saveDialog(dialog);
            },[&]{menuAction(menu,QStringLiteral("重命名文件夹"))->trigger();});
        },showMenu);
        QVERIFY(folderByName(QStringLiteral("工作区初始化")));
        interact(QStringLiteral("添加记录"),[&](QDialog &dialog) {
            dialog.findChild<QLineEdit *>("workRecordTitle")->setText(QStringLiteral("建立项目约定"));
            dialogEditor(dialog)->setPlainText(sample);
            saveDialog(dialog);
        },[&]{ button(window,QStringLiteral("添加记录"))->click(); });
        folderByName(QStringLiteral("工作区初始化"))->setExpanded(false);
        window.resize(1180,780);
        capture(window,QStringLiteral("collaboration-folders"));
        window.resize(980,640);
        capture(window,QStringLiteral("collaboration-folders-compact"));
        search->setText(QStringLiteral("没有这条记录"));
        QVERIFY(page->findChild<QLabel *>("recordHint")->isVisible());
        QVERIFY(tree->viewport()->findChild<QLabel *>("emptyViewHint")->isVisible());
        capture(window,QStringLiteral("v3.7.0-projects-no-results"));
        search->clear();
        // Fold state survives creating a second page against the same portable store.
        folderByName(QStringLiteral("工作区初始化"))->setExpanded(false);
        ProjectsPage restored(database_.get());
        auto *restoredTree=restored.findChild<QTreeWidget *>("workRecordList");
        bool found=false;
        for (int i=0;i<restoredTree->topLevelItemCount();++i) {
            auto *item=restoredTree->topLevelItem(i);
            if (item->text(0)==QStringLiteral("工作区初始化")) { QVERIFY(!item->isExpanded()); found=true; }
        }
        QVERIFY(found);
        tree->setCurrentItem(folderByName(QStringLiteral("界面优化")));
        interactMenu([&](QMenu &menu) {
            interact(QStringLiteral("删除文件夹"),[&](QDialog &dialog) {
                auto *message=qobject_cast<QMessageBox *>(&dialog);
                require(message && message->text().contains(QStringLiteral("记录会保留")),"Record preservation not explained");
                message->button(QMessageBox::Yes)->click();
            },[&]{menuAction(menu,QStringLiteral("删除文件夹…"))->trigger();});
        },showMenu);
        QCOMPARE(database_->workRecords(project).size(),3);
        for (const auto &record : database_->workRecords(project))
            if (record.title == QStringLiteral("初始化检查清单")) QCOMPARE(record.folderId,0);
        QVERIFY(!folderByName(QStringLiteral("界面优化")));
        const auto matches = tree->findItems(QStringLiteral("初始化检查清单"), Qt::MatchExactly);
        QCOMPARE(matches.size(), 1);
        tree->setCurrentItem(folderByName(QStringLiteral("工作区初始化")));
        interactMenu([&](QMenu &menu) {
            interact(QStringLiteral("删除协作记录"), [&](QDialog &dialog) {
                auto *confirmation = qobject_cast<QMessageBox *>(&dialog);
                require(confirmation, "Missing record deletion confirmation");
                confirmation->button(QMessageBox::Yes)->click();
            }, [&]{ menuAction(menu, QStringLiteral("删除记录…"))->trigger(); });
        }, [&]{ tree->customContextMenuRequested(tree->visualItemRect(matches.first()).center()); });
        QCOMPARE(database_->workRecords(project).size(), 2);
        for (const auto &record : database_->workRecords(project))
            QVERIFY(record.title != QStringLiteral("初始化检查清单"));
        button(window,QStringLiteral("Git 历史"))->setFocus();
        QVERIFY(!page->findChild<HeadingTextEdit *>(QStringLiteral("projectOutline")));
    }

    void collaborationFolderStars()
    {
        int project=0, other=0, alpha=0, beta=0, zeta=0, otherFolder=0;
        QVERIFY(database_->addProject(QStringLiteral("重点协作资料"),"",&project));
        QVERIFY(database_->addProject(QStringLiteral("另一项目"),"",&other));
        QVERIFY(database_->addRecordFolder(project,"Alpha 常规资料",&alpha));
        QVERIFY(database_->addRecordFolder(project,"Beta 项目约定",&beta));
        QVERIFY(database_->addRecordFolder(project,"Zeta 本周重点",&zeta));
        QVERIFY(database_->addRecordFolder(other,"Other 项目资料",&otherFolder));
        QVERIFY(database_->setRecordFolderStarred(otherFolder,true));
        QVERIFY(database_->addWorkRecord(project,QStringLiteral("本周验收清单"),sample,zeta));
        const auto original=database_->workRecords(project).first();
        QVERIFY(database_->setSetting("projects.last_selected",QString::number(project)));
        const QString expandedKey=QStringLiteral("projects.%1.expanded_record_folders").arg(project);
        QVERIFY(database_->setSetting(expandedKey,QString::number(zeta)));
        MainWindow window;
        window.show();
        button(window,QStringLiteral("项目"))->click();
        auto *page=window.findChild<ProjectsPage *>();
        auto *tree=page->findChild<QTreeWidget *>("workRecordList");
        auto *search=page->findChild<QLineEdit *>("recordSearch");
        auto folderById=[&](int id) -> QTreeWidgetItem * {
            for (int i=0;i<tree->topLevelItemCount();++i) {
                auto *item=tree->topLevelItem(i);
                if (item->data(0,Qt::UserRole+1).toBool() && item->data(0,Qt::UserRole).toInt()==id) return item;
            }
            return nullptr;
        };
        auto toggle=[&](int id, bool starred) {
            tree->setCurrentItem(folderById(id));
            interactMenu([&](QMenu &menu) {
                auto *action=menuAction(menu,starred ? QStringLiteral("取消重点标记") : QStringLiteral("标记重点并置顶"));
                require(!action->icon().isNull(),"Star action icon missing");
                action->trigger();
            },[&]{tree->customContextMenuRequested(tree->visualItemRect(tree->currentItem()).center());});
        };
        QCOMPARE(tree->topLevelItem(0)->data(0,Qt::UserRole).toInt(),alpha);
        const auto ordinaryIcon=folderById(zeta)->icon(0).pixmap(16,16).toImage();
        toggle(zeta,false);
        QCOMPARE(tree->topLevelItem(0)->data(0,Qt::UserRole).toInt(),zeta);
        QCOMPARE(tree->currentItem(),folderById(zeta));
        QVERIFY(folderById(zeta)->isExpanded());
        QCOMPARE(folderById(zeta)->text(0),QStringLiteral("Zeta 本周重点"));
        QVERIFY(folderById(zeta)->toolTip(0).contains(QStringLiteral("已置顶")));
        QVERIFY(folderById(zeta)->data(0,Qt::AccessibleTextRole).toString().contains(QStringLiteral("重点文件夹")));
        QVERIFY(folderById(zeta)->icon(0).pixmap(16,16).toImage()!=ordinaryIcon);
        toggle(beta,false);
        QCOMPARE(tree->topLevelItem(0)->data(0,Qt::UserRole).toInt(),beta);
        QCOMPARE(tree->topLevelItem(1)->data(0,Qt::UserRole).toInt(),zeta);
        QCOMPARE(tree->topLevelItem(2)->data(0,Qt::UserRole).toInt(),alpha);
        QVERIFY(!folderById(beta)->isExpanded());
        QVERIFY(database_->recordFolders(other).first().starred);
        const auto retained=database_->workRecords(project).first();
        QCOMPARE(retained.content,original.content);
        QCOMPARE(retained.folderId,original.folderId);
        QCOMPARE(retained.createdAt,original.createdAt);
        QCOMPARE(retained.updatedAt,original.updatedAt);
        window.resize(1180,780);
        capture(window,QStringLiteral("collaboration-starred-folders"));
        window.resize(980,640);
        capture(window,QStringLiteral("collaboration-starred-folders-compact"));
        tree->setCurrentItem(folderById(beta));
        interactMenu([&](QMenu &menu) {
            capture(menu,QStringLiteral("collaboration-starred-folder-menu"));
        },[&]{tree->customContextMenuRequested(tree->visualItemRect(tree->currentItem()).center());});
        search->setText(QStringLiteral("本周验收"));
        QVERIFY(folderById(zeta)->isExpanded());
        QVERIFY(folderById(beta)->isHidden());
        toggle(zeta,true);
        QCOMPARE(search->text(),QStringLiteral("本周验收"));
        QVERIFY(folderById(zeta)->isExpanded() && !folderById(zeta)->isHidden());
        QVERIFY(folderById(beta)->isHidden());
        QCOMPARE(tree->currentItem(),folderById(zeta));
        search->clear();
        QVERIFY(folderById(zeta)->isExpanded());
        QVERIFY(!folderById(beta)->isExpanded());
        QCOMPARE(tree->topLevelItem(0)->data(0,Qt::UserRole).toInt(),beta);
        QCOMPARE(tree->topLevelItem(1)->data(0,Qt::UserRole).toInt(),alpha);
        toggle(beta,true);
        QCOMPARE(tree->topLevelItem(0)->data(0,Qt::UserRole).toInt(),alpha);
        QCOMPARE(tree->topLevelItem(1)->data(0,Qt::UserRole).toInt(),beta);
        QCOMPARE(tree->topLevelItem(2)->data(0,Qt::UserRole).toInt(),zeta);
        toggle(zeta,false);
        tree->setCurrentItem(folderById(zeta));
        interactMenu([&](QMenu &menu) {
            interact(QStringLiteral("重命名文件夹"),[&](QDialog &dialog) {
                auto *name=dialog.findChild<QLineEdit *>("recordFolderName");
                require(name->text()==QStringLiteral("Zeta 本周重点"),"Star leaked into folder name");
                name->setText(QStringLiteral("本周重点 & 验收"));
                saveDialog(dialog);
            },[&]{menuAction(menu,QStringLiteral("重命名文件夹"))->trigger();});
        },[&]{tree->customContextMenuRequested(tree->visualItemRect(tree->currentItem()).center());});
        QVERIFY(database_->recordFolders(project).first().starred);
        QCOMPARE(tree->topLevelItem(0)->text(0),QStringLiteral("本周重点 & 验收"));
        {
            AppDatabase reopened;
            QVERIFY(reopened.open());
            ProjectsPage restored(&reopened);
            const auto *restoredTree=restored.findChild<QTreeWidget *>("workRecordList");
            QCOMPARE(restoredTree->topLevelItem(0)->data(0,Qt::UserRole).toInt(),zeta);
            QVERIFY(restoredTree->topLevelItem(0)->toolTip(0).contains(QStringLiteral("已置顶")));
        }
        const QString connection=QStringLiteral("folder-star-failure");
        {
            auto sql=QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),connection);
            sql.setDatabaseName(database_->databasePath());
            QVERIFY(sql.open());
            QSqlQuery query(sql);
            QVERIFY(query.exec("CREATE TRIGGER fail_folder_star BEFORE UPDATE OF starred ON project_record_folders BEGIN SELECT RAISE(ABORT,'star write blocked'); END"));
            tree->setCurrentItem(folderById(zeta));
            interactMenu([&](QMenu &menu) {
                interact(QStringLiteral("数据操作失败"),[&](QDialog &dialog) {
                    auto *message=qobject_cast<QMessageBox *>(&dialog);
                    require(message && message->text().contains("star write blocked"),"Star save failure hidden");
                    message->button(QMessageBox::Ok)->click();
                },[&]{menuAction(menu,QStringLiteral("取消重点标记"))->trigger();});
            },[&]{tree->customContextMenuRequested(tree->visualItemRect(tree->currentItem()).center());});
            QVERIFY(query.exec("DROP TRIGGER fail_folder_star"));
        }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(database_->recordFolders(project).first().starred);
        QCOMPARE(tree->topLevelItem(0),folderById(zeta));
        QVERIFY(folderById(zeta)->toolTip(0).contains(QStringLiteral("已置顶")));
    }

    void projectStatusTabsUi()
    {
        QList<int> projectIds;
        for (const auto &name : {QStringLiteral("Orchestrate 日常工作台"), QStringLiteral("已交付的界面调研"),
                QStringLiteral("文献阅读与实验记录——长项目名称在窄栏中省略"), QStringLiteral("归档资料整理")}) {
            int id=0; QVERIFY(database_->addProject(name,"",&id)); projectIds.append(id);
        }
        const int a=projectIds[0], b=projectIds[1], c=projectIds[2], d=projectIds[3];
        QVERIFY(database_->setProjectCompleted(b,true));
        QVERIFY(database_->setProjectCompleted(d,true));
        QVERIFY(database_->reorderProjects(projectIds));
        QVERIFY(database_->setSetting("projects.last_selected",QString::number(b)));
        QVERIFY(database_->addWorkRecord(a,QStringLiteral("开发约定"),sample));
        MainWindow window; window.resize(1180,760); window.show();
        button(window,QStringLiteral("项目"))->click();
        auto *page=window.findChild<ProjectsPage *>();
        auto *tabs=page->findChild<QTabBar *>("projectStatusTabs");
        auto *list=page->findChild<QListWidget *>("projectList");
        auto *title=page->findChild<QLabel *>("objectTitle");
        auto *tree=page->findChild<QTreeWidget *>("workRecordList");
        QVERIFY(tabs && list && title && tree);
        auto visibleIds=[&] { QList<int> ids; for (int i=0;i<list->count();++i) ids.append(list->item(i)->data(Qt::UserRole).toInt()); return ids; };
        auto storedIds=[&] { QList<int> ids; for (const auto &p:database_->projects()) ids.append(p.id); return ids; };
        auto setStatus=[&](int row,const QString &status) {
            interactMenu([&](QMenu &menu) { menuAction(menu,status)->trigger(); },
                [&]{ list->customContextMenuRequested(list->visualItemRect(list->item(row)).center()); });
        };
        QCOMPARE(tabs->count(),2); QCOMPARE(tabs->currentIndex(),0);
        QCOMPARE(tabs->tabText(0),QStringLiteral("进行中"));
        QCOMPARE(tabs->tabText(1),QStringLiteral("已完成"));
        QVERIFY(!tabs->tabIcon(0).isNull() && !tabs->tabIcon(1).isNull());
        QCOMPARE(visibleIds(),(QList<int>{a,c}));
        for (int i=0;i<list->count();++i) QVERIFY(list->item(i)->icon().isNull());
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toInt(),a);
        QCOMPARE(tree->topLevelItemCount(),1);
        capture(window,"v3.6.3-projects-active");
        list->setCurrentRow(1);
        QTest::keyClick(list,Qt::Key_Up,Qt::ControlModifier);
        QCOMPARE(visibleIds(),(QList<int>{c,a}));
        QCOMPARE(storedIds(),(QList<int>{c,b,a,d}));
        tabs->setFocus(); QTest::keyClick(tabs,Qt::Key_Right);
        QCOMPARE(tabs->currentIndex(),1); QCOMPARE(visibleIds(),(QList<int>{b,d}));
        list->setCurrentRow(1);
        // Exercise the same drop path used by native mouse dragging.
        auto *ordered=static_cast<SiblingOrderList *>(list);
        QVERIFY(ordered->dropSelectionAt(list->visualItemRect(list->item(0)).topLeft()+QPoint(12,2)));
        QCOMPARE(visibleIds(),(QList<int>{d,b}));
        QCOMPARE(storedIds(),(QList<int>{c,d,a,b}));
        capture(window,"v3.6.3-projects-completed");
        tabs->setCurrentIndex(0);
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toInt(),c);
        window.resize(980,640); capture(window,"v3.6.3-projects-compact");
        QVERIFY(tabs->rect().contains(tabs->tabRect(0)) && tabs->rect().contains(tabs->tabRect(1)));
        tabs->setCurrentIndex(1);
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toInt(),d);
        tabs->setCurrentIndex(0);
        // Changing an unselected project preserves the selected one and details.
        setStatus(1,QStringLiteral("已完成"));
        QCOMPARE(visibleIds(),(QList<int>{c}));
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toInt(),c);
        QCOMPARE(title->text(),list->currentItem()->text());
        setStatus(0,QStringLiteral("已完成"));
        QCOMPARE(list->count(),0); QVERIFY(!list->currentItem());
        QCOMPARE(tree->topLevelItemCount(),0); QVERIFY(title->text().isEmpty());
        QVERIFY(!button(window,QStringLiteral("新建文件夹"))->isEnabled());
        auto *hint=list->findChild<QLabel *>("emptyViewHint");
        QVERIFY(hint && hint->isVisible() && hint->text().contains(QStringLiteral("暂无进行中")));
        capture(window,"v3.6.3-projects-empty-active");
        tabs->setCurrentIndex(1);
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toInt(),c);
        setStatus(0,QStringLiteral("进行中"));
        tabs->setCurrentIndex(0);
        QCOMPARE(visibleIds(),(QList<int>{c}));
        tabs->setCurrentIndex(1);
        interact(QStringLiteral("新建项目"),[&](QDialog &dialog) {
            dialog.findChild<QLineEdit *>("projectName")->setText(QStringLiteral("新项目默认进行中"));
            button(dialog,QStringLiteral("新建"))->click();
        },[&]{button(window,QStringLiteral("新建项目"))->click();});
        QCOMPARE(tabs->currentIndex(),0); QCOMPARE(list->count(),2);
        QCOMPARE(list->currentItem()->text(),QStringLiteral("新项目默认进行中"));
        const int created=list->currentItem()->data(Qt::UserRole).toInt();
        // Failed status and ordering writes must not move rows or change selection.
        const QString connection="project-status-tab-failure";
        {
            auto sql=QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(database_->databasePath()); QVERIFY(sql.open());
            QSqlQuery query(sql);
            QVERIFY(query.exec("CREATE TRIGGER fail_status BEFORE UPDATE OF completed ON projects BEGIN SELECT RAISE(ABORT,'status blocked'); END"));
            interactMenu([&](QMenu &menu) {
                interact(QStringLiteral("数据操作失败"),[&](QDialog &dialog) {
                    auto *message=qobject_cast<QMessageBox *>(&dialog); require(message && message->text().contains("status blocked"),"Status failure hidden");
                    message->button(QMessageBox::Ok)->click();
                },[&]{menuAction(menu,QStringLiteral("已完成"))->trigger();});
            },[&]{list->customContextMenuRequested(list->visualItemRect(list->currentItem()).center());});
            QCOMPARE(visibleIds(),(QList<int>{c,created}));
            QCOMPARE(list->currentItem()->data(Qt::UserRole).toInt(),created);
            QVERIFY(query.exec("DROP TRIGGER fail_status"));
            QVERIFY(query.exec("CREATE TRIGGER fail_order BEFORE INSERT ON app_settings WHEN NEW.key='order.projects' BEGIN SELECT RAISE(ABORT,'order blocked'); END"));
            const auto orderBefore=storedIds();
            interact(QStringLiteral("数据操作失败"),[&](QDialog &dialog) {
                auto *message=qobject_cast<QMessageBox *>(&dialog); require(message && message->text().contains("order blocked"),"Order failure hidden");
                message->button(QMessageBox::Ok)->click();
            },[&]{QTest::keyClick(list,Qt::Key_Up,Qt::ControlModifier);});
            QCOMPARE(visibleIds(),(QList<int>{c,created})); QCOMPARE(storedIds(),orderBefore);
            QCOMPARE(list->currentItem()->data(Qt::UserRole).toInt(),created);
            QVERIFY(query.exec("DROP TRIGGER fail_order"));
        }
        QSqlDatabase::removeDatabase(connection);
        tabs->setCurrentIndex(1);
        while (list->count()) setStatus(0,QStringLiteral("进行中"));
        QVERIFY(hint->isVisible()); QCOMPARE(hint->text(),QStringLiteral("暂无已完成项目"));
        capture(window,"v3.6.3-projects-empty-completed");
        AppDatabase reopened; QVERIFY(reopened.open()); ProjectsPage restored(&reopened);
        QCOMPARE(restored.findChild<QTabBar *>("projectStatusTabs")->currentIndex(),0);
        QCOMPARE(restored.findChild<QListWidget *>("projectList")->count(),5);
        QCOMPARE(storedIds(),(QList<int>{c,d,a,b,created}));
    }

    void projectDirectoryDoubleClick()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString firstPath = folder.filePath(QStringLiteral("项目 中文 #100%"));
        const QString secondPath = folder.filePath(QStringLiteral("另一项目"));
        QVERIFY(QDir().mkpath(firstPath));
        QVERIFY(QDir().mkpath(secondPath));
        int first = 0, second = 0;
        QVERIFY(database_->addProject(QStringLiteral("项目一"), firstPath, &first));
        QVERIFY(database_->addProject(QStringLiteral("项目二"), secondPath, &second));
        QVERIFY(database_->setSetting("projects.last_selected", QString::number(first)));
        const QString logPath = folder.filePath("opened.txt");
        qputenv("ORCHESTRATE_TEST_DIRECTORY_LOG", logPath.toUtf8());
        const auto resetHandler = qScopeGuard([] { qunsetenv("ORCHESTRATE_TEST_DIRECTORY_LOG"); });
        const auto openedPaths = [logPath] {
            QFile file(logPath);
            if (!file.open(QIODevice::ReadOnly)) return QStringList();
            return QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
        };
        MainWindow window;
        window.show();
        button(window, QStringLiteral("项目"))->click();
        auto *list = window.findChild<QListWidget *>(QStringLiteral("projectList"));
        const auto findItem = [list](int id) -> QListWidgetItem * {
            for (int row = 0; row < list->count(); ++row)
                if (list->item(row)->data(Qt::UserRole).toInt() == id) return list->item(row);
            return nullptr;
        };
        for (const auto id : {second, first}) {
            auto *item = findItem(id);
            QVERIFY(item);
            const QPoint position = list->visualItemRect(item).center();
            const int count = openedPaths().size();
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, position);
            QCOMPARE(openedPaths().size(), count);
            QCOMPARE(list->currentItem()->data(Qt::UserRole).toInt(), id);
            QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, position);
            QTRY_COMPARE(openedPaths().size(), count + 1);
            QCOMPARE(openedPaths().last(), id == first ? firstPath : secondPath);
            QTRY_COMPARE(FileAccess::queue()->running, 0);
        }
        // Changing the directory refreshes the target used by the existing list.
        // Right-clicking another row must edit that project, not the selected one.
        list->setCurrentItem(findItem(second));
        interact(QStringLiteral("项目设置"), [&](QDialog &dialog) {
            require(dialog.findChild<QLineEdit *>(QStringLiteral("projectDirectory"))->text() == firstPath,
                    QStringLiteral("Context settings edited the wrong project"));
            dialog.findChild<QLineEdit *>(QStringLiteral("projectDirectory"))->setText(secondPath);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        }, [&] { openProjectSettings(window, findItem(first)); });
        auto *item = findItem(first);
        QVERIFY(item);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(item).center());
        QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(item).center());
        QTRY_COMPARE(openedPaths().size(), 3);
        QCOMPARE(openedPaths().last(), secondPath);
    }
    void longSingleLinePreservesContentAndUndo()
    {
        HeadingTextEdit editor;
        editor.resize(700, 500); editor.show();
        auto *source = editor.findChild<QPlainTextEdit *>("headingSource");
        const QString text = QStringLiteral("# Header\n") + QString(1024 * 1024, 'x') + QString::fromUtf8("中文😀");
        QElapsedTimer timer; timer.start();
        editor.setPlainText(text);
        QCoreApplication::processEvents(); qInfo() << "long-load-ms" << timer.elapsed();
        QVERIFY2(timer.elapsed() < 2000, "Loading a long line blocked the UI");
        QCOMPARE(editor.toPlainText(), text);
        timer.restart(); editor.setPreviewMode(true);
        QCoreApplication::processEvents(); qInfo() << "long-preview-ms" << timer.elapsed();
        QVERIFY2(timer.elapsed() < 2000, "Long-line preview blocked the UI");
        auto *preview = editor.findChild<QTextEdit *>("headingPreview");
        QVERIFY(preview->toPlainText().endsWith(text.mid(9)));
        editor.setPreviewMode(false); timer.restart(); editor.clear();
        QVERIFY2(timer.elapsed() < 2000, "Clearing a long document reflowed old text");
        QApplication::clipboard()->setText(text);
        timer.restart(); source->paste();
        QCoreApplication::processEvents(); qInfo() << "long-paste-ms" << timer.elapsed();
        QVERIFY2(timer.elapsed() < 2000, "Pasting a long line blocked the UI");
        QCOMPARE(editor.toPlainText(), text);
        timer.restart(); source->undo(); QCoreApplication::processEvents(); qInfo() << "long-undo-ms" << timer.elapsed(); QVERIFY(timer.elapsed()<2000); QVERIFY(editor.toPlainText().isEmpty());
        timer.restart(); source->redo(); QCoreApplication::processEvents(); qInfo() << "long-redo-ms" << timer.elapsed(); QVERIFY(timer.elapsed()<2000); QCOMPARE(editor.toPlainText(), text);
        timer.restart(); editor.setTextPointSize(12); QCoreApplication::processEvents(); qInfo() << "long-zoom-ms" << timer.elapsed(); QVERIFY(timer.elapsed()<2000);
        QCOMPARE(editor.toPlainText(), text);
        editor.setPreviewMode(true);
        timer.restart(); editor.setPlainText("short replacement"); QCoreApplication::processEvents();
        QVERIFY2(timer.elapsed()<2000,"Replacing a long preview reflowed old text");
        editor.setPreviewMode(false);
        editor.setPlainText(QString(20000, 'a') + '\n' + QString(20000, 'b'));
        auto cursor = source->textCursor(); cursor.setPosition(0); cursor.setPosition(20001, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        QCOMPARE(source->lineWrapMode(), QPlainTextEdit::NoWrap);
        editor.setPlainText("Ordinary text continues wrapping at word boundaries.");
        QCOMPARE(source->lineWrapMode(), QPlainTextEdit::WidgetWidth);
        editor.clear();
        const QString ordinaryLines = QString("ordinary paragraph\n").repeated(1000);
        QApplication::clipboard()->setText(ordinaryLines);
        source->paste();
        QCOMPARE(editor.toPlainText(), ordinaryLines);
        QCOMPARE(source->lineWrapMode(), QPlainTextEdit::WidgetWidth);
    }
    void projectDirectoryMissingOrNotFolder()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QFile file(folder.filePath(QStringLiteral("普通文件.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        int empty = 0, missing = 0, notFolder = 0;
        QVERIFY(database_->addProject(QStringLiteral("未设置目录"), QString(), &empty));
        QVERIFY(database_->addProject(QStringLiteral("目录已移走"), folder.filePath("missing"), &missing));
        QVERIFY(database_->addProject(QStringLiteral("路径是文件"), file.fileName(), &notFolder));
        DirectoryUrlRecorder opened;
        QDesktopServices::setUrlHandler("file", &opened, "record");
        const auto resetHandler = qScopeGuard([] { QDesktopServices::unsetUrlHandler("file"); });
        MainWindow window;
        window.show();
        button(window, QStringLiteral("项目"))->click();
        auto *list = window.findChild<QListWidget *>(QStringLiteral("projectList"));
        for (int row = 0; row < list->count(); ++row) {
            auto *item = list->item(row);
            const QPoint position = list->visualItemRect(item).center();
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, position);
            const auto open = [&] { QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, position); };
            if (item->data(Qt::UserRole).toInt() == empty) {
                open();
                QVERIFY(!QApplication::activeModalWidget());
            } else {
                interact(QStringLiteral("无法打开项目目录"), [](QDialog &dialog) {
                    auto *message = qobject_cast<QMessageBox *>(&dialog);
                    require(message && message->text().contains(QStringLiteral("项目设置")), "Missing folder guidance");
                    message->accept();
                }, open);
            }
        }
        QVERIFY(opened.urls.isEmpty());
        QCOMPARE(database_->projects().size(), 3);
        QVERIFY(QFileInfo::exists(file.fileName()));
    }

    void projectDeletionRequiresExactConfirmation()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QFile source(folder.filePath(QStringLiteral("keep.txt")));
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("real project files stay untouched");
        source.close();
        const QString name = QStringLiteral("测试项目 <b>甲</b>（研发）");
        const QString required = QStringLiteral("我确认删除（%1）").arg(name);
        int id = 0, other = 0;
        QVERIFY(database_->addProject(name, folder.path(), &id));
        QVERIFY(database_->addWorkRecord(id, QStringLiteral("随项目删除的记录")));
        QVERIFY(database_->addProject(QStringLiteral("保留项目"), QString(), &other));
        QVERIFY(database_->setSetting("projects.last_selected", QString::number(id)));
        MainWindow window;
        window.show();
        button(window, QStringLiteral("项目"))->click();
        auto *page = window.findChild<ProjectsPage *>();
        QVERIFY(!page->findChild<QPushButton *>(QStringLiteral("deleteProjectButton")));
        capture(window, QStringLiteral("projects-delete-hidden"));
        interact(QStringLiteral("项目设置"), [&](QDialog &settings) {
            auto *remove = settings.findChild<QPushButton *>(QStringLiteral("deleteProjectButton"));
            require(remove && remove->isVisible() && !remove->autoDefault() && !remove->isDefault(),
                    "Delete entry must be in settings and must not be the default action");
            capture(settings, QStringLiteral("project-settings-delete"));
            auto *nameInput = settings.findChild<QLineEdit *>(QStringLiteral("projectName"));
            nameInput->setText(QStringLiteral("尚未保存的新名称"));
            interact(QStringLiteral("确认删除项目"), [&](QDialog &dialog) {
                require(dialog.parentWidget() == &settings, "Confirmation must belong to project settings");
                auto *input = dialog.findChild<QLineEdit *>(QStringLiteral("deleteProjectConfirmation"));
                auto *buttons = dialog.findChild<QDialogButtonBox *>();
                auto *confirm = buttons->button(QDialogButtonBox::Ok);
                require(input && input->text().isEmpty() && !confirm->isEnabled(), "Initial confirmation must be empty");
                for (const auto &wrong : {QStringLiteral("我确认删除"), QStringLiteral("我确认删除（保留项目）"),
                                          required + QStringLiteral(" "), QStringLiteral("我确认删除(%1)").arg(name)}) {
                    input->setText(wrong);
                    require(!confirm->isEnabled(), "Incorrect confirmation was accepted");
                    QMetaObject::invokeMethod(buttons, "accepted", Qt::DirectConnection);
                    require(dialog.isVisible() && database_->projects().size() == 2, "Guard was bypassed");
                }
                input->setText(required);
                require(confirm->isEnabled(), "Exact project name should enable confirmation");
                capture(dialog, QStringLiteral("project-delete-confirmation"));
                buttons->button(QDialogButtonBox::Cancel)->click();
            }, [&] { QTest::mouseClick(remove, Qt::LeftButton); });
            require(settings.isVisible() && nameInput->text() == QStringLiteral("尚未保存的新名称"),
                    "Cancelling deletion must preserve unfinished settings");
            settings.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        }, [&] { openProjectSettings(window); });
        QCOMPARE(database_->projects().size(), 2);
        QCOMPARE(database_->workRecords(id).size(), 1);
        interact(QStringLiteral("项目设置"), [&](QDialog &settings) {
            require(settings.findChild<QLineEdit *>(QStringLiteral("projectName"))->text() == name,
                    "Cancelling settings must not rename the project");
            auto *remove = settings.findChild<QPushButton *>(QStringLiteral("deleteProjectButton"));
            interact(QStringLiteral("确认删除项目"), [&](QDialog &dialog) {
                auto *input = dialog.findChild<QLineEdit *>(QStringLiteral("deleteProjectConfirmation"));
                require(input->text().isEmpty(), "Reopened dialog retained confirmation");
                input->setText(required);
                dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            }, [&] { QTest::mouseClick(remove, Qt::LeftButton); });
            require(!settings.isVisible(), "Deleting the project must also close its settings");
        }, [&] { openProjectSettings(window); });
        QCOMPARE(database_->projects().size(), 1);
        QCOMPARE(database_->projects().first().id, other);
        QVERIFY(database_->workRecords(id).isEmpty());
        QVERIFY(QFileInfo::exists(source.fileName()));
    }


    void databaseInitializationFailureDisablesEditing()
    {
        database_.reset();
        QFile corrupt(fixture(QStringLiteral("data/orchestrate.sqlite3")));
        QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
        corrupt.write("not a sqlite database");
        corrupt.close();
        std::unique_ptr<MainWindow> window;
        interact(QStringLiteral("数据库初始化失败"), [&](QDialog &dialog) { dialog.accept(); },
                 [&] { window = std::make_unique<MainWindow>(); });
        QVERIFY(window->findChild<QLabel *>(QStringLiteral("databaseUnavailable")));
        QVERIFY(window->findChildren<ProjectsPage *>().isEmpty());
        QVERIFY(window->findChildren<CalendarPage *>().isEmpty());
        QVERIFY(window->findChildren<HeadingTextEdit *>().isEmpty());
        AppDatabase unavailable;
        QString error;
        QVERIFY(!unavailable.open(&error));
        QVERIFY(!unavailable.isOpen());
    }

    void projectAndWorkRoundTrip()
    {
        MainWindow window;
        window.show();
        button(window, QStringLiteral("项目"))->click();
        auto *page=window.findChild<ProjectsPage *>();
        QVERIFY(page);
        QVERIFY(!page->findChild<QLineEdit *>(QStringLiteral("projectName")));
        interact(QStringLiteral("新建项目"), [&](QDialog &dialog) {
            require(!dialog.findChild<QPushButton *>(QStringLiteral("deleteProjectButton")), "New projects must not offer deletion");
            require(!button(dialog,QStringLiteral("新建"))->isEnabled(),QStringLiteral("Blank project enabled"));
            dialog.findChild<QLineEdit *>(QStringLiteral("projectName"))->setText(QStringLiteral("# 项目名称仍是普通字段"));
            button(dialog,QStringLiteral("新建"))->click();
        }, [&]{ button(*page,QStringLiteral("新建项目"))->click(); });
        auto projects=database_->projects();
        QCOMPARE(projects.size(),1);
        QVERIFY(projects.first().directory.isEmpty());
        const int id=projects.first().id;
        interact(QStringLiteral("新建项目"), [&](QDialog &dialog) {
            dialog.findChild<QLineEdit *>(QStringLiteral("projectName"))->setText(QStringLiteral("取消的新项目"));
            button(dialog,QStringLiteral("取消"))->click();
        }, [&]{button(*page,QStringLiteral("新建项目"))->click();});
        QCOMPARE(database_->projects().size(),1);
        interact(QStringLiteral("项目设置"), [&](QDialog &dialog) {
            dialog.findChild<QLineEdit *>(QStringLiteral("projectDirectory"))->setText(QStringLiteral("D:/optional/nonexistent"));
            saveDialog(dialog);
        }, [&]{openProjectSettings(window);});
        QCOMPARE(database_->projects().first().directory,QStringLiteral("D:/optional/nonexistent"));
        const QString raw=QStringLiteral("  缩进原文\n")+sample+QStringLiteral("\n");
        QVERIFY(page->findChild<QPushButton *>(QStringLiteral("gitHistoryButton")));
        QVERIFY(!page->findChild<HeadingTextEdit *>(QStringLiteral("projectOutline")));
        interact(QStringLiteral("添加记录"), [&](QDialog &dialog) {
            require(!dialogEditor(dialog)->isPreviewMode(),QStringLiteral("New record must start in edit"));
            auto *title = dialog.findChild<QLineEdit *>(QStringLiteral("workRecordTitle"));
            auto *summary = dialog.findChild<HeadingTextEdit *>(QStringLiteral("recordSummaryEditor"));
            require(title && title->accessibleName() == QStringLiteral("标题"), QStringLiteral("Record title needs an accessible name"));
            require(summary && title->mapTo(&dialog, QPoint()).x() == summary->mapTo(&dialog, QPoint()).x(),
                    QStringLiteral("Record title must align with the editor's left edge"));
            dialogEditor(dialog)->setPlainText(sample);
            saveDialog(dialog); require(!static_cast<CollaborationRecordDialog &>(dialog).hasSaved(),QStringLiteral("Missing title allowed"));
            title->setText(QStringLiteral("第一阶段总结"));
            capture(dialog,QStringLiteral("work-record-editor"));
            saveDialog(dialog);
        }, [&]{button(*page,QStringLiteral("添加记录"))->click();});
        auto records=database_->workRecords(id);
        QCOMPARE(records.size(),1);
        QCOMPARE(records.first().title,QStringLiteral("第一阶段总结"));
        QCOMPARE(records.first().content,sample);
        auto *list=page->findChild<QTreeWidget *>(QStringLiteral("workRecordList"));
        QCOMPARE(list->topLevelItem(0)->text(0),QStringLiteral("第一阶段总结"));
        list->setCurrentItem(list->topLevelItem(0));
        interact(QStringLiteral("编辑记录"), [&](QDialog &dialog) {
            require(dialogEditor(dialog)->toPlainText()==sample,QStringLiteral("Body changed"));
            auto *title=dialog.findChild<QLineEdit *>(QStringLiteral("workRecordTitle"));
            require(!title->isReadOnly(),QStringLiteral("Title must be directly editable"));
            saveDialog(dialog); require(!static_cast<CollaborationRecordDialog &>(dialog).hasSaved(),QStringLiteral("Unchanged record was rewritten"));
            capture(dialog,QStringLiteral("work-record-preview"));
            enterEdit(dialogEditor(dialog));
            require(!title->isReadOnly(),QStringLiteral("Summary did not unlock"));
            dialogEditor(dialog)->setPlainText(QStringLiteral("## 不保存"));
            interact(QStringLiteral("未保存的修改"), [&](QDialog &prompt) { button(prompt, QStringLiteral("放弃修改"))->click(); }, [&] { dialog.reject(); });
        }, [&]{
            QCoreApplication::processEvents();
            const QPoint point=list->visualItemRect(list->topLevelItem(0)).center();
            QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,point);
            QTest::mouseDClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,point);
        });
        QCOMPARE(database_->workRecords(id).first().content,sample);
        interact(QStringLiteral("编辑记录"), [&](QDialog &dialog) {
            enterEdit(dialogEditor(dialog));
            dialogEditor(dialog)->setPlainText(raw);
            dialog.findChild<QLineEdit *>(QStringLiteral("workRecordTitle"))->setText(QStringLiteral("已更新简介"));
            saveDialog(dialog);
        }, [&]{list->itemDoubleClicked(list->topLevelItem(0),0);});
        QCOMPARE(database_->workRecords(id).first().content,raw);
        QCOMPARE(list->topLevelItem(0)->text(0),QStringLiteral("已更新简介"));
        auto *search=page->findChild<QLineEdit *>(QStringLiteral("recordSearch"));
        search->setText(QStringLiteral("不存在"));
        QVERIFY(list->topLevelItem(0)->isHidden());
        search->clear();
        QVERIFY(!list->topLevelItem(0)->isHidden());
        QVERIFY(!page->findChild<QPushButton *>(QStringLiteral("detachRecords")));
        interact(QStringLiteral("添加记录"), [&](QDialog &dialog) {
            dialog.findChild<QLineEdit *>(QStringLiteral("workRecordTitle"))->setText(QStringLiteral("第二条记录"));
            dialogEditor(dialog)->setPlainText(sample);
            saveDialog(dialog);
        }, [&]{ button(*page,QStringLiteral("添加记录"))->click(); });
        QCOMPARE(list->topLevelItemCount(),2);
        QVERIFY(list->window()==&window);
        QCOMPARE(list->topLevelItemCount(),2);
        capture(window,QStringLiteral("project-headings"));
        window.resize(980,640);
        capture(window,QStringLiteral("project-compact"));
        auto *projectList=page->findChild<QListWidget *>(QStringLiteral("projectList"));
        QVERIFY(projectList->visualItemRect(projectList->item(0)).height()>=44);
        AppDatabase reopened;
        QString error;
        QVERIFY2(reopened.open(&error),qPrintable(error));
        QCOMPARE(reopened.projects().first().targetType,QStringLiteral("local"));
        QCOMPARE(reopened.workRecords(id).size(),2);
        MainWindow restored;
        auto *restoredPage=restored.findChild<ProjectsPage *>();
        QVERIFY(!restoredPage->findChild<HeadingTextEdit *>(QStringLiteral("projectOutline")));
    }

    void legacyWorkTitles()
    {
        int id=0;
        QString error;
        QVERIFY(database_->addProject(QStringLiteral("旧项目"),QStringLiteral(""),&id,&error));
        const QString raw=QStringLiteral("\n  旧记录简介\n## 正文保持\n内容\n");
        QVERIFY(database_->addWorkRecord(id,raw,&error));
        MainWindow window;
        auto *list=window.findChild<QTreeWidget *>(QStringLiteral("workRecordList"));
        QVERIFY(list && list->topLevelItemCount()==1);
        QCOMPARE(list->topLevelItem(0)->text(0),QStringLiteral("旧记录简介"));
        QCOMPARE(database_->workRecords(id).first().content,raw);
        QVERIFY(database_->workRecords(id).first().title.isEmpty());
    }

    void workTitleMigrationAndProjectSwitching()
    {
        int first=0, second=0;
        QString error;
        QVERIFY(database_->addProject(QStringLiteral("旧项目"),QStringLiteral(""),&first,&error));
        QVERIFY(database_->addWorkRecord(first,QStringLiteral("旧正文\n完整保留"),&error));
        const auto before=database_->workRecords(first).first();
        // Reconstruct the prior schema only inside this test's isolated database.
        const QString connection=QStringLiteral("project-migration-fixture");
        {
            auto sql=QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),connection);
            sql.setDatabaseName(database_->databasePath());
            QVERIFY(sql.open());
            QSqlQuery query(sql);
            QVERIFY2(query.exec(QStringLiteral("ALTER TABLE project_work_records DROP COLUMN title")),qPrintable(query.lastError().text()));
            QVERIFY(query.exec(QStringLiteral("ALTER TABLE automation_tools DROP COLUMN customized")));
            QVERIFY(query.exec(QStringLiteral("ALTER TABLE automation_tools DROP COLUMN environment")));
            QVERIFY(query.exec(QStringLiteral("ALTER TABLE tool_commands DROP COLUMN enabled")));
            QVERIFY(query.exec(QStringLiteral("ALTER TABLE tool_commands DROP COLUMN position")));
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 0")));
        }
        QSqlDatabase::removeDatabase(connection);
        AppDatabase migrated;
        QVERIFY2(migrated.open(&error),qPrintable(error));
        const auto after=migrated.workRecords(first).first();
        QCOMPARE(after.id,before.id);
        QCOMPARE(after.content,before.content);
        QCOMPARE(after.createdAt,before.createdAt);
        QCOMPARE(after.updatedAt,before.updatedAt);
        QVERIFY(after.title.isEmpty());
        QVERIFY(migrated.updateWorkRecord(after.id,QStringLiteral("新标题"),after.content,&error));
        QVERIFY(migrated.updateWorkRecord(after.id,QStringLiteral("兼容旧正文接口"),&error));
        QCOMPARE(migrated.workRecords(first).first().title,QStringLiteral("新标题"));
        QVERIFY(migrated.addProject(QStringLiteral("第二项目"),QStringLiteral(""),&second,&error));
        QVERIFY(migrated.setSetting(QStringLiteral("projects.last_selected"),QString::number(first),&error));
        QVERIFY(migrated.setSetting(QStringLiteral("project.%1.outline_expanded").arg(first),QStringLiteral("false"),&error));
        MainWindow window;
        window.show();
        button(window,QStringLiteral("项目"))->click();
        auto *list=window.findChild<QListWidget *>(QStringLiteral("projectList"));
        QVERIFY(!window.findChild<HeadingTextEdit *>(QStringLiteral("projectOutline")));
        for(int i=0;i<list->count();++i) {
            if(list->item(i)->data(Qt::UserRole).toInt()!=second) continue;
            const QRect rect=list->visualItemRect(list->item(i));
            QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(rect.right()-10,rect.center().y()));
        }
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toInt(),second);
        QVERIFY(window.findChild<QPushButton *>(QStringLiteral("gitHistoryButton")));
    }
    void reminderPersistence_data()
    {
        QTest::addColumn<QString>("content");
        QTest::newRow("without-description") << QString();
        QTest::newRow("with-description") << sample;
    }
    void reminderPersistence()
    {
        QFETCH(QString,content);
        AppDatabase::Reminder reminder;
        reminder.title=QStringLiteral("提交材料");
        reminder.content=content;
        reminder.remindAt=QDateTime(QDate::currentDate().addDays(2),QTime(10,30));
        QString error;
        QVERIFY2(database_->addReminder(reminder,&reminder.id,&error),qPrintable(error));
        auto saved=database_->reminders(reminder.remindAt.date()).first();
        QCOMPARE(saved.content,content);
        QCOMPARE(saved.projectId,0);
        QVERIFY(!saved.lastNotifiedAt.isValid());
        reminder.title=QStringLiteral("修改后的提醒");
        reminder.content=QString();
        reminder.repeatMode=QStringLiteral("daily");
        QVERIFY2(database_->updateReminder(reminder,&error),qPrintable(error));
        AppDatabase reopened;
        QVERIFY2(reopened.open(&error),qPrintable(error));
        saved=reopened.reminders(reminder.remindAt.date()).first();
        QCOMPARE(saved.title,reminder.title);
        QVERIFY(saved.content.isEmpty());
        QVERIFY2(reopened.completeReminder(reminder.id,reminder.remindAt,&error),qPrintable(error));
        const auto next=reopened.reminders(reminder.remindAt.date().addDays(1));
        QCOMPARE(next.size(),1);
        QVERIFY(!next.first().lastNotifiedAt.isValid());
    }
    void reminderPreviewAndCancel()
    {
        const QDate date = QDate::currentDate().addDays(1);
        QString error;
        AppDatabase::Reminder reminder;
        reminder.title=QStringLiteral("提交资料");
        reminder.content=sample;
        reminder.remindAt=QDateTime(date,QTime(12,0));
        QVERIFY2(database_->addReminder(reminder,nullptr,&error),qPrintable(error));
        MainWindow window;
        window.show();
        auto *page = window.findChild<CalendarPage *>();
        QVERIFY(page);
        page->findChild<QCalendarWidget *>()->setSelectedDate(date);
        auto *list = page->findChild<QListWidget *>(QStringLiteral("reminderList"));
        QVERIFY(list && list->count() == 1);
        list->setCurrentRow(0);
        QTextDocument tooltip;
        tooltip.setHtml(list->item(0)->toolTip());
        QVERIFY(!tooltip.toPlainText().contains(QStringLiteral("# 今日")));
        QVERIFY(tooltip.toPlainText().contains(QStringLiteral("<b>不是粗体</b>")));
        interact(QStringLiteral("编辑提醒"), [&](QDialog &dialog) {
            auto *editor = dialogEditor(dialog);
            require(editor->toPlainText() == sample, QStringLiteral("Reminder source changed"));
            require(!editor->isPreviewMode(),QStringLiteral("Reminder not directly editable"));
            require(!dialog.findChild<QLineEdit *>(QStringLiteral("reminderTitle"))->isReadOnly(),QStringLiteral("Reminder title locked"));
            capture(dialog, QStringLiteral("reminder-preview"));
            dialog.reject();
        }, [&] { list->itemDoubleClicked(list->item(0)); });
        QCOMPARE(database_->reminders(date).first().content, sample);
    }
    void reminderUiRoundTrip()
    {
        MainWindow window;
        window.show();
        auto *page=window.findChild<CalendarPage *>();
        const QDate date=QDate::currentDate().addDays(3);
        page->findChild<QCalendarWidget *>()->setSelectedDate(date);
        interact(QStringLiteral("添加提醒"),[&](QDialog &dialog) {
            auto *editor=dialogEditor(dialog);
            require(!editor->isPreviewMode(),QStringLiteral("New reminder not editable"));
            saveDialog(dialog);
            require(dialog.findChild<QLabel *>("reminderSaveStatus")->text()==QStringLiteral("待填写标题"),QStringLiteral("Empty title can save"));
            dialog.findChild<QLineEdit *>(QStringLiteral("reminderTitle"))->setText(QStringLiteral("只填写标题的提醒"));
            saveDialog(dialog);
        },[&]{button(*page,QStringLiteral("添加提醒"))->click();});
        auto saved=database_->reminders(date);
        QCOMPARE(saved.size(),1);
        QVERIFY(saved.first().content.isEmpty());
        const int id=saved.first().id;
        auto *list=page->findChild<QListWidget *>(QStringLiteral("reminderList"));
        list->setCurrentRow(0);
        interact(QStringLiteral("编辑提醒"),[&](QDialog &dialog) {
            auto *editor=dialogEditor(dialog);
            auto *title=dialog.findChild<QLineEdit *>(QStringLiteral("reminderTitle"));
            auto *time=dialog.findChild<QDateTimeEdit *>(QStringLiteral("reminderTime"));
            auto *repeat=dialog.findChild<QComboBox *>(QStringLiteral("reminderRepeat"));
            require(!title->isReadOnly() && !time->isReadOnly() && repeat->isEnabled(),QStringLiteral("Reminder controls locked"));
            require(dialog.findChild<QLabel *>("reminderSaveStatus")->text()==QStringLiteral("已保存"),QStringLiteral("Unchanged reminder is dirty"));
            enterEdit(editor);
            require(!title->isReadOnly() && !time->isReadOnly() && repeat->isEnabled(),QStringLiteral("Reminder controls not unlocked"));
            title->setText(QStringLiteral("修改后的提醒"));
            editor->setPlainText(sample);
            repeat->setCurrentIndex(repeat->findData(QStringLiteral("daily")));
            saveDialog(dialog);
        },[&]{list->itemDoubleClicked(list->item(0));});
        saved=database_->reminders(date);
        QCOMPARE(saved.size(),1);
        QCOMPARE(saved.first().id,id);
        QCOMPARE(saved.first().title,QStringLiteral("修改后的提醒"));
        QCOMPARE(saved.first().content,sample);
        QCOMPARE(saved.first().repeatMode,QStringLiteral("daily"));
        AppDatabase reopened;
        QString error;
        QVERIFY2(reopened.open(&error),qPrintable(error));
        QCOMPARE(reopened.reminders(date).first().content,sample);
    }
    void emptyDialogCannotSave()
    {
        bool accepted = true;
        interact(QStringLiteral("测试空白正文"), [&](QDialog &dialog) {
            auto *editor = dialogEditor(dialog);
            require(!button(dialog, QStringLiteral("保存"))->isEnabled(), QStringLiteral("Empty save enabled"));
            editor->setPlainText(QStringLiteral(" \n\t"));
            require(!button(dialog, QStringLiteral("保存"))->isEnabled(), QStringLiteral("Blank save enabled"));
            button(dialog, QStringLiteral("取消"))->click();
        }, [&] { HeadingTextEdit::getText(nullptr, QStringLiteral("测试空白正文"), {}, &accepted); });
        QVERIFY(!accepted);
    }

private:
    std::unique_ptr<AppDatabase> database_;
};

int main(int argc, char **argv)
{
    if (FileAccess::isHelper(argc, argv)) {
        QCoreApplication helper(argc, argv);
        auto args = helper.arguments();
        if (args.value(2) == "open-directory") {
            args[2] = "check-directory";
            const int code = FileAccess::runHelper(args, false);
            if (code == 0) {
                QFile log(qEnvironmentVariable("ORCHESTRATE_TEST_DIRECTORY_LOG"));
                if (!log.open(QIODevice::WriteOnly | QIODevice::Append)) return 2;
                log.write(args.value(3).toUtf8() + '\n');
            }
            return code;
        }
        return FileAccess::runHelper(args, false);
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setOrganizationName(QStringLiteral("OrchestrateHeadingTests"));
    app.setApplicationName(QUuid::createUuid().toString(QUuid::Id128));
    QStandardPaths::setTestModeEnabled(true);
    if (QFileInfo(QCoreApplication::applicationDirPath()).fileName() != QStringLiteral("heading-text-test")) return 2;
    // This fixture has no automation tools; startup cannot execute/read real tools.
    if (QDir(fixture(QStringLiteral("tools"))).exists()) return 2;
    QDir().mkpath(fixture(QStringLiteral("artifacts")));
#ifdef Q_OS_WIN
    const QDir fonts(QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts")));
    for (const auto &name : {QStringLiteral("msyh.ttc"), QStringLiteral("msyhbd.ttc"), QStringLiteral("segoeui.ttf")})
        QFontDatabase::addApplicationFont(fonts.filePath(name));
    app.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
#endif
    app.setStyle(QStringLiteral("Fusion"));
    app.setWindowIcon(QIcon(QStringLiteral(":/app-icon/32.png")));
    HeadingTextTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_heading_text.moc"
