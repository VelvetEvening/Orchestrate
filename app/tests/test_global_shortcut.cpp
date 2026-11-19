#include "data/appdatabase.h"
#include "platform/globalshortcut.h"
#include "widgets/shortcutsettings.h"
#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QUuid>
#include <QtTest>
#include <optional>
#include <windows.h>

namespace {
constexpr int ProbeId = 17;
constexpr int ReservationId = 18;

bool reserve(QKeyCombination key, int id)
{
    const UINT modifier = key.keyboardModifiers() == Qt::ControlModifier ? MOD_CONTROL : MOD_ALT;
    return RegisterHotKey(nullptr, id, modifier | MOD_NOREPEAT, UINT(key.key()));
}

bool available(QKeyCombination key)
{
    return reserve(key, ProbeId) && UnregisterHotKey(nullptr, ProbeId);
}

std::optional<Qt::Key> availableLetter(bool includeControl = false)
{
    for (const auto letter : {Qt::Key_J, Qt::Key_K, Qt::Key_U, Qt::Key_G, Qt::Key_B}) {
        if (available(QKeyCombination(Qt::AltModifier, letter))
            && (!includeControl || available(QKeyCombination(Qt::ControlModifier, letter))))
            return letter;
    }
    return std::nullopt;
}

bool preparePortableFixture()
{
    // AppDatabase always uses the executable's data directory. Reuse only our own fixture.
    const auto path = QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath();
    if (path.isEmpty()) return false;
    const QDir executableDir(path);
    if (executableDir.dirName() != QStringLiteral("global-shortcut-test")) return false;
    const QDir data(executableDir.filePath(QStringLiteral("data")));
    const QFileInfo dataInfo(data.absolutePath());
    const QFileInfo database(data.filePath(QStringLiteral("orchestrate.sqlite3")));
    const QFileInfo marker(data.filePath(QStringLiteral(".global-shortcut-fixture")));
    for (const auto &entry : {dataInfo, database, marker}) {
        if (entry.isSymbolicLink() || entry.isJunction()) return false;
    }
    if (dataInfo.exists()) return dataInfo.isDir() && marker.isFile();
    if (!executableDir.mkdir(QStringLiteral("data"))) return false;
    QFile ownership(marker.absoluteFilePath());
    if (ownership.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return true;
    executableDir.rmdir(QStringLiteral("data"));
    return false;
}
}

class GlobalShortcutTest : public QObject {
    Q_OBJECT
private slots:
    void parsesSupportedBindingsAndDefaults()
    {
        GlobalShortcut shortcut(false, {});
        for (const auto key : {QKeyCombination(Qt::AltModifier, Qt::Key_A),
                               QKeyCombination(Qt::ControlModifier, Qt::Key_Z),
                               QKeyCombination(Qt::AltModifier, Qt::Key_J)}) {
            QVERIFY(shortcut.apply(false, key));
            GlobalShortcut restored(false, GlobalShortcut::text(key));
            QCOMPARE(restored.binding(), key);
        }
        QVERIFY(!shortcut.isRegistered());
        for (const auto *invalid : {"", "Ctrl+Alt+K", "Ctrl+7", "Alt+F2", "Ctrl+J, Ctrl+K", "X"}) {
            GlobalShortcut invalidShortcut(false, QString::fromLatin1(invalid));
            QCOMPARE(invalidShortcut.binding(), QKeyCombination(Qt::AltModifier, Qt::Key_X));
            QVERIFY(!invalidShortcut.enabled() && !invalidShortcut.isRegistered());
        }
    }

    void replacementKeepsOldRegistrationUntilSaved()
    {
        const auto letter = availableLetter(true);
        if (!letter) QSKIP("Test combinations are already registered in this Windows session.");
        const QKeyCombination oldKey(Qt::AltModifier, *letter);
        const QKeyCombination newKey(Qt::ControlModifier, *letter);
        const auto oldBinding = GlobalShortcut::text(oldKey);

        QVERIFY2(preparePortableFixture(), "Refusing an unowned database outside the dedicated shortcut test fixture.");
        AppDatabase database;
        QString error;
        QVERIFY2(database.open(&error), qPrintable(error));
        QVERIFY(database.setSetting("global_shortcut_binding", oldBinding));
        QVERIFY(database.setSetting("global_shortcut_enabled", "true"));

        GlobalShortcut shortcut(true, oldBinding);
        QVERIFY(shortcut.isRegistered());
        {
            QVERIFY(reserve(newKey, ReservationId));
            const auto release = qScopeGuard([] { UnregisterHotKey(nullptr, ReservationId); });
            bool saved = false;
            QVERIFY(!shortcut.apply(true, newKey, [&](QString *) { saved = true; return true; }));
            QVERIFY(!saved && !available(oldKey));
        }
        QCOMPARE(shortcut.binding(), oldKey);

        bool bothHeldDuringSave = false;
        const auto failSave = [](QString *message) { *message = QStringLiteral("Test write failed"); return false; };
        QVERIFY(!shortcut.apply(true, newKey, [&](QString *message) {
            bothHeldDuringSave = !available(oldKey) && !available(newKey);
            return failSave(message);
        }));
        QVERIFY(bothHeldDuringSave);
        QCOMPARE(shortcut.binding(), oldKey);
        QVERIFY(!available(oldKey) && available(newKey));
        QVERIFY(!shortcut.apply(false, oldKey, failSave));
        QVERIFY(shortcut.enabled() && !available(oldKey));

        // One real settings gesture must move the system registration to the new combination.
        ShortcutSettings settings(shortcut, database, nullptr);
        settings.show();
        auto *modifiers = settings.findChild<QButtonGroup *>("globalShortcutModifier");
        QVERIFY(modifiers && modifiers->button(int(Qt::ControlModifier)));
        QTest::mouseClick(modifiers->button(int(Qt::ControlModifier)), Qt::LeftButton);
        QCOMPARE(shortcut.binding(), newKey);
        QVERIFY(available(oldKey) && !available(newKey));

        MSG event{};
        event.message = WM_HOTKEY;
        event.wParam = shortcut.id_;
        QSignalSpy spy(&shortcut, &GlobalShortcut::activated);
        qintptr result = 1;
        event.lParam = MAKELPARAM(MOD_ALT, oldKey.key());
        QVERIFY(!shortcut.nativeEventFilter("windows_dispatcher_MSG", &event, &result));
        event.lParam = MAKELPARAM(MOD_CONTROL, newKey.key());
        QVERIFY(shortcut.nativeEventFilter("windows_dispatcher_MSG", &event, &result));
        QCOMPARE(result, qintptr(0));
        ++event.wParam;
        QVERIFY(!shortcut.nativeEventFilter("windows_dispatcher_MSG", &event, &result));
        QCOMPARE(spy.count(), 1);
        QVERIFY(shortcut.apply(false, newKey));
        QVERIFY(!shortcut.isRegistered() && available(newKey));
    }

    void retriesWhenStartupConflictIsReleased()
    {
        const auto letter = availableLetter();
        if (!letter) QSKIP("Test combinations are already registered in this Windows session.");
        const QKeyCombination key(Qt::AltModifier, *letter);
        QVERIFY(reserve(key, ReservationId));
        {
            GlobalShortcut shortcut(true, GlobalShortcut::text(key));
            const bool registered = shortcut.isRegistered();
            QVERIFY(UnregisterHotKey(nullptr, ReservationId));
            QVERIFY(shortcut.enabled() && !registered);
            QCOMPARE(shortcut.binding(), key);
            QVERIFY(shortcut.apply(true, key));
            QVERIFY(shortcut.isRegistered() && !available(key));
        }
        QVERIFY(available(key));
    }
};

int main(int argc, char **argv)
{
    // Keep test windows on a private desktop. Registrations can still conflict across desktops.
    const HDESK previous = GetThreadDesktop(GetCurrentThreadId());
    const auto name = QUuid::createUuid().toString(QUuid::Id128).toStdWString();
    const HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop) { qCritical() << "CreateDesktop failed" << GetLastError(); return 1; }
    const auto cleanup = qScopeGuard([&] { SetThreadDesktop(previous); CloseDesktop(desktop); });
    if (!SetThreadDesktop(desktop)) { qCritical() << "SetThreadDesktop failed" << GetLastError(); return 1; }
    qputenv("QT_QPA_PLATFORM", "windows");
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName(QString::fromStdWString(name));
    GlobalShortcutTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_global_shortcut.moc"
