#include "mainwindow.h"
#include "platform/globalshortcut.h"
#include <QCheckBox>
#include <QToolButton>
#include <QCloseEvent>
#include <QLabel>
#include <QSystemTrayIcon>
#include "pages/calendarpage.h"
#include "pages/projectspage.h"
#include "widgets/headingtext.h"
#include "widgets/headingtextedit.h"
#include "widgets/workspacecalendar.h"

#include <QApplication>
#include <QAction>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QCalendarWidget>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTimeEdit>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
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
        if (candidate->text() == text) return candidate;
    }
    throw std::runtime_error((QStringLiteral("Missing button: ") + text).toStdString());
}
void capture(QWidget &widget, const QString &name)
{
    QCoreApplication::processEvents();
    require(widget.grab().save(fixture(QStringLiteral("artifacts/") + name + QStringLiteral(".png"))),
            QStringLiteral("Screenshot failed"));
}
void interact(const QString &title, const std::function<void(QDialog &)> &handler,
              const std::function<void()> &action)
{
    QTimer timer;
    bool handled = false;
    QString error;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        if (dialog->windowTitle() != title || handled) {
            error = QStringLiteral("Unexpected dialog: ") + dialog->windowTitle();
            dialog->reject();
            return;
        }
        handled = true;
        timer.stop(); // Allow the handler to exercise a nested confirmation dialog.
        try { handler(*dialog); }
        catch (const std::exception &failure) {
            error = QString::fromUtf8(failure.what());
            dialog->reject();
        }
    });
    timer.start(20);
    action();
    require(handled, QStringLiteral("Expected dialog was not opened"));
    require(error.isEmpty(), error);
}
HeadingTextEdit *dialogEditor(QDialog &dialog)
{
    auto *editor = dialog.findChild<HeadingTextEdit *>();
    require(editor != nullptr, QStringLiteral("Heading editor missing from dialog"));
    return editor;
}
void enterEdit(HeadingTextEdit *editor)
{
    require(editor->isPreviewMode(),QStringLiteral("Existing record did not open in preview"));
    auto *preview=editor->findChild<QTextEdit *>(QStringLiteral("headingPreview"));
    require(preview && preview->isReadOnly(),QStringLiteral("Preview is editable"));
    const QString original=editor->toPlainText();
    QTest::keyClicks(preview,"unwanted edit");
    require(editor->toPlainText()==original,QStringLiteral("Typing changed preview content"));
    auto *tabs=editor->findChild<QTabBar *>(QStringLiteral("headingModeTabs"));
    require(tabs && tabs->tabRect(0).height()>=36,QStringLiteral("Mode tabs too small"));
    QTest::mouseClick(tabs,Qt::LeftButton,Qt::NoModifier,tabs->tabRect(0).center());
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
                    if (img.pixelColor(x, y) == QColor("#3b82d6")) ++count;
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
        button(window, QStringLiteral("项目记录"))->click();
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
        QCOMPARE(calendar->dateTextFormat(QDate::currentDate()).background().color(), QColor("#60a5fa"));
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
            require(!button(dialog,QStringLiteral("保存"))->isEnabled(),QStringLiteral("Missing diary summary allowed"));
            dialog.findChild<QLineEdit *>(QStringLiteral("diaryTitle"))->setText(QStringLiteral("今天的进展"));
            editor->setPreviewMode(true);
            capture(dialog, QStringLiteral("diary-preview"));
            button(dialog, QStringLiteral("保存"))->click();
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
        interact(QStringLiteral("编辑日记"), [&](QDialog &dialog) {
            auto *editor = dialogEditor(dialog);
            require(editor->toPlainText() == raw, QStringLiteral("Stored markers/whitespace lost"));
            enterEdit(editor);
            editor->setPlainText(QStringLiteral("# 不保存"));
            button(dialog, QStringLiteral("取消"))->click();
        }, [&] { list->itemDoubleClicked(list->item(0)); });
        QCOMPARE(database_->diaryEntries(QDate::currentDate()).first().content, raw);
        interact(QStringLiteral("编辑日记"), [&](QDialog &dialog) {
            enterEdit(dialogEditor(dialog));
            dialogEditor(dialog)->setPlainText(QStringLiteral("### 修改后\n仍是文本"));
            button(dialog, QStringLiteral("保存"))->click();
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
        auto *collection=page->findChild<QDialog *>(QStringLiteral("diaryCollection"));
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
        interact(QStringLiteral("编辑日记"),[&](QDialog &dialog) {
            auto *title=dialog.findChild<QLineEdit *>(QStringLiteral("diaryTitle"));
            require(title->isReadOnly(),QStringLiteral("Diary summary editable in preview"));
            enterEdit(dialogEditor(dialog));
            title->setText(QStringLiteral("完成阶段整理"));
            dialogEditor(dialog)->setPlainText(sample);
            button(dialog,QStringLiteral("保存"))->click();
        },[&]{button(*collection,QStringLiteral("查看日记"))->click();});
        QCOMPARE(migrated.diaryEntry(later).title,QStringLiteral("完成阶段整理"));
        QVERIFY(list->item(0)->isHidden());
        search->setText(oldDate.toString(Qt::ISODate));
        QVERIFY(!list->item(2)->isHidden());
        search->clear();
        collection->close();
        button(*page,QStringLiteral("日记集合"))->click();
        QCOMPARE(page->findChildren<QDialog *>(QStringLiteral("diaryCollection")).size(),1);
        QCOMPARE(list->item(0)->data(Qt::UserRole).toInt(),later);
        collection->close();
        page->findChild<QCalendarWidget *>()->setSelectedDate(oldDate);
        auto *dayList=page->findChild<QListWidget *>(QStringLiteral("diaryList"));
        QCOMPARE(dayList->item(0)->text(),QStringLiteral("旧日记简介"));
    }

    void calendarRecordSymbolsAndFutureDates()
    {
        CalendarCellProbe probe;
        const QDate today=QDate::currentDate();
        probe.setCurrentPage(today.year(),today.month());
        const QDate future=today.addDays(1);
        probe.setCurrentPage(future.year(),future.month());
        probe.setSelectedDate(future.addDays(future.day()==1 ? 1 : -1));
        probe.setCurrentPage(future.year(),future.month());
        probe.setRecordDates({future},{future});
        const QImage both=probe.renderDate(future);
        QVERIFY(countColor(both,QColor("#168b79"))>=12);
        QVERIFY(countColor(both,QColor("#c78719"))>=8);
        QVERIFY(countColor(both,QColor("#f0f4fa"))>200);
        // Marker centers occupy fixed separate sides beneath the date number.
        QCOMPARE(both.pixelColor(33,50),QColor("#168b79"));
        QCOMPARE(both.pixelColor(45,50),QColor("#c78719"));
        probe.setSelectedDate(future);
        const auto selected=probe.renderDate(future);
        QVERIFY(countColor(selected,QColor("#c5fff1"))>=12);
        QVERIFY(countColor(selected,QColor("#ffe08a"))>=8);
        const QDate outside=QDate(future.year(),future.month(),1).addMonths(1);
        const auto outsideImage=probe.renderDate(outside);
        QCOMPARE(countColor(outsideImage,QColor("#f0f4fa")),0);
        QVERIFY(countColor(outsideImage,QColor("#b8c0cc"))>0);
        probe.setCurrentPage(today.year(),today.month());
        const auto past=probe.renderDate(today.addDays(-1));
        QCOMPARE(countColor(past,QColor("#f0f4fa")),0);
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
            button(dialog,QStringLiteral("保存"))->click();
        },[&]{button(*page,QStringLiteral("添加日记"))->click();});
        QCoreApplication::processEvents();
        QVERIFY(countColor(calendar->grab().toImage(),QColor("#c5fff1"))>=12);
        interact(QStringLiteral("添加提醒"),[&](QDialog &dialog) {
            dialog.findChild<QLineEdit *>(QStringLiteral("reminderTitle"))->setText(QStringLiteral("今天的提醒"));
            dialog.findChild<QDateTimeEdit *>(QStringLiteral("reminderTime"))->setDateTime(QDateTime(date,QTime(23,59)));
            button(dialog,QStringLiteral("保存"))->click();
        },[&]{button(*page,QStringLiteral("添加提醒"))->click();});
        QCoreApplication::processEvents();
        const auto marked=calendar->grab().toImage();
        QVERIFY(countColor(marked,QColor("#c5fff1"))>=12);
        QVERIFY(countColor(marked,QColor("#ffe08a"))>=8);
        capture(window,QStringLiteral("calendar-record-markers"));
        button(*page,QStringLiteral("日记集合"))->click();
        auto *collection=page->findChild<QDialog *>(QStringLiteral("diaryCollection"));
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
        QCoreApplication::processEvents();
        const auto removed=calendar->grab().toImage();
        QCOMPARE(countColor(removed,QColor("#c5fff1")),0);
        QVERIFY(countColor(removed,QColor("#ffe08a"))>=8);
        QCOMPARE(collection->findChild<QListWidget *>(QStringLiteral("diaryCollectionList"))->count(),0);
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
        QVERIFY(database_->addProject(QStringLiteral("项目一"), firstPath, sample, &first));
        QVERIFY(database_->addProject(QStringLiteral("项目二"), secondPath, sample, &second));
        QVERIFY(database_->setSetting("projects.last_selected", QString::number(first)));
        DirectoryUrlRecorder opened;
        QDesktopServices::setUrlHandler("file", &opened, "record");
        const auto resetHandler = qScopeGuard([] { QDesktopServices::unsetUrlHandler("file"); });
        MainWindow window;
        window.show();
        button(window, QStringLiteral("项目记录"))->click();
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
            const int count = opened.urls.size();
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, position);
            QCOMPARE(opened.urls.size(), count);
            QCOMPARE(list->currentItem()->data(Qt::UserRole).toInt(), id);
            QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, position);
            QCOMPARE(opened.urls.size(), count + 1);
            QCOMPARE(opened.urls.last(), QUrl::fromLocalFile(id == first ? firstPath : secondPath));
        }
        // Changing the directory refreshes the target used by the existing list.
        interact(QStringLiteral("项目设置"), [&](QDialog &dialog) {
            dialog.findChild<QLineEdit *>(QStringLiteral("projectDirectory"))->setText(secondPath);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        }, [&] { button(window, QStringLiteral("项目设置"))->click(); });
        auto *item = findItem(first);
        QVERIFY(item);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(item).center());
        QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(item).center());
        QCOMPARE(opened.urls.size(), 3);
        QCOMPARE(opened.urls.last(), QUrl::fromLocalFile(secondPath));
    }
    void projectDirectoryMissingOrNotFolder()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QFile file(folder.filePath(QStringLiteral("普通文件.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        int empty = 0, missing = 0, notFolder = 0;
        QVERIFY(database_->addProject(QStringLiteral("未设置目录"), QString(), sample, &empty));
        QVERIFY(database_->addProject(QStringLiteral("目录已移走"), folder.filePath("missing"), sample, &missing));
        QVERIFY(database_->addProject(QStringLiteral("路径是文件"), file.fileName(), sample, &notFolder));
        DirectoryUrlRecorder opened;
        QDesktopServices::setUrlHandler("file", &opened, "record");
        const auto resetHandler = qScopeGuard([] { QDesktopServices::unsetUrlHandler("file"); });
        MainWindow window;
        window.show();
        button(window, QStringLiteral("项目记录"))->click();
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
        QVERIFY(database_->addProject(name, folder.path(), sample, &id));
        QVERIFY(database_->addWorkRecord(id, QStringLiteral("随项目删除的记录")));
        QVERIFY(database_->addProject(QStringLiteral("保留项目"), QString(), QString(), &other));
        QVERIFY(database_->setSetting("projects.last_selected", QString::number(id)));
        MainWindow window;
        window.show();
        button(window, QStringLiteral("项目记录"))->click();
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
        }, [&] { button(*page, QStringLiteral("项目设置"))->click(); });
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
        }, [&] { button(*page, QStringLiteral("项目设置"))->click(); });
        QCOMPARE(database_->projects().size(), 1);
        QCOMPARE(database_->projects().first().id, other);
        QVERIFY(database_->workRecords(id).isEmpty());
        QVERIFY(QFileInfo::exists(source.fileName()));
    }

    void outlineFailureBlocksBothExitPathsAndCanRetry()
    {
        int id = 0;
        QVERIFY(database_->addProject(QStringLiteral("故障恢复"), QString(), QStringLiteral("已保存"), &id));
        MainWindow window;
        window.show();
        auto *page = window.findChild<ProjectsPage *>();
        auto *outline = page->findChild<HeadingTextEdit *>(QStringLiteral("projectOutline"));
        const QString connection = QStringLiteral("outline-write-failure");
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE", connection);
            sql.setDatabaseName(database_->databasePath());
            QVERIFY(sql.open());
            QSqlQuery query(sql);
            QVERIFY(query.exec("CREATE TRIGGER fail_outline BEFORE UPDATE ON projects BEGIN SELECT RAISE(ABORT, 'injected outline failure'); END"));
            const auto cleanup = qScopeGuard([&] { query.exec("DROP TRIGGER IF EXISTS fail_outline"); });
            outline->setPlainText(sample);
            QCOMPARE(database_->projects().first().outline, QStringLiteral("已保存"));
            window.findChild<QCheckBox *>(QStringLiteral("closeToTrayCheck"))->setChecked(false);
            QCloseEvent close;
            interact(QStringLiteral("暂时无法退出"), [&](QDialog &dialog) { dialog.accept(); },
                     [&] { QApplication::sendEvent(&window, &close); });
            QVERIFY(!close.isAccepted());
            QCOMPARE(outline->toPlainText(), sample);
            QAction *quit = nullptr;
            for (auto *action : window.findChildren<QAction *>())
                if (action->text() == QStringLiteral("退出 Orchestrate")) quit = action;
            QVERIFY(quit);
            interact(QStringLiteral("暂时无法退出"), [&](QDialog &dialog) { dialog.accept(); }, [&] { quit->trigger(); });
            QVERIFY(window.isVisible());
            QCOMPARE(outline->toPlainText(), sample);
            QVERIFY(query.exec("DROP TRIGGER fail_outline"));
            QCloseEvent retry;
            QApplication::sendEvent(&window, &retry);
            QVERIFY(retry.isAccepted());
            QCOMPARE(database_->projects().first().outline, sample);
        }
        QSqlDatabase::removeDatabase(connection);
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
        button(window, QStringLiteral("项目记录"))->click();
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
            button(dialog,QStringLiteral("保存"))->click();
        }, [&]{button(*page,QStringLiteral("项目设置"))->click();});
        QCOMPARE(database_->projects().first().directory,QStringLiteral("D:/optional/nonexistent"));
        auto *outline=page->findChild<HeadingTextEdit *>(QStringLiteral("projectOutline"));
        QVERIFY(outline);
        const QString raw=QStringLiteral("  缩进原文\n")+sample+QStringLiteral("\n");
        outline->setPlainText(raw);
        outline->setPreviewMode(true);
        QCOMPARE(database_->projects().first().outline,raw);
        auto *outlineTitle=page->findChild<QLabel *>(QStringLiteral("outlineTitle"));
        QVERIFY(outlineTitle);
        QVERIFY(!outline->isVisible());
        QTest::mouseDClick(outlineTitle,Qt::LeftButton);
        QVERIFY(outline->isVisible());
        outline->window()->close();
        QVERIFY(!outline->isVisible());
        interact(QStringLiteral("添加工作记录"), [&](QDialog &dialog) {
            require(!dialogEditor(dialog)->isPreviewMode(),QStringLiteral("New record must start in edit"));
            auto *prefix=dialog.findChild<QLabel *>(QStringLiteral("summaryCaption"));
            require(prefix && prefix->text()==QStringLiteral("简介"),QStringLiteral("Summary caption missing"));
            require(prefix->parentWidget()!=dialog.findChild<QLineEdit *>(QStringLiteral("workRecordTitle")),QStringLiteral("Caption still inside editable value"));
            dialogEditor(dialog)->setPlainText(sample);
            require(!button(dialog,QStringLiteral("保存"))->isEnabled(),QStringLiteral("Missing title allowed"));
            dialog.findChild<QLineEdit *>(QStringLiteral("workRecordTitle"))->setText(QStringLiteral("第一阶段总结"));
            dialogEditor(dialog)->setPreviewMode(true);
            capture(dialog,QStringLiteral("work-record-editor"));
            button(dialog,QStringLiteral("保存"))->click();
        }, [&]{button(*page,QStringLiteral("添加记录"))->click();});
        auto records=database_->workRecords(id);
        QCOMPARE(records.size(),1);
        QCOMPARE(records.first().title,QStringLiteral("第一阶段总结"));
        QCOMPARE(records.first().content,sample);
        auto *list=page->findChild<QListWidget *>(QStringLiteral("workRecordList"));
        QCOMPARE(list->item(0)->text(),QStringLiteral("第一阶段总结"));
        list->setCurrentRow(0);
        interact(QStringLiteral("编辑工作记录"), [&](QDialog &dialog) {
            require(dialogEditor(dialog)->toPlainText()==sample,QStringLiteral("Body changed"));
            auto *title=dialog.findChild<QLineEdit *>(QStringLiteral("workRecordTitle"));
            require(title->isReadOnly(),QStringLiteral("Summary editable in preview"));
            require(!button(dialog,QStringLiteral("保存"))->isEnabled(),QStringLiteral("Unchanged preview can save"));
            capture(dialog,QStringLiteral("work-record-preview"));
            enterEdit(dialogEditor(dialog));
            require(!title->isReadOnly(),QStringLiteral("Summary did not unlock"));
            dialogEditor(dialog)->setPlainText(QStringLiteral("## 不保存"));
            button(dialog,QStringLiteral("取消"))->click();
        }, [&]{
            QCoreApplication::processEvents();
            const QPoint point=list->visualItemRect(list->item(0)).center();
            QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,point);
            QTest::mouseDClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,point);
        });
        QCOMPARE(database_->workRecords(id).first().content,sample);
        interact(QStringLiteral("编辑工作记录"), [&](QDialog &dialog) {
            enterEdit(dialogEditor(dialog));
            dialogEditor(dialog)->setPlainText(raw);
            dialog.findChild<QLineEdit *>(QStringLiteral("workRecordTitle"))->setText(QStringLiteral("已更新简介"));
            button(dialog,QStringLiteral("保存"))->click();
        }, [&]{list->itemDoubleClicked(list->item(0));});
        QCOMPARE(database_->workRecords(id).first().content,raw);
        QCOMPARE(list->item(0)->text(),QStringLiteral("已更新简介"));
        auto *search=page->findChild<QLineEdit *>(QStringLiteral("recordSearch"));
        search->setText(QStringLiteral("不存在"));
        QVERIFY(list->item(0)->isHidden());
        search->clear();
        QVERIFY(!list->item(0)->isHidden());
        QVERIFY(!page->findChild<QPushButton *>(QStringLiteral("detachRecords")));
        interact(QStringLiteral("添加工作记录"), [&](QDialog &dialog) {
            dialog.findChild<QLineEdit *>(QStringLiteral("workRecordTitle"))->setText(QStringLiteral("第二条记录"));
            dialogEditor(dialog)->setPlainText(sample);
            button(dialog,QStringLiteral("保存"))->click();
        }, [&]{ button(*page,QStringLiteral("添加记录"))->click(); });
        QCOMPARE(list->count(),2);
        QVERIFY(list->window()==&window);
        QCOMPARE(list->count(),2);
        capture(window,QStringLiteral("project-headings"));
        window.resize(980,640);
        capture(window,QStringLiteral("project-compact"));
        auto *projectList=page->findChild<QListWidget *>(QStringLiteral("projectList"));
        QVERIFY(projectList->visualItemRect(projectList->item(0)).height()>=44);
        AppDatabase reopened;
        QString error;
        QVERIFY2(reopened.open(&error),qPrintable(error));
        QCOMPARE(reopened.projects().first().outline,raw);
        QCOMPARE(reopened.workRecords(id).size(),2);
        MainWindow restored;
        auto *restoredPage=restored.findChild<ProjectsPage *>();
        QVERIFY(!restoredPage->findChild<HeadingTextEdit *>(QStringLiteral("projectOutline"))->isVisible());
        QCOMPARE(restoredPage->findChild<HeadingTextEdit *>(QStringLiteral("projectOutline"))->toPlainText(),raw);
    }
    void outlineWindowFromTitle()
    {
        int first=0, second=0;
        QString error;
        QVERIFY(database_->addProject(QStringLiteral("项目一"),QStringLiteral(""),sample,&first,&error));
        QVERIFY(database_->addProject(QStringLiteral("项目二"),QStringLiteral(""),QStringLiteral("第二份大纲"),&second,&error));
        // Old inline visibility settings must not reveal an inline editor.
        QVERIFY(database_->setSetting(QStringLiteral("project.%1.outline_expanded").arg(first),QStringLiteral("true"),&error));
        QVERIFY(database_->setSetting(QStringLiteral("projects.last_selected"),QString::number(first),&error));
        MainWindow window;
        window.show();
        button(window,QStringLiteral("项目记录"))->click();
        auto *page=window.findChild<ProjectsPage *>();
        auto *outline=page->findChild<HeadingTextEdit *>(QStringLiteral("projectOutline"));
        auto *title=page->findChild<QLabel *>(QStringLiteral("outlineTitle"));
        auto *floating=page->findChild<QDialog *>(QStringLiteral("outlineWindow"));
        QVERIFY(outline && title && floating);
        QVERIFY(!outline->isVisible());
        QVERIFY(!page->findChild<QWidget *>(QStringLiteral("outlineToggle")));
        QVERIFY(!page->findChild<QPushButton *>(QStringLiteral("detachOutline")));
        QVERIFY(!page->findChild<QPushButton *>(QStringLiteral("detachRecords")));
        QTest::mouseClick(title,Qt::LeftButton);
        QVERIFY(!floating->isVisible());
        QTest::mouseDClick(title,Qt::LeftButton);
        QVERIFY(floating->isVisible());
        QCOMPARE(outline->window(),floating);
        QVERIFY(outline->isPreviewMode());
        QTest::mouseDClick(title,Qt::LeftButton);
        QCOMPARE(page->findChildren<QDialog *>(QStringLiteral("outlineWindow")).size(),1);
        capture(*floating,QStringLiteral("project-outline-window"));
        QVERIFY(outline->height()>400);
        auto *preview=outline->findChild<QTextEdit *>();
        const qreal originalSize=preview->document()->defaultFont().pointSizeF();
        QSignalSpy zoomChanges(outline,&HeadingTextEdit::textChanged);
        sendWheel(preview->viewport(),240);
        QCOMPARE(preview->document()->defaultFont().pointSizeF(),originalSize+2);
        QCOMPARE(zoomChanges.count(),0);
        QCOMPARE(outline->toPlainText(),sample);
        capture(*floating,QStringLiteral("project-outline-zoom-preview"));
        enterEdit(outline);
        auto *source=outline->findChild<QPlainTextEdit *>();
        QVERIFY(source);
        sendWheel(source->viewport(),120);
        QCOMPARE(source->font().pointSizeF(),originalSize+3);
        QCOMPARE(zoomChanges.count(),0);
        capture(*floating,QStringLiteral("project-outline-zoom-editor"));
        source->moveCursor(QTextCursor::End);
        source->insertPlainText(QStringLiteral("\n新增内容"));
        floating->close();
        QVERIFY(!outline->isVisible());
        QTest::keyClick(title,Qt::Key_Return);
        QVERIFY(floating->isVisible());
        QCOMPARE(preview->document()->defaultFont().pointSizeF(),originalSize+3);
        enterEdit(outline);
        QVERIFY(source->document()->isUndoAvailable());
        source->undo();
        QCOMPARE(outline->toPlainText(),sample);
        auto *projects=page->findChild<QListWidget *>(QStringLiteral("projectList"));
        for(int i=0;i<projects->count();++i)
            if(projects->item(i)->data(Qt::UserRole).toInt()==second) projects->setCurrentRow(i);
        QCOMPARE(floating->windowTitle(),QStringLiteral("项目二 · 项目大纲"));
        QVERIFY(outline->isPreviewMode());
        QCOMPARE(outline->toPlainText(),QStringLiteral("第二份大纲"));
        outline->setPlainText(QStringLiteral("## 第二项目已修改"));
        QTest::keyClick(floating,Qt::Key_Escape);
        QVERIFY(!outline->isVisible());
        capture(window,QStringLiteral("project-title-entry"));
        window.resize(980,640);
        capture(window,QStringLiteral("project-title-entry-compact"));
        AppDatabase reopened;
        QVERIFY2(reopened.open(&error),qPrintable(error));
        for(const auto &project:reopened.projects())
            QCOMPARE(project.outline,project.id==first ? sample : QStringLiteral("## 第二项目已修改"));
    }
    void legacyWorkTitles()
    {
        int id=0;
        QString error;
        QVERIFY(database_->addProject(QStringLiteral("旧项目"),QStringLiteral(""),QStringLiteral(""),&id,&error));
        const QString raw=QStringLiteral("\n  旧记录简介\n## 正文保持\n内容\n");
        QVERIFY(database_->addWorkRecord(id,raw,&error));
        MainWindow window;
        auto *list=window.findChild<QListWidget *>(QStringLiteral("workRecordList"));
        QVERIFY(list && list->count()==1);
        QCOMPARE(list->item(0)->text(),QStringLiteral("旧记录简介"));
        QCOMPARE(database_->workRecords(id).first().content,raw);
        QVERIFY(database_->workRecords(id).first().title.isEmpty());
    }

    void workTitleMigrationAndProjectSwitching()
    {
        int first=0, second=0;
        QString error;
        QVERIFY(database_->addProject(QStringLiteral("旧项目"),QStringLiteral(""),QStringLiteral("大纲"),&first,&error));
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
        QVERIFY(migrated.addProject(QStringLiteral("第二项目"),QStringLiteral(""),QStringLiteral(""),&second,&error));
        QVERIFY(migrated.setSetting(QStringLiteral("projects.last_selected"),QString::number(first),&error));
        QVERIFY(migrated.setSetting(QStringLiteral("project.%1.outline_expanded").arg(first),QStringLiteral("false"),&error));
        MainWindow window;
        window.show();
        button(window,QStringLiteral("项目记录"))->click();
        auto *list=window.findChild<QListWidget *>(QStringLiteral("projectList"));
        QVERIFY(!window.findChild<HeadingTextEdit *>(QStringLiteral("projectOutline"))->isVisible());
        for(int i=0;i<list->count();++i) {
            if(list->item(i)->data(Qt::UserRole).toInt()!=second) continue;
            const QRect rect=list->visualItemRect(list->item(i));
            QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(rect.right()-10,rect.center().y()));
        }
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toInt(),second);
        auto *outline=window.findChild<HeadingTextEdit *>(QStringLiteral("projectOutline"));
        outline->setPlainText(sample);
        for(int i=0;i<list->count();++i)
            if(list->item(i)->data(Qt::UserRole).toInt()==first) list->setCurrentRow(i);
        for(const auto &project:migrated.projects())
            if(project.id==second) QCOMPARE(project.outline,sample);
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
            require(editor->isPreviewMode(),QStringLiteral("Reminder not in preview"));
            require(dialog.findChild<QLineEdit *>(QStringLiteral("reminderTitle"))->isReadOnly(),QStringLiteral("Reminder title unlocked"));
            capture(dialog, QStringLiteral("reminder-preview"));
            button(dialog, QStringLiteral("取消"))->click();
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
            require(!button(dialog,QStringLiteral("保存"))->isEnabled(),QStringLiteral("Empty title can save"));
            dialog.findChild<QLineEdit *>(QStringLiteral("reminderTitle"))->setText(QStringLiteral("只填写标题的提醒"));
            button(dialog,QStringLiteral("保存"))->click();
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
            require(title->isReadOnly() && time->isReadOnly() && !repeat->isEnabled(),QStringLiteral("Reminder controls editable in preview"));
            require(!button(dialog,QStringLiteral("保存"))->isEnabled(),QStringLiteral("Unchanged reminder can save"));
            enterEdit(editor);
            require(!title->isReadOnly() && !time->isReadOnly() && repeat->isEnabled(),QStringLiteral("Reminder controls not unlocked"));
            title->setText(QStringLiteral("修改后的提醒"));
            editor->setPlainText(sample);
            repeat->setCurrentIndex(repeat->findData(QStringLiteral("daily")));
            button(dialog,QStringLiteral("保存"))->click();
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
