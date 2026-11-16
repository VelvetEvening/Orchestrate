#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {
struct MainWindowSearch { DWORD pid; HWND hwnd = nullptr; };
HWND nativeMainWindow(DWORD pid)
{
    MainWindowSearch search{pid};
    EnumWindows([](HWND hwnd, LPARAM parameter) -> BOOL {
        auto &search = *reinterpret_cast<MainWindowSearch *>(parameter);
        DWORD owner = 0;
        GetWindowThreadProcessId(hwnd, &owner);
        if (owner != search.pid || GetWindow(hwnd, GW_OWNER)) return TRUE;
        wchar_t title[128]{};
        GetWindowTextW(hwnd, title, 128);
        if (QString::fromWCharArray(title) == QStringLiteral("Orchestrate")) {
            search.hwnd = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.hwnd;
}
}

class StartupVisibilityTest : public QObject {
    Q_OBJECT
private slots:
    void nativeStartup_data()
    {
        QTest::addColumn<bool>("hiddenLauncher");
        QTest::newRow("legacy-hidden-launcher") << true;
        QTest::newRow("normal-launcher") << false;
    }

    void nativeStartup()
    {
        QFETCH(bool, hiddenLauncher);
        QTemporaryDir fixture(QDir::tempPath() + QStringLiteral("/Orchestrate-visibility-XXXXXX"));
        QVERIFY(fixture.isValid());
        const QString install = fixture.filePath(QStringLiteral("app"));
        const QString workspace = fixture.filePath(QStringLiteral(".Orchestrate-update-visibility"));
        QVERIFY(QDir().mkpath(install + QStringLiteral("/data")));
        QVERIFY(QDir().mkpath(workspace));
        const QString source = qEnvironmentVariable("ORCHESTRATE_VISIBILITY_APP", QStringLiteral(ORCHESTRATE_APPLICATION));
        const QString executable = install + QStringLiteral("/Orchestrate.exe");
        QVERIFY2(QFile::copy(source, executable), qPrintable(source));
        // Synthetic database: do not import user data or claim the user's Alt+X.
        const QString connection = QStringLiteral("visibility-fixture");
        {
            auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            database.setDatabaseName(install + QStringLiteral("/data/orchestrate.sqlite3"));
            QVERIFY(database.open());
            QSqlQuery query(database);
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE app_settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)")));
            QVERIFY(query.exec(QStringLiteral("INSERT INTO app_settings VALUES ('global_shortcut_enabled', 'false')")));
            database.close();
        }
        QSqlDatabase::removeDatabase(connection);

        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("windows"));
        process.setProcessEnvironment(environment);
        process.setProgram(executable);
        const QString healthPath = workspace + QStringLiteral("/startup.json");
        process.setArguments({QStringLiteral("--update-health-file"), healthPath});
        process.setWorkingDirectory(install);
        process.setCreateProcessArgumentsModifier([hiddenLauncher](QProcess::CreateProcessArguments *args) {
            args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
            args->startupInfo->wShowWindow = hiddenLauncher ? SW_HIDE : SW_SHOWNORMAL;
        });
        const auto stop = qScopeGuard([&] {
            if (process.state() == QProcess::NotRunning) return;
            if (const auto hwnd = nativeMainWindow(DWORD(process.processId()))) {
                const DWORD thread = GetWindowThreadProcessId(hwnd, nullptr);
                PostThreadMessageW(thread, WM_QUIT, 0, 0);
            }
            if (!process.waitForFinished(5000)) { process.kill(); process.waitForFinished(5000); }
        });
        process.start();
        QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
        QElapsedTimer timer;
        timer.start();
        while (!QFile::exists(healthPath) && process.state() != QProcess::NotRunning && timer.elapsed() < 10000)
            QTest::qWait(25);
        const HWND hwnd = nativeMainWindow(DWORD(process.processId()));
        QVERIFY2(hwnd, "No native main window was created.");
        QVERIFY2(IsWindowVisible(hwnd) && !IsIconic(hwnd), "Updater reported startup with an invisible native main window.");
        QFile healthFile(healthPath);
        QVERIFY2(healthFile.open(QIODevice::ReadOnly), "Visible window did not confirm startup.");
        const auto health = QJsonDocument::fromJson(healthFile.readAll()).object();
        QCOMPARE(health.value(QStringLiteral("process_id")).toInteger(), process.processId());
        QVERIFY(health.value(QStringLiteral("window_visible")).toBool());
    }
};

QTEST_GUILESS_MAIN(StartupVisibilityTest)
#include "test_startup_visibility.moc"
