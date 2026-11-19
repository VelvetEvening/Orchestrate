#include "data/appdatabase.h"
#include "platform/globalshortcut.h"
#include "widgets/shortcutsettings.h"
#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <optional>
#include <windows.h>

namespace {
constexpr int ProbeId = 17;
constexpr char IsolatedRun[] = "--orchestrate-shortcut-fixture";

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

int runWithPortableDatabase(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir fixture(QDir::temp().filePath(QStringLiteral("orchestrate-shortcut-test-XXXXXX")));
    if (!fixture.isValid()) {
        qCritical() << "Could not create a portable shortcut test directory.";
        return 1;
    }
    // AppDatabase follows the executable, so the real database opens inside the fixture.
    const auto executable = fixture.filePath(QStringLiteral("GlobalShortcutTest.exe"));
    if (!QFile::copy(app.applicationFilePath(), executable)) {
        qCritical() << "Could not stage the shortcut test executable.";
        return 1;
    }
    QProcess child;
    child.setProcessChannelMode(QProcess::ForwardedChannels);
    child.start(executable, QStringList{QString::fromLatin1(IsolatedRun)} + app.arguments().mid(1));
    if (!child.waitForStarted(5000) || !child.waitForFinished(20000)) {
        qCritical() << "Shortcut test did not complete:" << child.errorString();
        return 1;
    }
    return child.exitStatus() == QProcess::NormalExit ? child.exitCode() : 1;
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

    void settingsChangesReachNativeRegistrationImmediately()
    {
        QList<Qt::Key> letters;
        for (const auto key : {Qt::Key_J, Qt::Key_K, Qt::Key_U, Qt::Key_G, Qt::Key_B}) {
            if (available(QKeyCombination(Qt::AltModifier, key))
                && available(QKeyCombination(Qt::ControlModifier, key))) letters.append(key);
        }
        if (letters.size() < 2) QSKIP("Test combinations are already registered in this Windows session.");
        const QKeyCombination initial(Qt::AltModifier, letters.first());
        const QKeyCombination disabledEdit(Qt::ControlModifier, letters.at(1));
        const QKeyCombination modifierEdit(Qt::AltModifier, letters.at(1));
        const QString initialText = GlobalShortcut::text(initial);
        const QString disabledEditText = GlobalShortcut::text(disabledEdit);
        const QString modifierEditText = GlobalShortcut::text(modifierEdit);

        AppDatabase database;
        QString error;
        QVERIFY2(database.open(&error), qPrintable(error));
        QVERIFY(database.setSetting("global_shortcut_binding", initialText));
        QVERIFY(database.setSetting("global_shortcut_enabled", "false"));
        GlobalShortcut shortcut(false, initialText);
        ShortcutSettings settings(shortcut, database, nullptr);
        auto *toggle = settings.findChild<QAbstractButton *>("globalShortcutEnabled");
        auto *modifier = settings.findChild<QButtonGroup *>("globalShortcutModifier");
        auto *letter = settings.findChild<QLineEdit *>("globalShortcutLetter");
        auto *status = settings.findChild<QLabel *>("globalShortcutStatus");
        QVERIFY(toggle && modifier && letter && status && !toggle->isChecked());
        settings.show();

        modifier->button(int(Qt::ControlModifier))->click();
        QCOMPARE(shortcut.binding(), QKeyCombination(Qt::ControlModifier, initial.key()));
        QTest::keyClick(letter, disabledEdit.key());
        QCOMPARE(shortcut.binding(), disabledEdit);
        QCOMPARE(database.setting("global_shortcut_binding"), disabledEditText);
        QVERIFY(!shortcut.isRegistered() && available(disabledEdit));
        QVERIFY(status->isHidden());

        {
            QVERIFY(reserve(disabledEdit));
            const auto release = qScopeGuard([] { UnregisterHotKey(nullptr, ProbeId); });
            QTest::mouseClick(toggle, Qt::LeftButton);
            QVERIFY(!toggle->isChecked() && !shortcut.enabled() && !shortcut.isRegistered());
            QCOMPARE(toggle->text(), QStringLiteral("关闭"));
            QCOMPARE(database.setting("global_shortcut_enabled"), QStringLiteral("false"));
            QCOMPARE(database.setting("global_shortcut_binding"), disabledEditText);
            QVERIFY(status->isVisible() && status->text().contains(QStringLiteral("已被占用")));
        }

        toggle->setFocus();
        QTest::keyClick(toggle, Qt::Key_Space);
        QVERIFY(toggle->isChecked() && shortcut.enabled() && shortcut.isRegistered());
        QCOMPARE(toggle->text(), QStringLiteral("开启"));
        QCOMPARE(database.setting("global_shortcut_enabled"), QStringLiteral("true"));
        QVERIFY(!available(disabledEdit) && status->isHidden());

        modifier->button(int(Qt::AltModifier))->click();
        QCOMPARE(shortcut.binding(), modifierEdit);
        QCOMPARE(database.setting("global_shortcut_binding"), modifierEditText);
        QVERIFY(available(disabledEdit) && !available(modifierEdit));
        QTest::keyClick(letter, initial.key());
        QCOMPARE(shortcut.binding(), initial);
        QCOMPARE(database.setting("global_shortcut_binding"), initialText);
        QVERIFY(available(modifierEdit) && !available(initial));

        {
            QVERIFY(reserve(modifierEdit));
            const auto release = qScopeGuard([] { UnregisterHotKey(nullptr, ProbeId); });
            QTest::keyClick(letter, modifierEdit.key());
            QCOMPARE(shortcut.binding(), initial);
            QCOMPARE(letter->text(), QString(QChar(char16_t(initial.key()))));
            QCOMPARE(database.setting("global_shortcut_binding"), initialText);
            QVERIFY(toggle->isChecked() && !available(initial));
            QVERIFY(status->isVisible() && status->text().contains(modifierEditText)
                    && status->text().contains(initialText));
        }
        QTest::keyClick(letter, modifierEdit.key());
        QCOMPARE(shortcut.binding(), modifierEdit);
        QVERIFY(status->isHidden() && !available(modifierEdit));
        QTest::mouseClick(toggle, Qt::LeftButton);
        QVERIFY(!toggle->isChecked() && !shortcut.isRegistered() && available(modifierEdit));
        QCOMPARE(database.setting("global_shortcut_binding"), modifierEditText);
        QCOMPARE(database.setting("global_shortcut_enabled"), QStringLiteral("false"));
    }
};

int main(int argc, char **argv)
{
    if (argc < 2 || qstrcmp(argv[1], IsolatedRun) != 0) return runWithPortableDatabase(argc, argv);
    for (int index = 1; index + 1 < argc; ++index) argv[index] = argv[index + 1];
    argv[--argc] = nullptr;
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
