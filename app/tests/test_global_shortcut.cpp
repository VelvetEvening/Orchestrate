#include "platform/globalshortcut.h"
#include <QGuiApplication>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest>
#include <optional>
#include <windows.h>

namespace {
constexpr int ProbeId = 17;

bool reserve(QKeyCombination key)
{
    const UINT modifier = key.keyboardModifiers() == Qt::ControlModifier ? MOD_CONTROL : MOD_ALT;
    return RegisterHotKey(nullptr, ProbeId, modifier | MOD_NOREPEAT, UINT(key.key()));
}

bool available(QKeyCombination key)
{
    if (!reserve(key)) return false;
    UnregisterHotKey(nullptr, ProbeId);
    return true;
}

std::optional<QKeyCombination> availableTestKey(Qt::KeyboardModifier modifier)
{
    for (const auto letter : {Qt::Key_J, Qt::Key_K, Qt::Key_U, Qt::Key_G, Qt::Key_B}) {
        const QKeyCombination key(modifier, letter);
        if (available(key)) return key;
    }
    return std::nullopt;
}
}

class GlobalShortcutTest : public QObject {
    Q_OBJECT
private slots:
    void allBindings()
    {
        GlobalShortcut shortcut(false, {});
        for (const auto modifier : {Qt::AltModifier, Qt::ControlModifier}) {
            for (int letter = Qt::Key_A; letter <= Qt::Key_Z; ++letter) {
                const QKeyCombination key(modifier, Qt::Key(letter));
                QVERIFY(shortcut.apply(false, key));
                GlobalShortcut restored(false, GlobalShortcut::text(key));
                QCOMPARE(restored.binding(), key);
            }
        }
        QVERIFY(!shortcut.isRegistered());
    }

    void changeAndRetry()
    {
        const auto alt = availableTestKey(Qt::AltModifier);
        const auto ctrl = availableTestKey(Qt::ControlModifier);
        if (!alt || !ctrl) QSKIP("Test combinations are already registered in this Windows session.");
        const auto oldKey = *alt;
        const auto newKey = *ctrl;
        GlobalShortcut shortcut(true, GlobalShortcut::text(oldKey));
        QVERIFY(shortcut.isRegistered());
        QVERIFY(reserve(newKey));
        bool saved = false;
        bool bothHeldDuringSave = false;
        const auto save = [&](QString *) {
            saved = true;
            bothHeldDuringSave = !available(oldKey) && !available(newKey);
            return true;
        };
        const bool changed = shortcut.apply(true, newKey, save);
        UnregisterHotKey(nullptr, ProbeId);
        QVERIFY(!changed && !saved);
        QCOMPARE(shortcut.binding(), oldKey);
        QVERIFY(shortcut.status().contains(GlobalShortcut::text(newKey)) && shortcut.status().contains(GlobalShortcut::text(oldKey)));
        QVERIFY(!available(oldKey));

        const auto failSave = [](QString *error) { *error = QStringLiteral("disk full"); return false; };
        QVERIFY(!shortcut.apply(true, newKey, failSave));
        QVERIFY(shortcut.status().contains("disk full"));
        QVERIFY(!available(oldKey) && available(newKey));
        QVERIFY(!shortcut.apply(false, oldKey, failSave));
        QVERIFY(shortcut.enabled() && !available(oldKey));
        QVERIFY(shortcut.apply(true, newKey, save));
        QVERIFY(saved && bothHeldDuringSave && available(oldKey) && !available(newKey));

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
    }

    void startupConflictAndInvalidPreferences()
    {
        const auto candidate = availableTestKey(Qt::AltModifier);
        if (!candidate) QSKIP("Test combinations are already registered in this Windows session.");
        const auto key = *candidate;
        QVERIFY(reserve(key));
        {
            GlobalShortcut shortcut(true, GlobalShortcut::text(key));
            const bool registered = shortcut.isRegistered();
            UnregisterHotKey(nullptr, ProbeId);
            QVERIFY(shortcut.enabled() && !registered);
            QCOMPARE(shortcut.binding(), key);
            QVERIFY(shortcut.apply(true, key));
        }
        QVERIFY(available(key));
        for (const auto *invalid : {"", "Ctrl+Alt+K", "Ctrl+7", "Alt+F2", "Ctrl+J, Ctrl+K", "X"}) {
            GlobalShortcut shortcut(false, QString::fromLatin1(invalid));
            QCOMPARE(shortcut.binding(), QKeyCombination(Qt::AltModifier, Qt::Key_X));
            QVERIFY(!shortcut.enabled() && !shortcut.isRegistered());
        }
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
    QGuiApplication app(argc, argv);
    GlobalShortcutTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_global_shortcut.moc"
