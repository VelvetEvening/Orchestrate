#include "platform/autostart.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

namespace {
std::unique_ptr<QSettings> store(const QString &path)
{
    return std::make_unique<QSettings>(path, QSettings::IniFormat);
}
}

class AutoStartTest final : public QObject
{
    Q_OBJECT
private slots:
    void copiesAreIndependent()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("login.ini");
        const QString approvalPath = directory.filePath("approval.ini");
        const QString productionName = AutoStart::entryNameForExecutable(QStringLiteral("E:/Apps/Orchestrate.exe"));
        const QString developmentName = AutoStart::entryNameForExecutable(QStringLiteral("D:/build/Orchestrate.exe"));
        AutoStart production(store(path), store(approvalPath), QStringLiteral("E:/Apps/Orchestrate.exe"));
        AutoStart development(store(path), store(approvalPath), QStringLiteral("D:/build/Orchestrate.exe"));
        QString error;
        QVERIFY(production.setEnabled(true, &error));
        QVERIFY(!development.state().enabled);
        QVERIFY(development.setEnabled(false, &error));
        QVERIFY(production.state().enabled);
        QVERIFY(development.setEnabled(true, &error));
        QSettings registration(path, QSettings::IniFormat);
        QCOMPARE(registration.value(productionName).toString(),
                 QStringLiteral("\"") + QDir::toNativeSeparators(QStringLiteral("E:/Apps/Orchestrate.exe")) + '"');
        QSettings approval(approvalPath, QSettings::IniFormat);
        const QByteArray disabled = QByteArray::fromHex("030000000000000000000000");
        approval.setValue(productionName, disabled);
        approval.sync();
        QVERIFY(production.state().disabledByWindows);
        QVERIFY(!development.state().disabledByWindows);
        QVERIFY(production.setEnabled(false, &error));
        QVERIFY(development.state().enabled);
        QVERIFY(development.setEnabled(false, &error));
        QVERIFY(!production.state().enabled);
        QVERIFY(production.setEnabled(true, &error));
        QVERIFY(development.setEnabled(true, &error));
        QVERIFY(development.setEnabled(false, &error));
        QVERIFY(production.state().enabled);
        approval.sync();
        QCOMPARE(approval.value(productionName).toByteArray(), disabled);
        QVERIFY(!approval.contains(developmentName));
        AutoStart updated(store(path), store(approvalPath), QStringLiteral("E:/Apps/Orchestrate.exe"));
        QVERIFY(updated.state().enabled);
        QVERIFY(updated.state().disabledByWindows);
    }

    void identityFollowsDirectory()
    {
        const QString name = AutoStart::entryNameForExecutable(QStringLiteral("C:/Apps/Orchestrate.exe"));
        QVERIFY(name.startsWith(QStringLiteral("Orchestrate-")));
        QCOMPARE(name, AutoStart::entryNameForExecutable(QStringLiteral("c:\\apps\\Orchestrate.exe")));
        QCOMPARE(name, AutoStart::entryNameForExecutable(QStringLiteral("C:/Apps/./Orchestrate.exe")));
        QCOMPARE(name, AutoStart::entryNameForExecutable(QStringLiteral("C:/Apps/old/../Orchestrate.exe")));
        QCOMPARE(name, AutoStart::entryNameForExecutable(QStringLiteral("C:/Apps/Renamed.exe")));
        QVERIFY(name != AutoStart::entryNameForExecutable(QStringLiteral("D:/Apps/Orchestrate.exe")));
        QVERIFY(name != AutoStart::entryNameForExecutable(QStringLiteral("C:/Apps-two/Orchestrate.exe")));
        QCOMPARE(AutoStart::entryNameForExecutable(QStringLiteral("C:/带 空格/Orchestrate.exe")),
                 AutoStart::entryNameForExecutable(QStringLiteral("c:\\带 空格\\Orchestrate.exe")));
    }

    void persistenceAndPathChange()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("login.ini");
        const QString executable = QStringLiteral("C:/带 空格/Orchestrate.exe");
        const QString name = AutoStart::entryNameForExecutable(executable);
        QSettings other(path, QSettings::IniFormat);
        other.setValue("AnotherApp", "unchanged");
        other.sync();
        AutoStart startup(store(path), nullptr, executable);
        QVERIFY(!startup.state().enabled);
        QVERIFY(startup.state().error.isEmpty());
        QString error;
        QVERIFY2(startup.setEnabled(true, &error), qPrintable(error));
        other.sync();
        QCOMPARE(other.value(name).toString(), '"' + QDir::toNativeSeparators(executable) + '"');
        AutoStart reopened(store(path), nullptr, executable);
        QVERIFY(reopened.state().enabled);
        AutoStart moved(store(path), nullptr, QStringLiteral("D:/New Folder/Orchestrate.exe"));
        QVERIFY(!moved.state().enabled);
        QVERIFY(moved.setEnabled(false, &error));
        QVERIFY(reopened.state().enabled);
        QVERIFY(moved.setEnabled(true, &error));
        other.sync();
        QVERIFY(other.value(AutoStart::entryNameForExecutable(QStringLiteral("D:/New Folder/Orchestrate.exe"))).toString().contains("New Folder"));
        QVERIFY(moved.setEnabled(false, &error));
        QVERIFY(moved.setEnabled(false, &error));
        QVERIFY(startup.setEnabled(false, &error));
        other.sync();
        QVERIFY(!other.contains(name));
        QCOMPARE(other.value("AnotherApp").toString(), QStringLiteral("unchanged"));
    }

    void windowsDisableIsReportedAndPreserved()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("login.ini");
        const QString approvalPath = directory.filePath("approval.ini");
        AutoStart startup(store(path), store(approvalPath), QStringLiteral("C:/Orchestrate.exe"));
        const QString name = AutoStart::entryNameForExecutable(QStringLiteral("C:/Orchestrate.exe"));
        QString error;
        QVERIFY(startup.setEnabled(true, &error));
        QSettings approval(approvalPath, QSettings::IniFormat);
        for (char marker : {2, 3, 6, 7}) {
            QByteArray bytes(12, '\0');
            bytes[0] = marker;
            approval.setValue(name, bytes);
            approval.sync();
            QCOMPARE(startup.state().disabledByWindows, marker == 3 || marker == 7);
            QVERIFY(startup.setEnabled(true, &error));
            approval.sync();
            QCOMPARE(approval.value(name).toByteArray(), bytes);
        }
    }

    void legacyEntryIsOnlyAdoptedByItsOwner()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("login.ini");
        const QString ownerPath = QStringLiteral("E:/带 空格/Orchestrate.exe");
        const QString command = '"' + QDir::toNativeSeparators(ownerPath) + '"';
        QSettings registration(path, QSettings::IniFormat);
        registration.setValue("Orchestrate", command);
        registration.sync();
        AutoStart owner(store(path), nullptr, ownerPath);
        AutoStart other(store(path), nullptr, QStringLiteral("D:/build/Orchestrate.exe"));
        QVERIFY(owner.state().enabled);
        QVERIFY(!other.state().enabled);
        registration.sync();
        QVERIFY(!registration.contains(AutoStart::entryNameForExecutable(ownerPath)));
        QString error;
        QVERIFY(other.setEnabled(false, &error));
        QVERIFY(owner.state().enabled);
        QVERIFY(other.setEnabled(true, &error));
        registration.sync();
        QCOMPARE(registration.value("Orchestrate").toString(), command);
        QVERIFY(owner.setEnabled(true, &error));
        registration.sync();
        QVERIFY(!registration.contains("Orchestrate"));
        QCOMPARE(registration.value(AutoStart::entryNameForExecutable(ownerPath)).toString(), command);
        QVERIFY(other.state().enabled);
        QVERIFY(owner.setEnabled(false, &error));
        QVERIFY(other.state().enabled);
    }

    void legacyWindowsDisableSurvivesMigration_data()
    {
        QTest::addColumn<bool>("enable");
        QTest::newRow("enable") << true;
        QTest::newRow("disable-then-enable") << false;
    }

    void legacyWindowsDisableSurvivesMigration()
    {
        QFETCH(bool, enable);
        QTemporaryDir directory;
        const QString path = directory.filePath("login.ini");
        const QString approvalPath = directory.filePath("approval.ini");
        const QString executable = QStringLiteral("C:/Orchestrate.exe");
        const QString name = AutoStart::entryNameForExecutable(executable);
        const QByteArray disabled = QByteArray::fromHex("070000000000000000000000");
        QSettings registration(path, QSettings::IniFormat);
        registration.setValue("Orchestrate", QStringLiteral("\"C:\\Orchestrate.exe\""));
        registration.sync();
        QSettings approval(approvalPath, QSettings::IniFormat);
        approval.setValue("Orchestrate", disabled);
        approval.sync();
        AutoStart startup(store(path), store(approvalPath), executable);
        QVERIFY(startup.state().enabled);
        QVERIFY(startup.state().disabledByWindows);
        QString error;
        QVERIFY(startup.setEnabled(enable, &error));
        registration.sync();
        QVERIFY(!registration.contains("Orchestrate"));
        approval.sync();
        QCOMPARE(approval.value("Orchestrate").toByteArray(), disabled);
        QCOMPARE(approval.value(name).toByteArray(), disabled);
        AutoStart reopened(store(path), store(approvalPath), executable);
        QVERIFY(reopened.setEnabled(true, &error));
        QVERIFY(reopened.state().disabledByWindows);
    }

    void existingWindowsDisableIsNeverCleared()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("login.ini");
        const QString approvalPath = directory.filePath("approval.ini");
        const QString executable = QStringLiteral("C:/Orchestrate.exe");
        QSettings registration(path, QSettings::IniFormat);
        registration.setValue("Orchestrate", QStringLiteral("\"C:\\Orchestrate.exe\""));
        registration.sync();
        QSettings approval(approvalPath, QSettings::IniFormat);
        const QString name = AutoStart::entryNameForExecutable(executable);
        const QByteArray disabled = QByteArray::fromHex("030000000000000000000000");
        approval.setValue("Orchestrate", QByteArray::fromHex("020000000000000000000000"));
        approval.setValue(name, disabled);
        approval.sync();
        AutoStart startup(store(path), store(approvalPath), executable);
        QString error;
        QVERIFY(startup.setEnabled(true, &error));
        QVERIFY(startup.state().disabledByWindows);
        approval.sync();
        QCOMPARE(approval.value(name).toByteArray(), disabled);
    }

    void failedApprovalMigrationKeepsLegacyRegistration()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("login.ini");
        const QString executable = QStringLiteral("C:/Orchestrate.exe");
        QSettings registration(path, QSettings::IniFormat);
        registration.setValue("Orchestrate", QStringLiteral("\"C:\\Orchestrate.exe\""));
        registration.sync();
        QFile approvalFile(directory.filePath("approval.ini"));
        QVERIFY(approvalFile.open(QIODevice::WriteOnly));
        approvalFile.write("legacy approval fixture\n");
        approvalFile.close();
        const auto failingFormat = QSettings::registerFormat("approval-write-failure",
            [](QIODevice &, QSettings::SettingsMap &settings) {
                settings.insert("Orchestrate", QByteArray::fromHex("030000000000000000000000"));
                return true;
            },
            [](QIODevice &, const QSettings::SettingsMap &) { return false; });
        AutoStart startup(store(path), std::make_unique<QSettings>(approvalFile.fileName(), failingFormat), executable);
        QString error;
        QVERIFY(!startup.setEnabled(false, &error));
        QVERIFY(!error.isEmpty());
        registration.sync();
        QCOMPARE(registration.value("Orchestrate").toString(), QStringLiteral("\"C:\\Orchestrate.exe\""));
    }

    void unavailableAndWriteFailure()
    {
        AutoStart unavailable(nullptr, nullptr, QStringLiteral("C:/Orchestrate.exe"));
        QString error;
        QVERIFY(!unavailable.setEnabled(true, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!unavailable.state().error.isEmpty());
        QTemporaryDir directory;
        QFile blocker(directory.filePath("file"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        AutoStart startup(store(blocker.fileName() + "/login.ini"), nullptr, QStringLiteral("C:/Orchestrate.exe"));
        QVERIFY(!startup.setEnabled(true, &error));
        QVERIFY(!error.isEmpty());
    }

    void nativeRegistryRoundTrip()
    {
#ifdef Q_OS_WIN
        // Exercise REG_SZ and REG_BINARY at a disposable, non-startup key only.
        const QString testName = QStringLiteral("OrchestrateAutoStartTest-") + QUuid::createUuid().toString(QUuid::Id128);
        const QString root = QStringLiteral("HKEY_CURRENT_USER\\Software\\") + testName;
        QSettings fixture(root, QSettings::NativeFormat);
        struct Cleanup {
            QString name;
            ~Cleanup() {
                QSettings parent(QStringLiteral("HKEY_CURRENT_USER\\Software"), QSettings::NativeFormat);
                parent.remove(name);
                parent.sync();
            }
        } cleanup{testName};
        AutoStart startup(std::make_unique<QSettings>(root + "\\Run", QSettings::NativeFormat),
                          std::make_unique<QSettings>(root + "\\Approval", QSettings::NativeFormat),
                          QStringLiteral("C:/测试 目录/Orchestrate.exe"));
        const QString name = AutoStart::entryNameForExecutable(QStringLiteral("C:/测试 目录/Orchestrate.exe"));
        QVERIFY(!startup.state().enabled);
        QString error;
        QVERIFY2(startup.setEnabled(true, &error), qPrintable(error));
        QVERIFY(startup.state().enabled);
        fixture.sync();
        QCOMPARE(fixture.value("Run/" + name).toString(), QStringLiteral("\"C:\\测试 目录\\Orchestrate.exe\""));
        fixture.setValue("Approval/" + name, QByteArray::fromHex("030000000000000000000000"));
        fixture.sync();
        QVERIFY(startup.state().disabledByWindows);
        QVERIFY(startup.setEnabled(false, &error));
        QVERIFY(!startup.state().enabled);
        fixture.setValue("Run/Orchestrate", QStringLiteral("\"C:\\测试 目录\\Orchestrate.exe\""));
        fixture.setValue("Approval/Orchestrate", QByteArray::fromHex("070000000000000000000000"));
        fixture.sync();
        QVERIFY(startup.state().enabled);
        QVERIFY(startup.state().disabledByWindows);
        QVERIFY(startup.setEnabled(true, &error));
        fixture.sync();
        QVERIFY(!fixture.contains("Run/Orchestrate"));
        QCOMPARE(fixture.value("Approval/" + name).toByteArray(), QByteArray::fromHex("030000000000000000000000"));
        QVERIFY(startup.state().disabledByWindows);
        QVERIFY(startup.setEnabled(false, &error));
#else
        QSKIP("Windows registry only");
#endif
    }
};

QTEST_GUILESS_MAIN(AutoStartTest)
#include "test_autostart.moc"
