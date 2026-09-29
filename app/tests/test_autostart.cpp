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
    void persistenceAndPathChange()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("login.ini");
        const QString executable = QStringLiteral("C:/带 空格/Orchestrate.exe");
        QSettings other(path, QSettings::IniFormat);
        other.setValue("AnotherApp", "unchanged");
        other.sync();
        AutoStart startup(store(path), nullptr, executable);
        QVERIFY(!startup.state().enabled);
        QVERIFY(startup.state().error.isEmpty());
        QString error;
        QVERIFY2(startup.setEnabled(true, &error), qPrintable(error));
        other.sync();
        QCOMPARE(other.value("Orchestrate").toString(), '"' + QDir::toNativeSeparators(executable) + '"');
        AutoStart reopened(store(path), nullptr, executable);
        QVERIFY(reopened.state().enabled);
        AutoStart moved(store(path), nullptr, QStringLiteral("D:/New Folder/Orchestrate.exe"));
        QVERIFY(moved.state().message.contains(QStringLiteral("其他位置")));
        QVERIFY(moved.setEnabled(false, &error));
        QVERIFY(!reopened.state().enabled);
        QVERIFY(moved.setEnabled(true, &error));
        other.sync();
        QVERIFY(other.value("Orchestrate").toString().contains("New Folder"));
        QVERIFY(moved.setEnabled(false, &error));
        QVERIFY(moved.setEnabled(false, &error));
        other.sync();
        QVERIFY(!other.contains("Orchestrate"));
        QCOMPARE(other.value("AnotherApp").toString(), QStringLiteral("unchanged"));
    }

    void windowsDisableIsReportedAndPreserved()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("login.ini");
        const QString approvalPath = directory.filePath("approval.ini");
        AutoStart startup(store(path), store(approvalPath), QStringLiteral("C:/Orchestrate.exe"));
        QString error;
        QVERIFY(startup.setEnabled(true, &error));
        QSettings approval(approvalPath, QSettings::IniFormat);
        for (char marker : {2, 3, 6, 7}) {
            QByteArray bytes(12, '\0');
            bytes[0] = marker;
            approval.setValue("Orchestrate", bytes);
            approval.sync();
            QCOMPARE(startup.state().disabledByWindows, marker == 3 || marker == 7);
            QVERIFY(startup.setEnabled(true, &error));
            approval.sync();
            QCOMPARE(approval.value("Orchestrate").toByteArray(), bytes);
        }
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
        QVERIFY(!startup.state().enabled);
        QString error;
        QVERIFY2(startup.setEnabled(true, &error), qPrintable(error));
        QVERIFY(startup.state().enabled);
        fixture.sync();
        QCOMPARE(fixture.value("Run/Orchestrate").toString(), QStringLiteral("\"C:\\测试 目录\\Orchestrate.exe\""));
        fixture.setValue("Approval/Orchestrate", QByteArray::fromHex("030000000000000000000000"));
        fixture.sync();
        QVERIFY(startup.state().disabledByWindows);
        QVERIFY(startup.setEnabled(false, &error));
        QVERIFY(!startup.state().enabled);
#else
        QSKIP("Windows registry only");
#endif
    }
};

QTEST_GUILESS_MAIN(AutoStartTest)
#include "test_autostart.moc"
