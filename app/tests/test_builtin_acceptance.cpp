#include "mainwindow.h"
#include "pages/automationpage.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>
#include <QUuid>
#include <functional>
#include <memory>
#include <stdexcept>

namespace {
void require(bool condition, const QString &message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

QString fixturePath(const QString &relative)
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(relative);
}

const QString manifestRelative = QStringLiteral("tools/scheduled-shutdown/orchestrate-tool.json");
const QString stateRelative = QStringLiteral("tools/scheduled-shutdown/state/current.json");
const QString databaseRelative = QStringLiteral("data/orchestrate.sqlite3");

void removeFixtureFile(const QString &relative)
{
    const QString path = fixturePath(relative);
    require(!QFileInfo::exists(path) || QFile::remove(path), QStringLiteral("Cannot remove fixture: ") + path);
}

QPushButton *button(QWidget &root, const QString &text)
{
    for (auto *candidate : root.findChildren<QPushButton *>()) {
        if (candidate->text() == text) return candidate;
    }
    throw std::runtime_error((QStringLiteral("Missing button: ") + text).toStdString());
}

bool visibleLabel(QWidget &root, const QString &text)
{
    for (auto *label : root.findChildren<QLabel *>()) {
        if (label->isVisible() && label->text().contains(text)) return true;
    }
    return false;
}

AppDatabase::AutomationTool onlyTool(AppDatabase &database)
{
    QString error;
    const auto tools = database.automationTools(0, &error);
    require(error.isEmpty(), error);
    require(tools.size() == 1, QStringLiteral("Expected exactly one tool (no duplicate registrations)"));
    return tools.first();
}

QTreeWidgetItem *selectTool(MainWindow &window, int id)
{
    auto *tree = window.findChild<QTreeWidget *>(QStringLiteral("toolTree"));
    require(tree != nullptr, QStringLiteral("Missing tool tree"));
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto *group = tree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            auto *item = group->child(j);
            if (item->data(0, Qt::UserRole).toInt() != id) continue;
            group->setExpanded(true);
            tree->scrollToItem(item);
            QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                              tree->visualItemRect(item).center());
            require(tree->currentItem() == item, QStringLiteral("Tool row selection failed"));
            return item;
        }
    }
    throw std::runtime_error("Missing tool row");
}

std::unique_ptr<MainWindow> openWindow()
{
    auto window = std::make_unique<MainWindow>();
    window->show();
    button(*window, QStringLiteral("自动化工具"))->click();
    QTest::qWait(30);
    require(window->findChild<AutomationPage *>()->isVisible(), QStringLiteral("Navigation failed"));
    return window;
}

void checkBuiltin(MainWindow &window, AppDatabase &database, int expectedId)
{
    const auto tool = onlyTool(database);
    require(tool.id == expectedId && tool.builtin, QStringLiteral("Built-in identity changed"));
    require(QDir::cleanPath(tool.registrationPath) == QDir::cleanPath(fixturePath(manifestRelative)),
            QStringLiteral("Manifest path did not follow executable directory"));
    require(QDir::cleanPath(tool.statePath) == QDir::cleanPath(fixturePath(stateRelative)),
            QStringLiteral("State path did not follow executable directory"));
    // The shipped manifest omits working_directory. Empty is intentional: the
    // command runner falls back to the manifest's directory, not the old path.
    require(tool.workingDirectory.isEmpty(), QStringLiteral("Old working-directory override survived adoption"));
    selectTool(window, tool.id);
    require(!visibleLabel(window, QStringLiteral("内置工具 ·")), QStringLiteral("Technical source hint should not be visible"));
    require(button(window, QStringLiteral("移除注册"))->isHidden(), QStringLiteral("Remove button exposed"));
    auto *commands = window.findChild<QListWidget *>(QStringLiteral("commandList"));
    require(commands && commands->count() == 8, QStringLiteral("Expected eight bundled commands"));
    QString error;
    const auto savedCommands = database.toolCommands(tool.id, &error);
    require(error.isEmpty() && savedCommands.size() == 8, QStringLiteral("Command replacement failed"));
    int countdownCommands = 0;
    for (const auto &command : savedCommands) {
        require(!command.name.contains(QStringLiteral("演示")), QStringLiteral("Demo command remains"));
        if (command.name == QStringLiteral("设置弹窗持续时间")) ++countdownCommands;
    }
    require(countdownCommands == 1, QStringLiteral("Duplicate countdown setting"));
}

// Only answer the exact dialog expected by a test. Unexpected dialogs are rejected
// and reported, not silently accepted. Never automate a command/risk confirmation.
void withDialogs(const QString &file, const QString &question, const std::function<void()> &action)
{
    QTimer timer;
    bool selected = false;
    bool answered = false;
    QPointer<QFileDialog> expectedFileDialog;
    int selectionAttempts = 0;
    QString unexpected;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *modal = QApplication::activeModalWidget();
        if (auto *dialog = qobject_cast<QFileDialog *>(modal)) {
            if (!file.isEmpty() && !answered && (!selected || expectedFileDialog == dialog)
                && ++selectionAttempts <= 150) {
                selected = true;
                expectedFileDialog = dialog;
                // Enter the known absolute fixture path directly; selectFile depends
                // on the asynchronously populated QFileSystemModel selection.
                auto *fileName = dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
                if (fileName) fileName->setText(QDir::toNativeSeparators(file));
                else dialog->selectFile(file);
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            } else {
                unexpected = QStringLiteral("Unexpected file dialog (attempts=%1, answered=%2, files=%3)")
                                 .arg(selectionAttempts).arg(answered).arg(dialog->selectedFiles().join(QStringLiteral(", ")));
                dialog->reject();
            }
        } else if (auto *box = qobject_cast<QMessageBox *>(modal)) {
            if (!question.isEmpty() && box->windowTitle() == question && !answered) {
                answered = true;
                box->button(QMessageBox::Yes)->click();
            } else {
                unexpected = box->windowTitle() + QStringLiteral(": ") + box->text();
                box->reject();
            }
        }
    });
    timer.start(20);
    action();
    require(unexpected.isEmpty(), unexpected);
    require(file.isEmpty() || selected, QStringLiteral("File picker was not exercised"));
    require(question.isEmpty() || answered, QStringLiteral("Confirmation was not exercised"));
}

void checkRemoveProtection(MainWindow &window, AppDatabase &database)
{
    const int id = onlyTool(database).id;
    auto *item = selectTool(window, id);
    auto *tree = window.findChild<QTreeWidget *>(QStringLiteral("toolTree"));
    QStringList actions;
    bool inspected = false;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *menu = qobject_cast<QMenu *>(widget);
            if (!menu || !menu->isVisible()) continue;
            for (auto *entry : menu->actions()) actions.append(entry->text());
            inspected = true;
            menu->close();
        }
    });
    timer.start(20);
    // Emit the same signal as a user context-menu request, avoiding native menus.
    tree->customContextMenuRequested(tree->visualItemRect(item).center());
    timer.stop();
    require(inspected && actions.contains(QStringLiteral("重新读取声明")), QStringLiteral("Menu not inspected"));
    require(!actions.contains(QStringLiteral("移除注册")), QStringLiteral("Context menu exposes removal"));
    // Invoke the hidden button's real handler too: hiding alone is not protection.
    withDialogs({}, {}, [&] { button(window, QStringLiteral("移除注册"))->click(); });
    require(onlyTool(database).id == id, QStringLiteral("Hidden remove handler deleted a built-in"));
}

void screenshot(MainWindow &window, const QString &name)
{
    QCoreApplication::processEvents();
    require(window.grab().save(fixturePath(QStringLiteral("artifacts/") + name + QStringLiteral(".png"))),
            QStringLiteral("Could not save screenshot"));
}

void freshInstall()
{
    require(!QFileInfo::exists(fixturePath(databaseRelative)), QStringLiteral("Fresh fixture is not empty"));
    auto window = openWindow();
    AppDatabase database;
    QString error;
    require(database.open(&error), error);
    const auto tool = onlyTool(database);
    const auto groups = database.toolGroups(&error);
    require(error.isEmpty() && groups.size() == 1 && groups.first().name == QStringLiteral("系统工具")
                && tool.groupId == groups.first().id, QStringLiteral("Default group was not assigned"));
    require(!tool.refreshEnabled, QStringLiteral("Fresh install enabled periodic refresh"));
    checkBuiltin(*window, database, tool.id);
    require(visibleLabel(*window, QStringLiteral("暂无状态")), QStringLiteral("Missing-state badge incorrect"));
    require(visibleLabel(*window, QStringLiteral("还没有状态文件")), QStringLiteral("Missing-state summary incorrect"));
    checkRemoveProtection(*window, database);
    screenshot(*window, QStringLiteral("fresh-install"));
    const int groupId = tool.groupId;
    window.reset();
    window = openWindow();
    checkBuiltin(*window, database, tool.id);
    require(onlyTool(database).groupId == groupId && database.toolGroups().size() == 1,
            QStringLiteral("Second startup changed group or duplicated it"));
}

void seedLegacy(bool grouped)
{
    QString error;
    {
        AppDatabase database;
        require(database.open(&error), error);
        AppDatabase::AutomationTool tool;
        tool.externalId = QStringLiteral("scheduled-shutdown");
        tool.name = QStringLiteral("旧手动注册");
        tool.targetType = QStringLiteral("windows-local");
        tool.registrationPath = fixturePath(QStringLiteral("old-location/orchestrate-tool.json"));
        tool.statePath = fixturePath(QStringLiteral("old-location/state/current.json"));
        tool.workingDirectory = fixturePath(QStringLiteral("old-location"));
        tool.refreshEnabled = true;
        tool.refreshMode = QStringLiteral("daily");
        tool.refreshIntervalSeconds = 900;
        tool.dailyRefreshTime = QStringLiteral("21:30");
        if (grouped) require(database.addToolGroup(QStringLiteral("我的分组"), &tool.groupId, &error), error);
        int id = 0;
        require(database.addAutomationTool(tool, &id, &error), error);
        AppDatabase::ToolCommand obsolete;
        obsolete.toolId = id;
        obsolete.name = QStringLiteral("旧演示命令");
        obsolete.executable = QStringLiteral("DO-NOT-EXECUTE");
        require(database.addToolCommand(obsolete, nullptr, &error), error);
    }
    // Model an actual pre-builtin SQLite schema, not merely a false flag.
    const QString connection = QStringLiteral("legacy-fixture");
    {
        auto sql = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        sql.setDatabaseName(fixturePath(databaseRelative));
        require(sql.open(), sql.lastError().text());
        QSqlQuery query(sql);
        require(query.exec(QStringLiteral("ALTER TABLE automation_tools DROP COLUMN builtin")), query.lastError().text());
        sql.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

void legacyAdoption(bool grouped)
{
    seedLegacy(grouped);
    auto window = openWindow();
    AppDatabase database;
    QString error;
    require(database.open(&error), error);
    const auto tool = onlyTool(database);
    require(tool.id == 1, QStringLiteral("Legacy row was replaced instead of adopted"));
    checkBuiltin(*window, database, tool.id);
    const auto groups = database.toolGroups(&error);
    require(error.isEmpty() && groups.size() == 1 && tool.groupId == groups.first().id
                && groups.first().name == (grouped ? QStringLiteral("我的分组") : QStringLiteral("系统工具")),
            QStringLiteral("Legacy grouping was not preserved/assigned correctly"));
    require(tool.refreshEnabled && tool.refreshMode == QStringLiteral("daily")
                && tool.refreshIntervalSeconds == 900 && tool.dailyRefreshTime == QStringLiteral("21:30"),
            QStringLiteral("Legacy refresh settings changed"));
    require(visibleLabel(*window, QStringLiteral("暂无状态")), QStringLiteral("Legacy missing-state display incorrect"));
    checkRemoveProtection(*window, database);
    screenshot(*window, grouped ? QStringLiteral("legacy-grouped") : QStringLiteral("legacy-ungrouped"));
    window.reset();
    window = openWindow();
    checkBuiltin(*window, database, tool.id);
    require(onlyTool(database).groupId == tool.groupId, QStringLiteral("Restart changed legacy group"));
}

void reloadAndReimport()
{
    auto window = openWindow();
    AppDatabase database;
    QString error;
    require(database.open(&error), error);
    const int id = onlyTool(database).id;
    int groupId = 0;
    require(database.addToolGroup(QStringLiteral("自选分组"), &groupId, &error), error);
    require(database.setToolGroup(id, groupId, &error), error);
    require(database.updateToolRefreshSettings(id, true, QStringLiteral("daily"), 900,
                                               QStringLiteral("21:30"), &error), error);
    window.reset();
    window = openWindow();
    for (int pass = 0; pass < 2; ++pass) {
        selectTool(*window, id);
        withDialogs({}, {}, [&] { button(*window, QStringLiteral("重新读取声明"))->click(); });
        require(visibleLabel(*window, QStringLiteral("已重新读取声明，共 8 条命令")), QStringLiteral("Reload not exercised"));
        checkBuiltin(*window, database, id);
        checkRemoveProtection(*window, database);
        withDialogs(fixturePath(manifestRelative), QStringLiteral("工具已注册"), [&] {
            button(*window, QStringLiteral("注册本地工具"))->click();
        });
        checkBuiltin(*window, database, id);
        const auto tool = onlyTool(database);
        require(tool.groupId == groupId && tool.refreshEnabled && tool.refreshMode == QStringLiteral("daily")
                    && tool.refreshIntervalSeconds == 900 && tool.dailyRefreshTime == QStringLiteral("21:30"),
                QStringLiteral("Reload/re-import overwrote local preferences"));
        checkRemoveProtection(*window, database);
    }
    screenshot(*window, QStringLiteral("reimport-protected"));
    window.reset();
    window = openWindow();
    checkBuiltin(*window, database, id);
    require(onlyTool(database).groupId == groupId, QStringLiteral("Preferences not persistent"));
}

void deletedGroupStaysDeleted()
{
    auto window = openWindow();
    AppDatabase database;
    QString error;
    require(database.open(&error), error);
    const auto original = onlyTool(database);
    auto *item = selectTool(*window, original.id);
    auto *tree = window->findChild<QTreeWidget *>(QStringLiteral("toolTree"));
    tree->setCurrentItem(item->parent());
    withDialogs({}, QStringLiteral("删除工具组"), [&] {
        button(*window, QStringLiteral("删除工具组"))->click();
    });
    require(database.toolGroups().isEmpty() && onlyTool(database).groupId == 0,
            QStringLiteral("Deleting group did not ungroup the tool"));
    require(!database.setting(QStringLiteral("automation.builtin_group_id")).isEmpty(),
            QStringLiteral("Deleted group marker lost"));
    window.reset();
    window = openWindow();
    checkBuiltin(*window, database, original.id);
    require(database.toolGroups().isEmpty() && onlyTool(database).groupId == 0,
            QStringLiteral("Startup recreated a user-deleted group"));
    screenshot(*window, QStringLiteral("deleted-group-restart"));
}
} // namespace

int main(int argc, char **argv)
{
    // Use the real widget tree with Qt's offscreen backend; never show a native
    // window/tray icon, read real AppData, or launch any tool command.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setOrganizationName(QStringLiteral("OrchestrateAcceptanceTests"));
    app.setApplicationName(QUuid::createUuid().toString(QUuid::Id128));
    QStandardPaths::setTestModeEnabled(true);
#ifdef Q_OS_WIN
    // The offscreen platform has no native font discovery on Windows. Load
    // already-installed fonts for legible screenshots; do not install anything.
    const QDir fonts(QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts")));
    for (const auto &name : {QStringLiteral("msyh.ttc"), QStringLiteral("msyhbd.ttc"),
                             QStringLiteral("segoeui.ttf")}) {
        QFontDatabase::addApplicationFont(fonts.filePath(name));
    }
    app.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
#endif
    app.setStyle(QStringLiteral("Fusion"));
    app.setWindowIcon(QIcon(QStringLiteral(":/app-icon/32.png")));
    QTextStream output(stdout);
    if (QFileInfo(QCoreApplication::applicationDirPath()).fileName() != QStringLiteral("builtin-acceptance-test")) {
        output << "Refusing to run outside the dedicated acceptance-test directory." << Qt::endl;
        return 2;
    }
    const QDir bundled(fixturePath(QStringLiteral("tools/scheduled-shutdown")));
    if (!QFileInfo::exists(fixturePath(manifestRelative)) || !bundled.entryList({QStringLiteral("*.ps1")}, QDir::Files).isEmpty()) {
        output << "Fixture must contain a manifest but NO PowerShell scripts." << Qt::endl;
        return 2;
    }
    QDir().mkpath(fixturePath(QStringLiteral("artifacts")));
    const QList<QPair<QString, std::function<void()>>> cases {
        {QStringLiteral("fresh-install"), freshInstall},
        {QStringLiteral("legacy-ungrouped"), [] { legacyAdoption(false); }},
        {QStringLiteral("legacy-grouped"), [] { legacyAdoption(true); }},
        {QStringLiteral("reload-and-reimport"), reloadAndReimport},
        {QStringLiteral("deleted-group-restart"), deletedGroupStaysDeleted}
    };
    int failures = 0;
    for (const auto &test : cases) {
        try {
            removeFixtureFile(databaseRelative);
            removeFixtureFile(databaseRelative + QStringLiteral("-wal"));
            removeFixtureFile(databaseRelative + QStringLiteral("-shm"));
            removeFixtureFile(stateRelative);
            test.second();
            require(!QFileInfo::exists(fixturePath(stateRelative)), QStringLiteral("Test unexpectedly generated tool state"));
            const QString saved = QStringLiteral("artifacts/") + test.first + QStringLiteral(".sqlite3");
            removeFixtureFile(saved);
            require(QFile::copy(fixturePath(databaseRelative), fixturePath(saved)), QStringLiteral("Could not archive test database"));
            output << "PASS " << test.first << Qt::endl;
        } catch (const std::exception &error) {
            ++failures;
            output << "FAIL " << test.first << ": " << error.what() << Qt::endl;
        }
    }
    output << "Passed: " << cases.size() - failures << " / " << cases.size() << Qt::endl;
    output << "Artifacts: " << fixturePath(QStringLiteral("artifacts")) << Qt::endl;
    return failures == 0 ? 0 : 1;
}
