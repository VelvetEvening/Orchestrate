#include "globalshortcut.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QUuid>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

GlobalShortcut::GlobalShortcut(QObject *parent) : QObject(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            this, &GlobalShortcut::unregister);
}
GlobalShortcut::~GlobalShortcut()
{
    unregister();
    QCoreApplication::instance()->removeNativeEventFilter(this);
}
void GlobalShortcut::unregister()
{
#ifdef Q_OS_WIN
    if (registered_) UnregisterHotKey(nullptr, id_);
    if (id_) GlobalDeleteAtom(id_);
#endif
    registered_ = false;
    id_ = 0;
}
void GlobalShortcut::configure(bool enabled)
{
    unregister();
    if (!enabled) {
        status_ = QStringLiteral("Alt+X 已禁用，可通过托盘打开 Orchestrate。");
    } else if (QGuiApplication::platformName() == QStringLiteral("offscreen")
               || QGuiApplication::platformName() == QStringLiteral("minimal")) {
        status_ = QStringLiteral("测试环境：不注册系统全局快捷键。");
    } else {
#ifdef Q_OS_WIN
        const QString name = QStringLiteral("Orchestrate.Hotkey.") + QUuid::createUuid().toString();
        id_ = GlobalAddAtomW(reinterpret_cast<LPCWSTR>(name.utf16()));
        if (id_ && RegisterHotKey(nullptr, id_, MOD_ALT | MOD_NOREPEAT, 'X')) {
            registered_ = true;
            status_ = QStringLiteral("Alt+X 已启用：后台运行时可唤起 Orchestrate（完全退出后无效）。");
        } else {
            const DWORD error = GetLastError();
            unregister();
            status_ = error == ERROR_HOTKEY_ALREADY_REGISTERED
                ? QStringLiteral("Alt+X 注册失败：已被其他程序占用。解除冲突后可点击重试；仍可通过托盘打开 Orchestrate。")
                : QStringLiteral("Alt+X 注册失败（Windows 错误 %1）。可点击重试或通过托盘打开 Orchestrate。").arg(error);
        }
#else
        status_ = QStringLiteral("当前平台不支持此 Windows 全局快捷键。");
#endif
    }
    emit statusChanged(status_);
}
bool GlobalShortcut::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (registered_ && message && (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG")) {
        const auto *msg = static_cast<MSG *>(message);
        if (msg->message == WM_HOTKEY && msg->wParam == id_
            && LOWORD(msg->lParam) == MOD_ALT && HIWORD(msg->lParam) == 'X') {
            if (result) *result = 0;
            emit activated();
            return true;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return false;
}
